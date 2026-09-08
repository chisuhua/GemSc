// src/tlm/pcie/pcie_root_complex_tlm.cc
// PcieRootComplexTLM 实现 (T-P7-3)
// 作者 CppTLM Team / 日期 2027-01-19
#include "tlm/pcie/pcie_root_complex_tlm.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

namespace tlm::pcie {

    PcieRootComplexTLM::PcieRootComplexTLM(const std::string& name, EventQueue* eq)
        : name_(name), eq_(eq), axi_() {
    }

    void PcieRootComplexTLM::init() {
        axi_.reset();
        devices_.clear();
    }

    void PcieRootComplexTLM::attach_to_endpoint(PcieEndpointIP* ep) noexcept {
        ep_ = ep;
    }

    void PcieRootComplexTLM::detach() noexcept {
        ep_ = nullptr;
        devices_.clear();
    }

    // ===================== PCIe 枚举 =====================

    bool PcieRootComplexTLM::enumerate() {
        if (!ep_) {
            return false;
        }
        devices_.clear();

        // 发现 PF0 (slot 0)
        auto& pf_config = ep_->vf_pool().config_of(0);
        DiscoveredDevice dev;
        dev.device_id = 0;
        dev.function = 0;
        dev.vendor_id = static_cast<uint16_t>(pf_config.read(0x00) & 0xFFFF);
        dev.device_id_reg = static_cast<uint16_t>((pf_config.read(0x00) >> 16) & 0xFFFF);
        dev.class_code = pf_config.read(0x08);
        dev.revision_id = static_cast<uint8_t>(pf_config.read(0x08) & 0xFF);
        devices_.push_back(dev);

        // 注：VF 的发现在实际 PCIe 中需要 ARI capability + SR-IOV capability
        // 本模型简化：枚举只报告 PF0；VF 通过 stream_id 访问配置空间
        return true;
    }

    // ===================== 配置空间访问 =====================

    uint32_t PcieRootComplexTLM::config_read(uint16_t device, uint16_t function, uint16_t offset,
                                             uint16_t stream_id) {
        if (!ep_ || stream_id >= PcieEndpointIP::NUM_PORTS) {
            return 0xFFFFFFFFu;
        }
        (void)device;   // 当前仅支持 device=0
        (void)function; // 当前仅支持 function=0
        return ep_->vf_pool().config_of(stream_id).read(offset);
    }

    bool PcieRootComplexTLM::config_write(uint16_t device, uint16_t function, uint16_t offset,
                                          uint32_t value, uint16_t stream_id) {
        if (!ep_ || stream_id >= PcieEndpointIP::NUM_PORTS) {
            return false;
        }
        (void)device;
        (void)function;
        ep_->vf_pool().config_of(stream_id).write(offset, value);
        return true;
    }

    // ===================== BAR 管理 =====================

    bool PcieRootComplexTLM::bar_allocate(uint16_t device, uint16_t function, uint16_t bar_offset,
                                          uint32_t value) {
        if (!ep_) {
            return false;
        }
        (void)device;
        (void)function;
        // 直接写入 EP 配置空间（BAR 寄存器）
        ep_->vf_pool().config_of(0).write(bar_offset, value);
        return true;
    }

    // ===================== BAR 空间访问（经 AXI master 路由到 EP）=====================

    uint8_t PcieRootComplexTLM::bytes_to_awsize(uint8_t bytes) {
        uint8_t sz = 0;
        while ((1u << sz) < bytes) {
            ++sz;
        }
        return sz;
    }

    bool PcieRootComplexTLM::bar_write(uint16_t device, uint64_t addr, uint64_t data,
                                       uint8_t bytes) {
        if (!ep_) {
            return false;
        }
        (void)device;
        if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) {
            return false;
        }
        const uint16_t id = static_cast<uint16_t>((bar_tx_id_++ & 0xFFFF) + 1);

        bundles::Axi4Bundle req;
        req.awid.write(id);
        req.awaddr.write(addr);
        req.awlen.write(0);
        req.awsize.write(bytes_to_awsize(bytes));
        req.awburst.write(1);
        req.wdata.write(data);
        req.wstrb.write(static_cast<uint64_t>(0xFFu << 0));
        req.wlast.write(1);

        return axi_.master_req(req);
    }

    bool PcieRootComplexTLM::bar_read(uint16_t device, uint64_t addr, uint64_t& data,
                                      uint8_t bytes) {
        if (!ep_) {
            return false;
        }
        (void)device;
        if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) {
            return false;
        }
        const uint16_t id = static_cast<uint16_t>((bar_tx_id_++ & 0xFFFF) + 1);

        bundles::Axi4Bundle req;
        req.arid.write(id);
        req.araddr.write(addr);
        req.arlen.write(0);
        req.arsize.write(bytes_to_awsize(bytes));
        req.arburst.write(1);

        data = 0;
        return axi_.master_req(req);
    }

    // Phase 8 M1: tick() 在 RC ↔ EP 之间建立真实数据路径闭环（同 HostBypassTLM）。
    void PcieRootComplexTLM::tick() {
        if (ep_) {
            auto* ep_ax = PcieAxiAdapter::for_endpoint(ep_->getName());
            if (ep_ax) {
                cpptlm::Axi4StreamAdapter& ep_axi = ep_ax->axi();
                cpptlm::Axi4StreamAdapter& rc_axi = axi_;

                if (rc_axi.master_req_valid() && !ep_axi.slave_req_valid()) {
                    ep_axi.slave_req(rc_axi.master_req_data());
                    rc_axi.master_req_consume();
                }
                if (ep_axi.master_req_valid() && !rc_axi.slave_req_valid()) {
                    rc_axi.slave_req(ep_axi.master_req_data());
                    ep_axi.master_req_consume();
                }
                if (ep_axi.slave_resp_valid() && !rc_axi.master_resp_valid()) {
                    rc_axi.master_resp(ep_axi.slave_resp_data());
                    ep_axi.slave_resp_consume();
                }
                if (rc_axi.slave_resp_valid() && !ep_axi.master_resp_valid()) {
                    ep_axi.master_resp(rc_axi.slave_resp_data());
                    rc_axi.slave_resp_consume();
                }

                // 桥接转发完成后推进 EP adapter：转移 slave_resp/master_req 产出通道
                // （避免 EP tick 同周期 self-clear 响应，Phase 8 M1 修复）
                ep_axi.tick();
            }
        }
        axi_.tick();
    }

} // namespace tlm::pcie
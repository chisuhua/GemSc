// src/tlm/pcie/host_bypass_tlm.cc
// HostBypassTLM 实现 (T-P7-1 + T-P7-2)
// 作者 CppTLM Team / 日期 2027-01-19
#include "tlm/pcie/host_bypass_tlm.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

namespace tlm::pcie {

    HostBypassTLM::HostBypassTLM(const std::string& name, EventQueue* eq)
        : name_(name), eq_(eq), axi_() {
    }

    void HostBypassTLM::init() {
        axi_.reset();
    }

    void HostBypassTLM::attach_to_endpoint(PcieEndpointIP* ep) noexcept {
        ep_ = ep;
    }

    void HostBypassTLM::detach() noexcept {
        ep_ = nullptr;
    }

    // ===================== 软件 bring-up API (T-P7-2) =====================

    bool HostBypassTLM::config_write(uint16_t offset, uint32_t value, uint16_t stream_id) {
        if (!ep_ || stream_id >= PcieEndpointIP::NUM_PORTS) {
            return false;
        }
        // 经 EP 配置空间单一真源写入（PcieConfigSpace::write 处理对齐/越界/只读保护）
        ep_->vf_pool().config_of(stream_id).write(offset, value);
        return true;
    }

    uint32_t HostBypassTLM::config_read(uint16_t offset, uint16_t stream_id) {
        if (!ep_ || stream_id >= PcieEndpointIP::NUM_PORTS) {
            return 0xFFFFFFFFu;
        }
        return ep_->vf_pool().config_of(stream_id).read(offset);
    }

    uint8_t HostBypassTLM::bytes_to_awsize(uint8_t bytes) {
        // bytes ∈ {1,2,4,8} → awsize = log2(bytes)
        uint8_t sz = 0;
        while ((1u << sz) < bytes) {
            ++sz;
        }
        return sz;
    }

    bool HostBypassTLM::bar_write(uint64_t addr, uint64_t data, uint8_t bytes) {
        if (!ep_) {
            return false;
        }
        if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) {
            return false;
        }
        const uint16_t id = static_cast<uint16_t>((bar_tx_id_++ & 0xFFFF) + 1);

        bundles::Axi4Bundle req;
        req.awid.write(id);
        req.awaddr.write(addr);
        req.awlen.write(0);                       // 单拍
        req.awsize.write(bytes_to_awsize(bytes)); // 2^awsize 字节/拍
        req.awburst.write(1);                     // INCR
        req.wdata.write(data);
        req.wstrb.write(static_cast<uint64_t>(0xFFu << 0)); // 全 strobe（8 字节内）
        req.wlast.write(1);

        return axi_.master_req(req);
    }

    bool HostBypassTLM::bar_read(uint64_t addr, uint64_t& data, uint8_t bytes) {
        if (!ep_) {
            return false;
        }
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

        data = 0; // 输出初始化（读数据经 axi_master_resp 呈现，此处仅为占位）
        return axi_.master_req(req);
    }

    // Phase 8 M1: tick() 在 hb ↔ EP 之间建立真实数据路径闭环。
    //  hb.axi_master_req_valid → ep.axi_slave_in（host → EP）
    //  ep.axi_master_out_valid → hb.axi_slave_in（EP → host，反向桥接）
    //  ep.axi_slave_resp_valid → hb.axi_master_resp（EP 响应 → host 主通道）
    //  让 hb 与 EP 真正双向互通，EP 内部消费请求并产生真实响应（M1 闭合）。
    void HostBypassTLM::tick() {
        if (ep_) {
            auto* ep_ax = PcieAxiAdapter::for_endpoint(ep_->getName());
            if (ep_ax) {
                cpptlm::Axi4StreamAdapter& ep_axi = ep_ax->axi();
                cpptlm::Axi4StreamAdapter& hb_axi = axi_;

                // host master_out → EP slave_in
                if (hb_axi.master_req_valid() && !ep_axi.slave_req_valid()) {
                    ep_axi.slave_req(hb_axi.master_req_data());
                    hb_axi.master_req_consume();
                }

                // EP master_out → host slave_in
                if (ep_axi.master_req_valid() && !hb_axi.slave_req_valid()) {
                    hb_axi.slave_req(ep_axi.master_req_data());
                    ep_axi.master_req_consume();
                }

                // EP slave_resp → host master_resp
                if (ep_axi.slave_resp_valid() && !hb_axi.master_resp_valid()) {
                    hb_axi.master_resp(ep_axi.slave_resp_data());
                    ep_axi.slave_resp_consume();
                }

                // host slave_resp → EP master_resp（反向
                if (hb_axi.slave_resp_valid() && !ep_axi.master_resp_valid()) {
                    ep_axi.master_resp(hb_axi.slave_resp_data());
                    hb_axi.slave_resp_consume();
                }

                // 桥接转发完成后推进 EP adapter：转移 slave_resp/master_req 产出通道
                // （避免 EP tick 同周期 self-clear 响应，Phase 8 M1 修复）
                ep_axi.tick();
            }
        }
        axi_.tick();
    }

} // namespace tlm::pcie
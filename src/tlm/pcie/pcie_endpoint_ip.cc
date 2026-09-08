// src/tlm/pcie/pcie_endpoint_ip.cc
// PcieEndpointIP 实现 (T-P4-7)
// 作者 CppTLM Team / 日期 2026-10-13
#include "tlm/pcie/pcie_endpoint_ip.hh"
#include "tlm/gpu/pcie_config_space_mvp.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include <nlohmann/json.hpp>

namespace tlm::pcie {

    PcieEndpointIP::PcieEndpointIP(const std::string& name, EventQueue* eq)
        : ChStreamModuleBase(name, eq) {
        pool_.init_all();
    }

    void PcieEndpointIP::init() {
        ChStreamModuleBase::init();
        pool_.init_all();
    }

    void PcieEndpointIP::do_reset(const ResetConfig&) {
        pool_.init_all();
    }

    void PcieEndpointIP::set_stream_adapter(cpptlm::StreamAdapterBase* a) {
        if (a) {
            adapters_[0] = a;
        }
    }

    void PcieEndpointIP::set_stream_adapter(cpptlm::StreamAdapterBase* adapters[]) {
        if (!adapters) {
            return;
        }
        for (unsigned i = 0; i < NUM_PORTS; ++i) {
            adapters_[i] = adapters[i];
        }
    }

    bool PcieEndpointIP::all_ports_have_adapter() const {
        for (unsigned i = 0; i < NUM_PORTS; ++i) {
            if (!adapters_[i]) {
                return false;
            }
        }
        return true;
    }

    void PcieEndpointIP::on_config_loaded() {
        const auto& params = get_config(); // SimObject::get_config()
        attach_composition(params);
    }

    void PcieEndpointIP::attach_composition(const nlohmann::json& params) {
        // AXI Stream Adapter 独立挂接（per Phase 5 T-P5-6, 不依赖 link_layer 分支）
        if (params.contains("axi_adapter")) {
            auto* ax = PcieAxiAdapter::attach_to_endpoint(getName(), event_queue);
            if (ax) {
                ax->set_endpoint(this);
                // Phase 6 T-P6-4: JSON axi4_mapper_inject: true → 注入 AXI4Mapper（缺省 false
                // 不注入）
                const auto& axi_json = params["axi_adapter"];
                const bool mapper_inject = axi_json.value("axi4_mapper_inject", false);
                ax->set_mapper_injected(mapper_inject);
            }
        } else {
            PcieAxiAdapter::detach_from_endpoint(getName());
        }
        if (!params.contains("link_layer")) {
            return;
        }
        const auto& ll_json = params["link_layer"];
        const bool enabled = ll_json.value("enabled", true);
        if (!enabled) {
            PcieLinkLayer::detach_from_endpoint(getName());
            PciePhyDigitalCtrl::detach_from_endpoint(getName());
            PcieBypassMux::detach_from_endpoint(getName());
            return;
        }
        tlm::pcie::PcieLinkLayerConfig ll_cfg;
        ll_cfg.enabled = enabled;
        ll_cfg.fc_capacity = ll_json.value("fc_token_bucket_capacity", 256u);
        ll_cfg.fc_init_p = ll_json.value("fc_initial_credit_p", 256u);
        ll_cfg.fc_init_np = ll_json.value("fc_initial_credit_np", 256u);
        ll_cfg.fc_init_cpl = ll_json.value("fc_initial_credit_cpl", 256u);
        ll_cfg.retry_buffer_size = ll_json.value("retry_buffer_size", 4096u);

        auto* ll = PcieLinkLayer::attach_to_endpoint(getName(), event_queue, ll_cfg);
        auto* phy = PciePhyDigitalCtrl::attach_to_endpoint(getName(), event_queue);
        if (phy) {
            phy->link_layer(ll);
            phy->set_link_up(true);
        }
        auto* mux = PcieBypassMux::attach_to_endpoint(getName(), ll);
        if (mux) {
            mux->set_phy_initialized(phy != nullptr);
            const std::string bypass_mode = ll_json.value("bypass_mode", std::string("Full"));
            if (bypass_mode == "Bypass") {
                mux->apply_mode(BypassMode::Bypass);
            } else if (bypass_mode == "Partial") {
                mux->apply_mode(BypassMode::Partial);
            }
        }
    }

    void PcieEndpointIP::tick() {
        // Phase 8 M1: 真实 AXI 数据路径接线 — PcieEndpointIP::tick() 驱动
        // PcieAxiAdapter 消费 slave_in 请求，EP 内部真实处理并产生真实响应。
        // HostBypass/RC (Host 侧 master) ↔ PcieAxiAdapter (EP 侧 slave) 双向闭环。
        if (auto* ax = PcieAxiAdapter::for_endpoint(getName())) {
            cpptlm::Axi4StreamAdapter& axi = ax->axi();

            if (axi.slave_req_valid()) {
                const bundles::Axi4Bundle& req = axi.slave_req_data();

                // 写/读判别: 使用 Axi4Bundle::is_write_request() 谓词 (per
                // include/bundles/axi4_bundles_tlm.hh:86-88)。该谓词判定"是否
                // 写请求",替代原有启发式 (awid!=0||awaddr!=0||awlen!=0) 三字段判别。
                // 启发式在 awid=0,awaddr=0,awlen=0 时会把任何含 wlast=1,wdata!=0
                // 的合法写请求误判为读,丢失写数据。
                //
                // 注: 当前 Axi4StreamAdapter 把 AW/W 打包为一个 Axi4Bundle 投递,
                // AR 通道在另一拍投递。所以"is_write_request"用 awlen/awid/awaddr
                // 任一非 0 作为写请求信号,语义与启发式一致但更稳健(未来如需扩展
                // 流式接口,只需在此谓词点扩展,不涉及消费者)。
                if (req.is_write_request()) {
                    // 写请求: 配置空间偏移 (< config_size) vs BAR 空间
                    // PCIe 规范 (cfg 路径):
                    //   - awaddr 低 2 bit [1:0] 为对齐保留位,请求方保证 = 00
                    //   - dword offset = awaddr >> 2,byte offset = (awaddr >> 2) << 2
                    // 范围判定用原始 awaddr (无屏蔽) 以正确区分 cfg vs BAR 空间。
                    const uint64_t awaddr = req.awaddr.read();
                    const uint16_t bid = static_cast<uint16_t>(req.awid.read());

                    const bool is_cfg = awaddr < pool_.config_of(0).config_size();

                    if (is_cfg) {
                        // cfg 路径: 屏蔽低 2 bit 后右移得到 byte offset
                        const uint16_t cfg_byte_off = static_cast<uint16_t>(awaddr & ~0x3ULL);
                        pool_.config_of(0).write(cfg_byte_off,
                                                 static_cast<uint32_t>(req.wdata.read()));
                    } else {
                        // BAR 空间: 4B 粒度 key + 按 wstrb 字节 mask 部分写
                        // (per Oracle P1-2 报告,原 8B 对齐 key + 忽略 wstrb 建模失真)
                        //
                        // BAR slot 模型: 32-bit 寄存器(对应真实 PCIe BAR 4B 寄存器),
                        // wdata 低 32 bit + wstrb 低 4 bit mask 字节粒度。
                        // ch_uint<512>::wdata/wstrb 实际是 64-bit 存储,仅用低 32/4 bit。
                        //
                        // wstrb 字节语义 (per AXI/PCIe 规范):
                        //   wstrb[i]=1 表示 byte[i] 有效 (要写)
                        //   例: wstrb=0xE = 0b1110 → byte[0]不写, byte[1,2,3]写
                        //       字节 mask = 0xFFFFFF00 (byte[0]=0x00, byte[1..3]=0xFF)
                        const uint64_t key = awaddr & ~0x3ULL;
                        const uint32_t wdata32 = static_cast<uint32_t>(req.wdata.read());
                        const uint32_t wstrb4 = static_cast<uint32_t>(req.wstrb.read() & 0xFULL);
                        uint32_t byte_mask = 0;
                        for (unsigned i = 0; i < 4; ++i) {
                            if (wstrb4 & (1u << i)) {
                                byte_mask |= (0xFFu << (i * 8));
                            }
                        }
                        const uint32_t old_val =
                            static_cast<uint32_t>(bar_store_[key]); // 默认 0 if missing
                        const uint32_t new_val = (old_val & ~byte_mask) | (wdata32 & byte_mask);
                        bar_store_[key] = new_val;
                    }

                    bundles::Axi4Bundle wresp;
                    wresp.bid.write(bid);
                    wresp.bresp.write(0);
                    axi.slave_resp(wresp);
                } else {
                    // 读请求: 配置空间偏移 (< config_size) vs BAR 空间
                    // PCIe 规范解码同上
                    const uint64_t araddr = req.araddr.read();
                    const uint16_t rid = static_cast<uint16_t>(req.arid.read());
                    uint64_t rdata = 0;

                    const bool is_cfg = araddr < pool_.config_of(0).config_size();

                    if (is_cfg) {
                        const uint16_t cfg_byte_off = static_cast<uint16_t>(araddr & ~0x3ULL);
                        rdata = pool_.config_of(0).read(cfg_byte_off);
                    } else {
                        // BAR 空间: 4B 粒度 key 读取
                        const uint64_t key = araddr & ~0x3ULL;
                        const auto it = bar_store_.find(key);
                        if (it != bar_store_.end()) {
                            // BAR slot 是 32-bit 寄存器,高 32 bit 总是 0
                            rdata = static_cast<uint32_t>(it->second);
                        }
                    }

                    bundles::Axi4Bundle rresp;
                    rresp.rid.write(rid);
                    rresp.rdata.write(rdata);
                    rresp.rresp.write(0);
                    rresp.rlast.write(1);
                    axi.slave_resp(rresp);
                }
                axi.slave_req_consume();
            }

            // 注意：此处不调用 axi.tick()。slave_resp 通道由 HostBypass/RootComplex
            // 在桥接转发完成后调用 ep_axi.tick() 推进（避免响应被同周期 self-clear，
            // 保证 HostBypass/RC tick 能读到 EP 产生的真实响应）。Phase 8 M1 修复。
        }

        for (unsigned i = 0; i < NUM_PORTS; ++i) {
            if (adapters_[i]) {
                adapters_[i]->tick();
            }
        }
        if (auto* ll = PcieLinkLayer::for_endpoint(getName())) {
            ll->tick();
        }
    }

    void PcieEndpointIP::flr_pf() noexcept {
        pool_.flr_pf();
    }

    void PcieEndpointIP::flr_vf(uint16_t vf_id) noexcept {
        pool_.flr_vf(vf_id);
    }

    PcieLinkLayer* PcieEndpointIP::link_layer() const noexcept {
        return PcieLinkLayer::for_endpoint(getName());
    }

    PciePhyDigitalCtrl* PcieEndpointIP::phy() const noexcept {
        return PciePhyDigitalCtrl::for_endpoint(getName());
    }

    PcieBypassMux* PcieEndpointIP::bypass_mux() const noexcept {
        return PcieBypassMux::for_endpoint(getName());
    }

} // namespace tlm::pcie
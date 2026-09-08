// src/tlm/gpu/pcie_endpoint_tlm.cc
// PcieEndpointTLM 实现：4 端口 PCIe slave 模型
// 作者 CppTLM Team / 日期 2026-08-26

#include "tlm/gpu/pcie_endpoint_tlm.h"

#include <algorithm>
#include <stdexcept>
#include <vector>
#include "bundles/pcie_bundles_tlm.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_bypass_mux.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"
#include "tlm/pcie/pcie_phy_digital_ctrl_tlm.hh"

namespace tlm::gpu {

    namespace {
        // JSON → Access 转换
        PcieBarRouter::Access parse_access(const std::string& s) {
            if (s == "RO" || s == "ro")
                return PcieBarRouter::Access::RO;
            if (s == "WO" || s == "wo")
                return PcieBarRouter::Access::WO;
            return PcieBarRouter::Access::RW; // 默认
        }

        // JSON → SideEffect 转换
        PcieBarRouter::SideEffect parse_side_effect(const std::string& s) {
            if (s == "doorbell")
                return PcieBarRouter::SideEffect::DOORBELL;
            return PcieBarRouter::SideEffect::NONE;
        }

        // JSON params.link_layer → PcieLinkLayerConfig（composition 集成通道）
        // per design.md §10.1：fc_token_bucket_capacity / retry_buffer_size /
        // link_error_injection.enabled；无 fc_refill_rate（per Q2 修订）
        tlm::pcie::PcieLinkLayerConfig parse_link_layer_config(const nlohmann::json& cfg) {
            tlm::pcie::PcieLinkLayerConfig lc;
            if (!cfg.is_object())
                return lc;
            lc.enabled = cfg.value("enabled", true);
            lc.fc_capacity = cfg.value("fc_token_bucket_capacity", 256u);
            lc.fc_init_p = cfg.value("fc_initial_credit_p", lc.fc_capacity);
            lc.fc_init_np = cfg.value("fc_initial_credit_np", lc.fc_capacity);
            lc.fc_init_cpl = cfg.value("fc_initial_credit_cpl", lc.fc_capacity);
            lc.retry_buffer_size = cfg.value("retry_buffer_size", 4096u);
            if (cfg.contains("link_error_injection")) {
                lc.link_error_injection_enabled =
                    cfg["link_error_injection"].value("enabled", false);
            }
            return lc;
        }

        // C1 (Oracle C1): 按 Bypass Mux mode 分派 TLP 入口数据路径。
        //   - Full   : 完整链路 → TLP 过 PcieLinkLayer (FC 门控 + Rx ACK + rate-switch 检查)
        //   - Bypass : 短路链路层 → TLP 直接送事务层 (绕过 LL FC/busy/rate-switch 检查)
        //   - Partial: 跳过 PHY 阶段但保留 LL FC/DLLP → 数据路径与 Full 一致 (LL 仍工作)
        // 无 mux (未配置 bypass_mode) 时回退：有 LL 则走 LL, 无 LL 直通。
        // 返回 false = 反压 (TLP 保持 pending 不消费)。
        bool dispatch_tlp_entry(PcieEndpointTLM& ep, const bundles::PcieTlpBundle& req) {
            auto* ll = tlm::pcie::PcieLinkLayer::for_endpoint(ep.getName());
            auto* mux = tlm::pcie::PcieBypassMux::for_endpoint(ep.getName());
            if (mux && mux->mode() == tlm::pcie::BypassMode::Bypass) {
                // Bypass: 绕过链路层 (不检查 FC / rate-switch / wire busy)
                return true;
            }
            // Full / Partial / 无 mux：过链路层 FC 门控 + ACK 生成
            if (ll) {
                return ll->rx_tlp_from_host(req);
            }
            return true;
        }
    } // namespace

    PcieEndpointTLM::PcieEndpointTLM(const std::string& name, EventQueue* eq)
        : ChStreamModuleBase(name, eq), cfg_space_(std::make_unique<PcieConfigSpace>()),
          bar_router_(std::make_unique<PcieBarRouter>()), msix_(std::make_unique<MsiXTable>()) {
    }

    PcieEndpointTLM::~PcieEndpointTLM() = default;

    void PcieEndpointTLM::init() {
        cfg_space_->init();
        bar_router_->init();
        msix_->init();
    }

    void PcieEndpointTLM::do_reset(const ResetConfig&) {
        for (unsigned i = 0; i < NUM_PORTS; i++) {
            req_in[i].reset();
            resp_out[i].reset();
        }
    }

    void PcieEndpointTLM::set_stream_adapter(cpptlm::StreamAdapterBase* a) {
        // 单端口回退（向后兼容）：仅处理 port 0
        adapters_[0] = a;
    }

    void PcieEndpointTLM::set_stream_adapter(cpptlm::StreamAdapterBase* adapters[]) {
        // 4 端口多端口注入（per spec.md Scenario "All 4 ports receive non-null StreamAdapter"）
        for (unsigned i = 0; i < NUM_PORTS; i++) {
            adapters_[i] = adapters[i];
        }
    }

    bool PcieEndpointTLM::all_ports_have_adapter() const {
        for (unsigned i = 0; i < NUM_PORTS; i++) {
            if (adapters_[i] == nullptr)
                return false;
        }
        return true;
    }

    void PcieEndpointTLM::on_config_loaded() {
        // params 从 sim_object set_config 注入
        const auto& cfg = get_config();
        if (!cfg.is_object())
            return;

        // config_size 参数化 PcieConfigSpace
        if (cfg.contains("config_size")) {
            const std::size_t sz = cfg.value("config_size", 4096u);
            if (sz != 256 && sz != 4096) {
                throw std::invalid_argument("PcieEndpointTLM: config_size must be 256 or 4096");
            }
            cfg_space_ = std::make_unique<PcieConfigSpace>(sz);
        }

        // msix_num_vectors 参数化 MsiXTable
        if (cfg.contains("msix_num_vectors")) {
            const uint16_t n = cfg.value("msix_num_vectors", 16);
            msix_ = std::make_unique<MsiXTable>(n);
        }

        // 重新初始化子组件
        cfg_space_->init();
        bar_router_->init();
        msix_->init();

        // 应用 capabilities（chain 声明）
        if (cfg.contains("capabilities")) {
            apply_capabilities_config(cfg);
        }

        // 应用 bar0_registers（数据化寄存器表）
        if (cfg.contains("bar0_registers")) {
            apply_bar0_registers_config(cfg);
        }

        // composition 集成 PcieLinkLayer（per Phase 1 T-P1-7，不改 .h 布局）
        // JSON params.link_layer.enabled=true → 挂接链路层（FC/ACK-NAK/Rx 双向）
        if (cfg.contains("link_layer")) {
            const auto ll_cfg = parse_link_layer_config(cfg["link_layer"]);
            if (ll_cfg.enabled) {
                auto* ll =
                    tlm::pcie::PcieLinkLayer::attach_to_endpoint(getName(), event_queue, ll_cfg);
                const std::string bypass_mode = cfg["link_layer"].value("bypass_mode", "Full");
                auto* phy =
                    tlm::pcie::PciePhyDigitalCtrl::attach_to_endpoint(getName(), event_queue);
                auto* mux = tlm::pcie::PcieBypassMux::attach_to_endpoint(getName(), ll);
                if (phy && mux) {
                    phy->link_layer(ll);
                    phy->mux(mux); // C5: Surprise Removal 需要 mux 清理
                    mux->set_phy_initialized(phy != nullptr);
                    phy->set_link_up(true);
                    if (bypass_mode == "Bypass") {
                        mux->apply_mode(tlm::pcie::BypassMode::Bypass);
                    } else if (bypass_mode == "Partial") {
                        mux->apply_mode(tlm::pcie::BypassMode::Partial);
                    }
                }
            } else {
                tlm::pcie::PcieLinkLayer::detach_from_endpoint(getName());
                tlm::pcie::PciePhyDigitalCtrl::detach_from_endpoint(getName());
                tlm::pcie::PcieBypassMux::detach_from_endpoint(getName());
            }
        }

        // composition 集成 PcieAxiAdapter（per Phase 5 T-P5-6，不改 .h 布局）
        // JSON params.axi_adapter → 挂接 AXI Stream Adapter（axi_master_out /
        // axi_slave_in / cfg_slave_in 三端口），供 SoC 侧 for_endpoint() 消费。
        if (cfg.contains("axi_adapter")) {
            tlm::pcie::PcieAxiAdapter::attach_to_endpoint(getName(), event_queue);
        } else {
            tlm::pcie::PcieAxiAdapter::detach_from_endpoint(getName());
        }
    }

    void PcieEndpointTLM::apply_capabilities_config(const nlohmann::json& cfg) {
        for (const auto& cap : cfg["capabilities"]) {
            const uint8_t id = cap.value("id", uint8_t{0});
            const uint8_t offset = cap.value("offset", uint8_t{0});
            const uint8_t next = cap.value("next", uint8_t{0});
            const uint16_t control = cap.value("control", uint16_t{0});
            if (!cfg_space_->add_capability(id, offset, next, control)) {
                // 失败时静默（per spec.md：capability 重叠不应 crash）
            }
        }
    }

    void PcieEndpointTLM::apply_bar0_registers_config(const nlohmann::json& cfg) {
        for (const auto& reg : cfg["bar0_registers"]) {
            const uint32_t offset = reg.value("offset", uint32_t{0});
            const std::string name = reg.value("name", std::string{"REG"});
            const std::string access_s = reg.value("access", std::string{"RW"});
            const std::string effect_s = reg.value("side_effect", std::string{"none"});
            const uint32_t stream_id = reg.value("stream_id", uint32_t{0});
            if (!bar_router_->add_register(offset, name, parse_access(access_s),
                                           parse_side_effect(effect_s), stream_id)) {
                // 失败静默（offset 越界/未对齐/重叠）
            }
        }
    }

    void PcieEndpointTLM::handle_slave_in_tlp() {
        if (!req_in[PORT_SLAVE_IN].valid())
            return;
        const auto& req = req_in[PORT_SLAVE_IN].data();
        const uint8_t kind = req.kind.read();

        // composition: 按 Bypass Mux mode 分派 TLP 数据路径 (C1)。
        //   Full/Partial → 过 PcieLinkLayer (FC 门控 + Rx ACK)；Bypass → 短路链路层直送事务层。
        // 返回 false = 反压 (FC 不足 / rate-switch 中 / wire busy)，req 保持 pending。
        if (!dispatch_tlp_entry(*this, req)) {
            return;
        }

        bundles::PcieTlpBundle resp;
        resp.bar_index.write(req.bar_index.read());
        resp.offset.write(req.offset.read());
        resp.size.write(req.size.read());
        resp.requester_id.write(req.requester_id.read());
        resp.trans_id.write(req.trans_id.read());

        switch (kind) {
        case bundles::PcieTlpBundle::CFG_READ: {
            const uint16_t cfg_offset = static_cast<uint16_t>(req.offset.read());
            resp.data.write(cfg_space_->read(cfg_offset));
            resp.kind.write(bundles::PcieTlpBundle::CFG_READ);
            resp_out[PORT_MMIO_OUT].write(resp);
            break;
        }
        case bundles::PcieTlpBundle::CFG_WRITE: {
            const uint16_t cfg_offset = static_cast<uint16_t>(req.offset.read());
            cfg_space_->write(cfg_offset, static_cast<uint32_t>(req.data.read()));
            resp.kind.write(bundles::PcieTlpBundle::CFG_WRITE);
            resp_out[PORT_MMIO_OUT].write(resp);
            break;
        }
        case bundles::PcieTlpBundle::MMIO_READ: {
            const uint32_t off = static_cast<uint32_t>(req.offset.read());
            resp.data.write(bar_router_->mmio_read(off));
            resp.kind.write(bundles::PcieTlpBundle::MMIO_READ);
            resp_out[PORT_MMIO_OUT].write(resp);
            break;
        }
        case bundles::PcieTlpBundle::MMIO_WRITE: {
            const uint32_t off = static_cast<uint32_t>(req.offset.read());
            const uint32_t val = static_cast<uint32_t>(req.data.read());
            bar_router_->mmio_write(off, val, req.trans_id.read());
            resp.kind.write(bundles::PcieTlpBundle::MMIO_WRITE);
            resp_out[PORT_MMIO_OUT].write(resp);
            break;
        }
        case bundles::PcieTlpBundle::MEM_READ:
        case bundles::PcieTlpBundle::MEM_WRITE: {
            // BAR1 MEM 转发：descriptor-only, size > 8 时 data=0 (per design.md §2.3)
            if (req.size.read() > 8) {
                resp.data.write(0);
            } else {
                // size <= 8: 内联 data 直接转发
                resp.data.write(req.data.read());
            }
            resp.kind.write(kind);
            resp_out[PORT_MEM_OUT].write(resp);
            break;
        }
        default:
            break; // 未知 kind 静默丢弃
        }

        req_in[PORT_SLAVE_IN].consume();
    }

    void PcieEndpointTLM::tick() {
        // 1. 处理 slave_in 入口
        handle_slave_in_tlp();

        // 2. 推进 bar_router_ 周期（完成 doorbell 强序写）
        bar_router_->tick();

        // 3. mmio_out 端口：把 bar_router_ 中到期的 doorbell 事件 consume
        // (resp_out[PORT_MMIO_OUT] 已经在 handle_slave_in_tlp 中写入了响应,
        //  这里只负责清空 doorbell 副作用队列)
        while (bar_router_->try_pop_doorbell_out()) {
            bar_router_->consume_doorbell_out();
        }

        // 4. irq_out 端口：从 MsiXTable pending 拉取并写出（PcieTlpBundle{IRQ_DELIVERY}）
        while (const auto* evt = msix_->try_pop_irq_out()) {
            bundles::PcieTlpBundle irq_tlp;
            irq_tlp.kind.write(bundles::PcieTlpBundle::IRQ_DELIVERY);
            irq_tlp.offset.write(static_cast<uint64_t>(evt->vector)); // vector 编码到 offset
            irq_tlp.size.write(evt->msg_data);                        // msg_data 编码到 size
            irq_tlp.data.write(evt->msg_addr);                        // msg_addr 编码到 data
            irq_tlp.trans_id.write(evt->trans_id);
            resp_out[PORT_IRQ_OUT].write(irq_tlp);
            msix_->consume_irq_out();
        }

        // 5. 调用各 adapter 的 tick()（如需要）
        for (unsigned i = 0; i < NUM_PORTS; i++) {
            if (adapters_[i])
                adapters_[i]->tick();
        }

        // 6. composition: 推进 PcieLinkLayer（消费错误注入等周期逻辑）
        if (auto* ll = tlm::pcie::PcieLinkLayer::for_endpoint(getName())) {
            ll->tick();
        }
    }

} // namespace tlm::gpu
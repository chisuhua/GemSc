// src/tlm/pcie/pcie_bypass_mux.cc
// PcieBypassMux 实现: 3 态 Bypass Mux + apply_mode 10 步清理
// 作者 CppTLM Team / 日期 2026-10-06
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §7
//       decisions.md §Q7 + §Q14

#include "tlm/pcie/pcie_bypass_mux.hh"

#include "tlm/pcie/pcie_link_layer_tlm.hh"

#include <memory>
#include <string>
#include <unordered_map>

namespace tlm::pcie {

    namespace {

        // PcieEndpointTLM ↔ PcieBypassMux composition 注册表（按 EP 模块名）
        // 冻结 .h 布局不能加成员 → EP 侧通过 on_config_loaded() 挂接，这里持有生命周期。
        std::unordered_map<std::string, std::unique_ptr<PcieBypassMux>>& mux_registry() {
            static std::unordered_map<std::string, std::unique_ptr<PcieBypassMux>> reg;
            return reg;
        }

    } // namespace

    PcieBypassMux* PcieBypassMux::attach_to_endpoint(const std::string& endpoint_name,
                                                     PcieLinkLayer* ll) {
        auto& reg = mux_registry();
        auto it = reg.find(endpoint_name);
        if (it != reg.end()) {
            it->second = std::make_unique<PcieBypassMux>(ll);
            return it->second.get();
        }
        auto [new_it, _] = reg.emplace(endpoint_name, std::make_unique<PcieBypassMux>(ll));
        return new_it->second.get();
    }

    PcieBypassMux* PcieBypassMux::for_endpoint(const std::string& endpoint_name) noexcept {
        auto& reg = mux_registry();
        auto it = reg.find(endpoint_name);
        return (it != reg.end()) ? it->second.get() : nullptr;
    }

    void PcieBypassMux::detach_from_endpoint(const std::string& endpoint_name) noexcept {
        auto& reg = mux_registry();
        reg.erase(endpoint_name);
    }

    void PcieBypassMux::apply_mode(BypassMode new_mode, DrainPolicy policy) {
        drain_policy_ = policy;

        // C5: Partial 模式守卫移到最前 (步骤 6 → 步骤 0)
        // 先校验 phy_initialized 再清理状态，异常时零状态污染
        if (new_mode == BypassMode::Partial && !phy_initialized_) {
            throw std::logic_error("Partial mode requires PHY Digital Ctrl initialized");
        }

        // 0. 通知对端 (RC BFM / HostBypass) 准备切换
        notify_peer_mode_change(new_mode);

        // 1. 暂停所有 DLLP/TLP 传输
        pause_link_layer();

        // 2. 处理 in-flight TLP/AXI 事务 (Oracle Top-7 关键)
        if (drain_policy_ == DrainPolicy::GRACEFUL_DRAIN) {
            drain_in_flight(); // 等待完成 (有超时, 默认 1µs)
        } else {
            // IMMEDIATE_ABORT: 立即 abort + 记录中断通知
            aborted_tlps_ += in_flight_tlps_;
            in_flight_tlps_ = 0;
        }

        // 3. 清理 Retry Buffer (累积确认语义, 清到 ACK seq 而非全清)
        clear_retry_buffer_to_ack();

        // 4. 重置 seq# 计数器 (避免对端序列号失步)
        reset_seq_counters();

        // 5. 重置 FC Token Bucket (所有 VF/VC)
        reset_fc_buckets();

        // 6. (已前移) Partial 模式守卫

        // 7. MSI-X pending 状态清理 (Oracle Top-7 遗漏)
        clear_msix_pending();

        // 8. 提交新模式
        mode_ = new_mode;

        // 9. 通知对端切换完成 (对端可恢复传输)
        notify_peer_mode_complete(new_mode);

        // 10. 恢复传输
        resume_link_layer();
    }

    void PcieBypassMux::drain_in_flight() {
        // GRACEFUL_DRAIN: 等在途事务完成 (有超时, 默认 1µs)
        // TLM 侧无真实等待 → 模拟为立即完成 (in-flight 在切换窗口内全部落定)
        in_flight_tlps_ = 0;
    }

    void PcieBypassMux::clear_retry_buffer_to_ack() {
        // 清理 retry buffer: DRAIN 语义下 in-flight 已全部确认/丢弃
        // (累积确认语义: 清到 ACK seq; TLM 简化: 全清, 因为 DRAIN 保证已 ACK)
        if (link_layer_) {
            link_layer_->clear_retry_buffer();
        }
    }

    void PcieBypassMux::reset_seq_counters() {
        // seq# 计数器重置: 从 0 重新开始 (per design §7 步骤 4, 防对端失步)
        if (link_layer_) {
            link_layer_->reset_seq_counters();
        }
    }

    void PcieBypassMux::reset_fc_buckets() {
        // FC Token Bucket 重置: 恢复初始 credit (per design §7 步骤 5)
        if (link_layer_) {
            link_layer_->reset_fc_buckets();
        }
    }

    void PcieBypassMux::clear_msix_pending() {
        // MSI-X pending 状态清理 (per design §7 步骤 7)
        msix_pending_ = 0;
    }

    void PcieBypassMux::surprise_removal_cleanup() noexcept {
        // C5 (Q14): PRSNT# 移除 → drain/abort + 清 retry/seq/FC + MSI-X pending
        // 与 apply_mode 复用同一清理管线 (步骤 2/3/4/5/7), 但不提交模式切换
        aborted_tlps_ += in_flight_tlps_;
        in_flight_tlps_ = 0;
        if (link_layer_) {
            link_layer_->clear_retry_buffer();
            link_layer_->reset_seq_counters();
            link_layer_->reset_fc_buckets();
        }
        msix_pending_ = 0;
    }

} // namespace tlm::pcie

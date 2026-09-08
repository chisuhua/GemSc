// include/tlm/gpu/pcie_endpoint_tlm.h
// PcieEndpointTLM: SOC 片内 PCIe Endpoint IP 模型（4 端口 ChStream）
// 功能描述：dGPU SOC die 内的 PCIe slave 模型；接收 host→endpoint TLP，
//           路由 BAR0 寄存器 / BAR1 VRAM；MSI-X 中断投递到 irq_out
//           端口：slave_in / mmio_out / mem_out / irq_out (4 端口)
// 作者 CppTLM Team / 日期 2026-08-26
// 参考: openspec/changes/2026-08-26-cpptlm-dgpu-pcie-endpoint/design.md §3,§4
//       ADR-SOC-07 D2 (PCIe slave 归属 SOC)
#ifndef CPPTLM_PCIE_ENDPOINT_TLM_H
#define CPPTLM_PCIE_ENDPOINT_TLM_H

#include <cstdint>
#include <memory>
#include <string>
#include "bundles/pcie_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "core/sim_object.hh"
#include "framework/stream_adapter.hh"
#include "tlm/gpu/msix_table_mvp.hh"
#include "tlm/gpu/pcie_bar_router_mvp.hh"
#include "tlm/gpu/pcie_config_space_mvp.hh"
#include <nlohmann/json.hpp>

namespace tlm::gpu {

    /**
     * @brief PcieEndpointTLM：4 端口 ChStream PCIe Endpoint IP 模型
     *
     * 设计原则（per design.md §3,§4 + spec.md）：
     *   - 端口：
     *     0. slave_in  (ingress) → PcieTlpBundle  接收 host→endpoint TLP
     *     1. mmio_out  (egress)  → PcieTlpBundle  BAR0 解码 + 门铃副作用
     *     2. mem_out   (egress)  → PcieTlpBundle  BAR1 MEM 转发
     *     3. irq_out   (egress)  → PcieTlpBundle (IRQ_DELIVERY kind 传输 MsiXDeliveryBundle 语义)
     *
     *   - 子组件：PcieConfigSpace + PcieBarRouter + MsiXTable
     *   - 多端口 StreamAdapter 注入路径：set_stream_adapter(adapters[4])
     *     每个 adapter 类型擦除为 cpptlm::StreamAdapterBase*
     *
     * JSON 参数（per spec.md Scenario "JSON instantiation with multi-port adapter injection"）：
     *   {
     *     "config_size": 4096,                       // 256 或 4096
     *     "msix_num_vectors": 16,                    // 默认 16
     *     "bar0_registers": [                        // BAR0 寄存器表（数据化）
     *       {"offset": 0x0014, "name": "GPU_REG_DOORBELL", "access": "WO", "side_effect":
     * "doorbell", "stream_id": 0},
     *       ...
     *     ],
     *     "capabilities": [{"id": 17, "offset": 64, "next": 80, "control": 0}]
     *   }
     *
     * 强序写语义（per design.md §3 + spec.md "Doorbell register write is table-driven"）：
     *   BAR0 寄存器定义声明 side_effect="doorbell" 后，MMIO_WRITE 触发
     *   250-700ns 区间延迟的 mmio_out 门铃事务（沿用 s2 Doorbell 强序）。
     *   **禁止** C++ `if (offset == 0x0014)` 硬编码路径。
     */
    [[deprecated("use PcieEndpointIP instead; Phase 8 整合迁移")]] class PcieEndpointTLM
        : public ChStreamModuleBase {
    public:
        static constexpr unsigned NUM_PORTS = 4;

        // 端口索引常量（与 PcieTlpBundle 的 kind 枚举语义对应）
        static constexpr unsigned PORT_SLAVE_IN = 0;
        static constexpr unsigned PORT_MMIO_OUT = 1;
        static constexpr unsigned PORT_MEM_OUT = 2;
        static constexpr unsigned PORT_IRQ_OUT = 3;

        // 多端口 ChStream 端口（per MultiPortStreamAdapter 模板要求 public 访问）
        // 端口 0 = slave_in (req_in[0] 真正使用, resp_out[0] 未用)
        // 端口 1,2,3 = mmio_out/mem_out/irq_out (resp_out[1..3] 真正使用, req_in[1..3] 未用)
        cpptlm::InputStreamAdapter<bundles::PcieTlpBundle> req_in[NUM_PORTS];
        cpptlm::OutputStreamAdapter<bundles::PcieTlpBundle> resp_out[NUM_PORTS];

        PcieEndpointTLM(const std::string& name, EventQueue* eq);
        ~PcieEndpointTLM() override;

        PcieEndpointTLM(const PcieEndpointTLM&) = delete;
        PcieEndpointTLM& operator=(const PcieEndpointTLM&) = delete;
        PcieEndpointTLM(PcieEndpointTLM&&) = delete;
        PcieEndpointTLM& operator=(PcieEndpointTLM&&) = delete;

        std::string get_module_type() const override {
            return "PcieEndpointTLM";
        }

        // ChStreamModuleBase 接口（4 端口）
        void set_stream_adapter(cpptlm::StreamAdapterBase* a) override;
        void set_stream_adapter(cpptlm::StreamAdapterBase* adapters[]) override;
        unsigned num_ports() const override {
            return NUM_PORTS;
        }

        // 模块业务逻辑
        void init() override;
        void tick() override;
        void do_reset(const ResetConfig&) override;

        // JSON 参数注入（ModuleFactory::set_config 触发 on_config_loaded 调用）
        void on_config_loaded() override;

        // 测试/调试访问器
        PcieConfigSpace& config_space() {
            return *cfg_space_;
        }
        const PcieConfigSpace& config_space() const {
            return *cfg_space_;
        }
        PcieBarRouter& bar_router() {
            return *bar_router_;
        }
        const PcieBarRouter& bar_router() const {
            return *bar_router_;
        }
        MsiXTable& msix() {
            return *msix_;
        }
        const MsiXTable& msix() const {
            return *msix_;
        }

        // 测试断言 helper：4 个端口是否都接收到非空 adapter
        bool all_ports_have_adapter() const;

        // 单 adapter 访问（测试断言）
        cpptlm::StreamAdapterBase* get_adapter(unsigned idx) const {
            return (idx < NUM_PORTS) ? adapters_[idx] : nullptr;
        }

    private:
        // 子组件（per design.md §3）
        std::unique_ptr<PcieConfigSpace> cfg_space_;
        std::unique_ptr<PcieBarRouter> bar_router_;
        std::unique_ptr<MsiXTable> msix_;

        // 多端口 adapter 数组（per spec.md Scenario "All 4 ports receive non-null StreamAdapter"）
        cpptlm::StreamAdapterBase* adapters_[NUM_PORTS] = {nullptr};

        // 4 端口 Bundle 适配器已在 public 区声明（per MultiPortStreamAdapter 模板要求）

        void handle_slave_in_tlp();

        // 内部: BAR0 寄存器表/doorbell side-effect 配置 (从 JSON 加载)
        void apply_bar0_registers_config(const nlohmann::json& params);
        void apply_capabilities_config(const nlohmann::json& params);
    };

} // namespace tlm::gpu

#endif // CPPTLM_PCIE_ENDPOINT_TLM_H
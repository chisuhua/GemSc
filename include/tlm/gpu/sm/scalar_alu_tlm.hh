// include/tlm/gpu/sm/scalar_alu_tlm.hh
// ScalarALU: 标量 ALU 子模块 stub (per SM 微架构 §15.3.3)
// stub 仅注册/拓扑占位; 真值见 cpptlm::gpu::ScalarALU (scalar_alu.hh, Task 1.3)
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_SCALARALU_HH
#define TLM_GPU_SM_SCALARALU_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class ScalarALU : public ChStreamModuleBase {
public:
    explicit ScalarALU(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "ScalarALU"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_SCALARALU_HH

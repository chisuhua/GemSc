// include/tlm/gpu/sm/vector_alu_tlm.hh
// VectorALU: 向量 ALU 子模块 stub (per SM 微架构 §15.3.3)
// per Task 2.1 v2 P0-5 拆分, 保类名
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_VECTORALU_HH
#define TLM_GPU_SM_VECTORALU_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class VectorALU : public ChStreamModuleBase {
public:
    explicit VectorALU(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "VectorALU"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_VECTORALU_HH

// include/tlm/gpu/sm/matrix_core_tlm.hh
// MatrixCore: 矩阵核心子模块 stub (CDNA MFMA / NV Tensor Core)
// per Task 2.1 v2 P0-5 拆分, 保类名
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_MATRIXCORE_HH
#define TLM_GPU_SM_MATRIXCORE_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class MatrixCore : public ChStreamModuleBase {
public:
    explicit MatrixCore(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "MatrixCore"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_MATRIXCORE_HH

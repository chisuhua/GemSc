// include/tlm/gpu/sm/reg_file_unit_tlm.hh
// RegFileUnit: 寄存器文件子模块 stub (per SM 微架构 §15.5.6 SM-owns-state)
// per Task 2.1 v2 P0-5 拆分, 保类名
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_REGFILEUNIT_HH
#define TLM_GPU_SM_REGFILEUNIT_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class RegFileUnit : public ChStreamModuleBase {
public:
    explicit RegFileUnit(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "RegFileUnit"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_REGFILEUNIT_HH

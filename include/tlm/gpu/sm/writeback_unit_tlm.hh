// include/tlm/gpu/sm/writeback_unit_tlm.hh
// WritebackUnit: 写回单元 stub
// per Task 2.1 v2 P0-5 拆分, 保类名
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_WRITEBACKUNIT_HH
#define TLM_GPU_SM_WRITEBACKUNIT_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class WritebackUnit : public ChStreamModuleBase {
public:
    explicit WritebackUnit(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "WritebackUnit"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_WRITEBACKUNIT_HH

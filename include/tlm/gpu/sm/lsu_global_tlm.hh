// include/tlm/gpu/sm/lsu_global_tlm.hh
// LsuGlobal: 全局内存加载存储单元 stub
// per Task 2.1 v2 P0-5 拆分, 保类名
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分)
#ifndef TLM_GPU_SM_LSUGLOBAL_HH
#define TLM_GPU_SM_LSUGLOBAL_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm::sm {

class LsuGlobal : public ChStreamModuleBase {
public:
    explicit LsuGlobal(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {}
    std::string get_module_type() const override { return "LsuGlobal"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;
};

} // namespace tlm::sm

#endif // TLM_GPU_SM_LSUGLOBAL_HH

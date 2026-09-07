// include/tlm/gpu/sm/reg_file_unit_tlm.hh
// RegFileUnit: 寄存器文件子模块 stub (per SM 微架构 §15.5.6 SM-owns-state)
// per Task 2.1 v2 P0-5 拆分, 保类名 + parent_/set_parent (per Oracle Task 2.11 P-2)
//
// 功能 (per plan line 792 + Oracle F-1 P0 修复):
//   - tick() dispatch no-op (Task 2.11 范围: 接线准备; 真实 write-back 协调推迟 Task 2.12)
//   - parent_/set_parent 模式 (镜像 lsu_lds_tlm.hh, per Oracle F-2 P0)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-1 拆分 + P1-11 加 parent_)
#ifndef TLM_GPU_SM_REGFILEUNIT_HH
#define TLM_GPU_SM_REGFILEUNIT_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class RegFileUnit : public ChStreamModuleBase {
public:
    explicit RegFileUnit(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "RegFileUnit"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;

    // Task 2.11 P1-11: 注入 SM parent (per Oracle F-2 P0 修复模式, 镜像 lsu_lds_tlm.hh)
    void set_parent(::tlm::StreamingMultiprocessorTLM* p) { parent_ = p; }

private:
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_REGFILEUNIT_HH

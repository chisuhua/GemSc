// include/tlm/gpu/sm/writeback_unit_tlm.hh
// WritebackUnit: 写回单元子模块 stub (per SM 微架构 §15.3.3.10) - Task 2.12 端口接线
//
// 功能 (per plan line 793 + Oracle F-2 P0 修复, re-scoped to rf_-only):
//   - tick() 从 WritebackUnit truth 推进队列
//   - parent_/set_parent 模式 (镜像 reg_file_unit_tlm.hh, per Oracle Task 2.12 Q3)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-12 加 parent_, per Oracle 预审 Task 2.12)
#ifndef TLM_GPU_SM_WRITEBACKUNIT_HH
#define TLM_GPU_SM_WRITEBACKUNIT_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明

namespace sm {

class WritebackUnit : public ChStreamModuleBase {
public:
    WritebackUnit(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "WritebackUnit"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;

    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_WRITEBACKUNIT_HH

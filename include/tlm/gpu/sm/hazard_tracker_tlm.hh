// include/tlm/gpu/sm/hazard_tracker_tlm.hh
// HazardTracker: 冒险跟踪器子模块 stub (per SM 微架构 §15.5.6) - Task 2.13 端口接线
//
// 功能 (per plan line 801 + Oracle Task 2.13 Q3):
//   - tick() no-op dispatch (HT 由 producer/release 驱动, 不在 tick 内自调度)
//   - parent_/set_parent 模式 (镜像 reg_file_unit_tlm.hh)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-13 加 parent_, per Oracle 预审 Task 2.13)
#ifndef TLM_GPU_SM_HAZARDTRACKER_HH
#define TLM_GPU_SM_HAZARDTRACKER_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明

namespace sm {

class HazardTracker : public ChStreamModuleBase {
public:
    HazardTracker(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "HazardTracker"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;

    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_HAZARDTRACKER_HH

// src/tlm/gpu/sm/hazard_tracker_tlm.cc
// HazardTracker: tick() stub 接线 (Task 2.13 P1-13, per Oracle 预审 Task 2.13)
//
// stub tick() 行为:
//   - 调用 parent_->hazard_tracker()->tick() (镜像 writeback_unit_tlm.cc dispatch)
//   - HT 真值 tick() 当前 no-op (per Oracle Q4, 由 producer/release 驱动)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-13 端口接线)
#include "tlm/gpu/sm/hazard_tracker_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

HazardTracker::HazardTracker(const std::string& n, EventQueue* eq)
    : ChStreamModuleBase(n, eq), parent_(nullptr) {}

void HazardTracker::tick() {
    if (!parent_ || !parent_->hazard_tracker()) {
        return;
    }
    parent_->hazard_tracker()->tick();
}

} // namespace tlm::sm

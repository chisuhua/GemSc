// src/tlm/gpu/sm/writeback_unit_tlm.cc
// WritebackUnit: tick() 接线 (Task 2.12 P1-12, per Oracle 预审 Task 2.12)
//
// stub tick() 行为:
//   - 调用 parent_->writeback()->tick(current_cycle) (镜像 lsu_lds_tlm.cc dispatch 模式)
//   - 不消费 ring, 不做 pipe 判断 (per Oracle F-2 re-scope, WB 由 producer 经 truth enqueue)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-12 端口接线)
#include "tlm/gpu/sm/writeback_unit_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

WritebackUnit::WritebackUnit(const std::string& n, EventQueue* eq)
    : ChStreamModuleBase(n, eq), parent_(nullptr) {}

void WritebackUnit::tick() {
    if (!parent_ || !parent_->writeback()) {
        return;
    }
    // 推进 WB 队列: finish_cycle ≤ current_cycle 的条目写回 RF
    parent_->writeback()->tick(parent_->getCurrentCycle());
}

} // namespace tlm::sm

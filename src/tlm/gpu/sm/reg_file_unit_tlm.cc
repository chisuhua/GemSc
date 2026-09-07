// src/tlm/gpu/sm/reg_file_unit_tlm.cc
// RegFileUnit: tick() stub 实现 (Task 2.1 拆分, Task 2.11 加 parent_/set_parent)
//
// Task 2.11 范围: parent_/set_parent 接线准备, tick() no-op
//   - 真实 write-back 协调由 WritebackUnit (Task 2.12) 接管
//   - 当前 ring buffer 数据由 FetchUnit/DecodeUnit/IssueUnit 推进,
//     ALU/LSU 真值类经 parent_->set_scalar_reg() 直接写 RegFileUnit
//   - 因此 RegFileUnit stub tick() 在 Task 2.11 范围无需 dispatch
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-11 stub + parent_)
#include "tlm/gpu/sm/reg_file_unit_tlm.hh"

namespace tlm::sm {

RegFileUnit::RegFileUnit(const std::string& n, EventQueue* eq)
    : ChStreamModuleBase(n, eq), parent_(nullptr) {}

void RegFileUnit::tick() {
    // Task 2.11: no-op (parent_ 注入已就位, 真实 write-back 推迟 Task 2.12)
}

} // namespace tlm::sm

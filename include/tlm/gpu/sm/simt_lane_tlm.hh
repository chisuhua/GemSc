// include/tlm/gpu/sm/simt_lane_tlm.hh
// SIMTLane: SIMT 线程控制子模块 stub (per SM 微架构 §15.3.3.4) - Task 2.8 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q3 C 策略)
//   - 判 pipe == kSIMTLane → 调 parent_->simt_lane()->execute() (cpptlm::gpu::SIMTLane 真值)
//   - **不调 mark_completed** (mask 更新非指令完成, per Oracle Q4)
//   - pipe 判断只驻留 sl_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
//
// 架构定位:
//   - tlm::sm::SIMTLane stub (12 子模块之一, Task 2.1 拆分后) - 端口接线节点
//   - cpptlm::gpu::SIMTLane 真值类 (Task 2.8 真值) - dispatch 目标
//   - 关系: sl_ tick() 内部 dispatch 到 simt_lane_ (真值类), 类似 sa_/va_/mc_ 模式
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() → du_->tick() → iu_->tick() → sa_->tick() → va_->tick()
//                              → mc_->tick() → sl_->tick() → return 1
//   - sl_->tick() 内部 pipe 判断 + dispatch (exec 源保持 fu_->fetched().instr_desc 一致)
//
// SIMTLane 与 IssueUnitTLM 层级 (per Oracle Q10 P2):
//   - IssueUnitTLM = warp 调度 (Task 2.4 Round-robin)
//   - SIMTLane = warp 内 thread mask 控制 (Task 2.8)
//   - **禁止 sl_ 做 warp 调度** (避免与 iu_ 重复)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-8 端口接线, per Oracle 预审 Task 2.8)
#ifndef TLM_GPU_SM_SIMT_LANE_TLM_HH
#define TLM_GPU_SM_SIMT_LANE_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class SIMTLane : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:77)
    SIMTLane(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "SIMTLane"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.8 真值: dispatch 到 cpptlm::gpu::SIMTLane 真值

    // 设置 parent 指针 (per Oracle 预审 Task 2.8 F-2 P0 修复,
    // 镜像 Fetch/Decode/Issue/ScalarALU/VectorALU/MatrixCore set_parent 模式)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_SIMT_LANE_TLM_HH

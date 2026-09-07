// include/tlm/gpu/sm/lsu_global_tlm.hh
// LsuGlobal: 全局内存 LSU 子模块 stub (per SM 微架构 §15.3.3.5) - Task 2.9 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q5 末尾 dispatch)
//   - 判 pipe == kLsuGlobal + is_memory → 调 parent_->lsu_global()->execute() (enqueue)
//   - **不调 mark_completed** (异步完成在 lsu_global_ 真值类 tick() 归零回调, per Oracle Q4)
//   - pipe 判断只驻留 lg_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
//
// 架构定位:
//   - tlm::sm::LsuGlobal stub (12 子模块之一, Task 2.1 拆分后) - 端口接线节点
//   - cpptlm::gpu::LsuGlobal 真值类 (Task 2.9 异步骨架) - dispatch + 异步推进
//   - 关系: lg_ tick() dispatch 到 lsu_global_ (enqueue), lsu_global_ tick() 推进归零回调
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 链: lsu_global_->tick() (head, 无条件推进) → fu → du → iu → sa → va → mc
//                       → sl → lg (dispatch) → return 1
//   - lsu_global_->tick() 无条件在 head (per Oracle Q5 P0 修正, 异步不依赖 ring)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-9 端口接线, per Oracle 预审 Task 2.9)
#ifndef TLM_GPU_SM_LSU_GLOBAL_TLM_HH
#define TLM_GPU_SM_LSU_GLOBAL_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class LsuGlobal : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:78)
    LsuGlobal(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "LsuGlobal"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.9 真值: dispatch 到 cpptlm::gpu::LsuGlobal (enqueue)

    // 设置 parent 指针 (per Oracle 预审 Task 2.9 F-2 P0 修复,
    // 镜像 Fetch/Decode/Issue/ScalarALU/VectorALU/MatrixCore/SIMTLane set_parent 模式)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_LSU_GLOBAL_TLM_HH

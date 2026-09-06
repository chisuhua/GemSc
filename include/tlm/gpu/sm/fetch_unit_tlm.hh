// include/tlm/gpu/sm/fetch_unit_tlm.hh
// FetchUnitTLM: 取指单元 (per SM 微架构 §15.3.3) - Task 2.2 真值
//
// 功能:
//   - tick() 从 SM 顶层 ring buffer 取下一条 InstrDescriptor
//   - 写入 FetchToIssueBundle (per HSK-9 §3 + architecture/15 §15.5.6)
//   - 维护 fetched_valid_ 状态 (空 ring 时 has_fetched() == false)
//
// 架构定位:
//   - tlm::sm::FetchUnitTLM stub (12 子模块之一, 与 cpptlm::ScalarALU 等独立真值类并存)
//   - 构造签名 (name, EventQueue*, SM 顶层*) - parent 持 SM 顶层指针
//   - tick() 调用 parent_->fetch_next_instr() 访问 ring buffer (private 封装)
//   - 不同于 cpptlm::gpu::ScalarALU (独立真值类, 不继承 ChStreamModuleBase)
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() + fu_->fetched() + scalar_alu_->execute() + mark completed
//   - FetchUnitTLM 不直接调 ScalarALU (职责分离, 单向数据流)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-2 真值, per Oracle 预审 Task 2.2)
#ifndef TLM_GPU_SM_FETCHUNITTLM_HH
#define TLM_GPU_SM_FETCHUNITTLM_HH

#include "bundles/sm_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

// using directive: FetchToIssueBundle 在 bundles::sm:: namespace (sm_bundles_tlm.hh line 22)
using bundles::sm::FetchToIssueBundle;

namespace sm {

class FetchUnitTLM : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:80)
    FetchUnitTLM(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "FetchUnitTLM"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.2 真值: 从 ring 取下一条 + 写 FetchToIssueBundle

    // 设置 parent 指针 (per Oracle 预审 Task 2.2 F-2 P0: 12 子模块 unique_ptr 之前从未构造,
    // 现在 make_unique + set_parent(this) 桥接)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

    // 访问器 (per Oracle 预审 Task 2.2 Q8)
    const FetchToIssueBundle& fetched() const { return fetched_; }
    bool has_fetched() const { return fetched_valid_; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
    FetchToIssueBundle fetched_{};   // 上一次 tick() 取到的 bundle
    bool fetched_valid_ = false;     // ring 非空时 true (空 ring tick() 后 false)
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_FETCHUNITTLM_HH

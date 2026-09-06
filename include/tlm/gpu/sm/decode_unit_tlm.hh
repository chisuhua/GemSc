// include/tlm/gpu/sm/decode_unit_tlm.hh
// DecodeUnitTLM: 译码单元 (per SM 微架构 §15.3.3) - Task 2.3 真值
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor
//   - 提取 pipe + latency_class 字段, 写 DecodeToIssueBundle
//   - 维护 decoded_valid_ 状态 (无 fetch 时 has_decoded() == false)
//
// 架构定位:
//   - tlm::sm::DecodeUnitTLM stub (12 子模块之一, 与 cpptlm::ScalarALU 等独立真值类并存)
//   - 构造签名 (name, EventQueue*) - 与 module_factory 2 参数 lambda 兼容
//   - set_parent(SM 顶层*) 接口 - SM 构造后调 set_parent(this) 桥接
//   - tick() 读 parent_->fu()->fetched(), 不直接调 ScalarALU (单向数据流)
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() + du_->tick() + ScalarALU 真值 + mark completed
//   - DecodeUnitTLM 仅做字段提取, 不消费 ring buffer (consume 唯一入口: FetchUnitTLM)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-3 真值, per Oracle 预审 Task 2.3)
#ifndef TLM_GPU_SM_DECODEUNITTLM_HH
#define TLM_GPU_SM_DECODEUNITTLM_HH

#include "bundles/sm_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

// using directive: DecodeToIssueBundle 在 bundles::sm:: namespace (sm_bundles_tlm.hh:33-36)
using bundles::sm::DecodeToIssueBundle;

namespace sm {

class DecodeUnitTLM : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:80)
    DecodeUnitTLM(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "DecodeUnitTLM"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.3 真值: 从 fetched 提取 pipe/latency_class

    // 设置 parent 指针 (per Oracle 预审 Task 2.3 F-2 P0 修复)
    // SM 构造函数 make_unique 后调 set_parent(this) 注入
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

    // 访问器 (镜像 FetchUnitTLM 模式 per Oracle P1)
    const DecodeToIssueBundle& decoded() const { return decoded_; }
    bool has_decoded() const { return decoded_valid_; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
    DecodeToIssueBundle decoded_{};   // 上一次 tick() 提取的 bundle
    bool decoded_valid_ = false;      // 无 fetch 或 parent 缺失时 false
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_DECODEUNITTLM_HH

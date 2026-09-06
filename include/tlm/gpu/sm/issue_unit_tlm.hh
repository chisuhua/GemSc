// include/tlm/gpu/sm/issue_unit_tlm.hh
// IssueUnitTLM: 发射单元 (per SM 微架构 §15.3.3) - Task 2.4 真值
//
// 功能:
//   - tick() 从 DecodeUnitTLM.decoded() 读 DecodeToIssueBundle
//   - Round-robin 调度 warps: warp_id = (last_issued_warp_id + 1) % num_warps
//   - 维护 issued_valid_ 状态 (无 decoded 时 has_issued() == false)
//   - 维护 last_issued_warp_id_ 调度状态 (首 tick 调度到 warp 1)
//
// 架构定位:
//   - tlm::sm::IssueUnitTLM stub (12 子模块之一, 与 cpptlm::ScalarALU 等独立真值类并存)
//   - 构造签名 (name, EventQueue*) - 与 module_factory 2 参数 lambda 兼容
//   - set_parent(SM 顶层*) 接口 - SM 构造后调 set_parent(this) 桥接
//   - tick() 读 parent_->du()->decoded(), 不再 Decode, 不 consume ring
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() → du_->tick() → iu_->tick() → ScalarALU 真值 → mark completed
//   - IssueUnitTLM 仅做 Round-robin 调度, 不直接调 ScalarALU (单向数据流)
//   - IssueToExecBundle.src_values[] 由 PTX-EMU 上行同步 (F1.4 双计算决策), IssueUnit 不填
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-4 真值, per Oracle 预审 Task 2.4)
#ifndef TLM_GPU_SM_ISSUEUNITTLM_HH
#define TLM_GPU_SM_ISSUEUNITTLM_HH

#include "bundles/sm_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <cstdint>
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

// using directive: IssueToExecBundle 在 bundles::sm:: namespace (sm_bundles_tlm.hh:42-46)
using bundles::sm::IssueToExecBundle;

namespace sm {

class IssueUnitTLM : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:81)
    IssueUnitTLM(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "IssueUnitTLM"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.4 真值: Round-robin warp 调度

    // 设置 parent 指针 (per Oracle 预审 Task 2.4 F-2 P0 修复)
    // SM 构造函数 make_unique 后调 set_parent(this) 注入
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

    // 访问器 (镜像 FetchUnitTLM/DecodeUnitTLM 模式)
    const IssueToExecBundle& issued() const { return issued_; }
    bool has_issued() const { return issued_valid_; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
    IssueToExecBundle issued_{};        // 上一次 tick() 调度的 bundle
    bool issued_valid_ = false;         // 无 decoded 或 parent 缺失时 false
    // Round-robin 调度状态 (per Oracle 预审 Task 2.4 Q3/Q8)
    uint32_t last_issued_warp_id_ = 0; // 首 tick 调度到 warp 1 (约定)
    uint32_t num_warps_ = 4;            // per SM 微架构 §15.3.3 (后续 Task 透传 DeviceConfig.max_warps_per_sm 时改)
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_ISSUEUNITTLM_HH

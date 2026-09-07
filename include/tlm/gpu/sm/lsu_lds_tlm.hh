// include/tlm/gpu/sm/lsu_lds_tlm.hh
// LsuLDS: 共享内存 LSU 子模块 stub (per SM 微架构 §15.3.3.6) - Task 2.10 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q4 A 策略)
//   - 判 pipe == kLsuLDS + is_memory → 调 parent_->lsu_lds()->execute() (同步回写)
//   - pipe 判断只驻留 ll_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 链: lsu_global_->tick() (head) → fu → du → iu → sa → va → mc
//                       → sl → lg → ll → return 1
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-10 端口接线, per Oracle 预审 Task 2.10)
#ifndef TLM_GPU_SM_LSU_LDS_TLM_HH
#define TLM_GPU_SM_LSU_LDS_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class LsuLDS : public ChStreamModuleBase {
public:
    LsuLDS(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "LsuLDS"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;

    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_LSU_LDS_TLM_HH

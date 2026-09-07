// include/tlm/gpu/streaming_multiprocessor_tlm.hh
// StreamingMultiprocessorTLM: SM 顶层容器 (gpgpu-sim 风格 SM 微架构)
//
// 功能:
//   - 持有 12 个 ChStream 子模块 (per architecture/15 §15.2 + §15.3)
//   - 实现 IComputeDevice 15 方法 (per architecture/15 §15.5 + HSK-9 §3 + ADR-SOC-16 §2.3)
//   - 11 preserved 方法签名与 IPtxEmuDevice 逐字同构 (含 get_thread_state 返回 ThreadState)
//   - SM-owns-state 模式: RegFileUnit 持寄存器唯一真值源 (per architecture/15 §15.5.6)
//
// 实施状态:
//   - Task 4: SM 顶层 + IComputeDevice 15 方法 stub (本文件 + .cc)
//   - Task 5: 12 个子模块独立 .hh + 空 .cc (per architecture/15 §15.3.3)
//   - Task 7: 12 子模块完整实现 (连接 8 Bundle)
//   - Task 9: GpuComputeUnitTLM rename 合并入此
//   - Task 18: 完整实现 15 方法真实逻辑 (Tick + SM-owns-state 协议)
//
// 作者 CppTLM Team / 日期 2027-02-09 (SM 重构 Task 4 + Task 5 stub)
// 参考: docs/soc_arch/architecture/15-sm-microarchitecture-design.md §15.2 + §15.5
//       docs/soc_arch/adr/ADR-SOC-16-sm-microarchitecture.md
//       docs/soc_arch/adr/hsk9-announcement-draft.md §3
#ifndef TLM_GPU_STREAMING_MULTIPROCESSOR_TLM_HH
#define TLM_GPU_STREAMING_MULTIPROCESSOR_TLM_HH

#include "bundles/compute_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include "tlm/gpu/i_compute_device.hh"
#include "tlm/gpu/sm/fetch_unit_tlm.hh"        // Task 2.1 P1-1 拆分: 取指单元 stub
#include "tlm/gpu/sm/decode_unit_tlm.hh"       // Task 2.1 P1-1 拆分: 译码单元 stub
#include "tlm/gpu/sm/issue_unit_tlm.hh"        // Task 2.1 P1-1 拆分: 发射单元 stub
#include "tlm/gpu/sm/scalar_alu_tlm.hh"        // Task 2.1 P1-1 拆分: ScalarALU stub (NOT cpptlm::gpu::ScalarALU)
#include "tlm/gpu/sm/vector_alu_tlm.hh"        // Task 2.1 P1-1 拆分: 向量 ALU stub
#include "tlm/gpu/sm/matrix_core_tlm.hh"       // Task 2.1 P1-1 拆分: 矩阵核心 stub
#include "tlm/gpu/sm/simt_lane_tlm.hh"         // Task 2.1 P1-1 拆分: SIMT lane stub
#include "tlm/gpu/sm/lsu_global_tlm.hh"        // Task 2.1 P1-1 拆分: 全局内存 stub
#include "tlm/gpu/sm/lsu_lds_tlm.hh"           // Task 2.1 P1-1 拆分: 共享内存 stub
#include "tlm/gpu/sm/reg_file_unit_tlm.hh"     // Task 2.1 P1-1 拆分: 寄存器文件 stub (SM-owns-state §15.5.6)
#include "tlm/gpu/sm/writeback_unit_tlm.hh"    // Task 2.1 P1-1 拆分: 写回单元 stub
#include "tlm/gpu/sm/hazard_tracker_tlm.hh"    // Task 2.1 P1-1 拆分: 冒险跟踪器 stub
#include "tlm/gpu/sm/scalar_alu.hh" // Task 1.3 P1-3 ScalarALU 真值 (独立 cpptlm::gpu::ScalarALU)
#include "tlm/gpu/sm/vector_alu.hh" // Task 2.6 P1-6 VectorALU 真值 (独立 cpptlm::gpu::VectorALU)
#include "tlm/gpu/sm/matrix_alu.hh" // Task 2.7 P1-7 MatrixCore stub (cpptlm::gpu::MatrixCore 真值推迟 Task 4.6)
#include "tlm/gpu/sm/simt_lane.hh" // Task 2.8 P1-8 SIMTLane 真值 (EXEC mask 64-bit + 分歧检测)
#include "tlm/gpu/sm/lsu_global.hh" // Task 2.9 P1-9 LsuGlobal 真值 (异步内存回调骨架)
#include "tlm/gpu/sm/lsu_lds.hh" // Task 2.10 P1-10 LsuLDS 真值 (共享内存 bank conflict 检测 stub)
#include "tlm/gpu/sm/reg_file.hh" // Task 2.11 P1-11 RegFileUnit 真值 (取代 scalar_regs_, per Oracle F-1 P0 修复)
#include "tlm/gpu/sm/writeback.hh" // Task 2.12 P1-12 WritebackUnit 真值 (in-flight 队列 + per-warp 写回, per Oracle F-2 re-scope)

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tlm {

    class StreamingMultiprocessorTLM : public ChStreamModuleBase,
                                       public cpptlm::gpu::IComputeDevice {
    public:
        explicit StreamingMultiprocessorTLM(const std::string& name, EventQueue* eq);
        ~StreamingMultiprocessorTLM() override = default;

        std::string get_module_type() const override {
            return "StreamingMultiprocessorTLM";
        }
        void set_stream_adapter(cpptlm::StreamAdapterBase* adapter) override;

    // === SM 顶层 accessor (per Oracle 预审 Task 2.2 F-4) ===
    // fetch_next_instr: 取 ring front + consume (封装, instr_ring_ 保持 private)
    bool fetch_next_instr(cpptlm::gpu::InstrDescriptor& out) {
        if (ring_count_ == 0) return false;
        out = instr_ring_[ring_head_];
        ring_head_ = (ring_head_ + 1) % 64;
        --ring_count_;
        return true;
    }
    // fu(): 返回 FetchUnitTLM 指针 (per Oracle F-2 P0 修复, 12 子模块唯一构造入口)
    sm::FetchUnitTLM* fu() { return fu_.get(); }
    // du(): 返回 DecodeUnitTLM 指针 (per Oracle 预审 Task 2.3 F-4 P1)
    sm::DecodeUnitTLM* du() { return du_.get(); }
    // iu(): 返回 IssueUnitTLM 指针 (per Oracle 预审 Task 2.4 F-4 P1)
    sm::IssueUnitTLM* iu() { return iu_.get(); }
    // sa(): 返回 ScalarALU stub 指针 (per Oracle 预审 Task 2.5 Q6)
    sm::ScalarALU* sa() { return sa_.get(); }
    // scalar_alu(): 返回 cpptlm::gpu::ScalarALU 真值类指针 (per Oracle Q12 Q7)
    cpptlm::gpu::ScalarALU* scalar_alu() { return scalar_alu_.get(); }
    // va(): 返回 VectorALU stub 指针 (per Oracle 预审 Task 2.6 F-4 P1)
    sm::VectorALU* va() { return va_.get(); }
    // vector_alu(): 返回 cpptlm::gpu::VectorALU 真值类指针 (va_ tick dispatch 目标)
    cpptlm::gpu::VectorALU* vector_alu() { return vector_alu_.get(); }
    // mc(): 返回 MatrixCore stub 指针 (per Oracle 预审 Task 2.7 F-4 P1)
    sm::MatrixCore* mc() { return mc_.get(); }
    // matrix_alu(): 返回 cpptlm::gpu::MatrixCore 真值类指针 (mc_ tick dispatch 目标)
    cpptlm::gpu::MatrixCore* matrix_alu() { return matrix_alu_.get(); }
    // sl(): 返回 SIMTLane stub 指针 (per Oracle 预审 Task 2.8 F-4 P1)
    sm::SIMTLane* sl() { return sl_.get(); }
    // simt_lane(): 返回 cpptlm::gpu::SIMTLane 真值类指针 (sl_ tick dispatch 目标)
    cpptlm::gpu::SIMTLane* simt_lane() { return simt_lane_.get(); }
    // lg(): 返回 LsuGlobal stub 指针 (per Oracle 预审 Task 2.9 F-4 P1)
    sm::LsuGlobal* lg() { return lg_.get(); }
    // lsu_global(): 返回 cpptlm::gpu::LsuGlobal 真值类指针 (lg_ tick dispatch 目标)
    cpptlm::gpu::LsuGlobal* lsu_global() { return lsu_global_.get(); }
    // ll(): 返回 LsuLDS stub 指针 (per Oracle 预审 Task 2.10 Q6 命名修正 ll_, 镜像 lg()/sl()/mc() 模式)
    sm::LsuLDS* ll() { return ll_.get(); }
    // lsu_lds(): 返回 cpptlm::gpu::LsuLDS 真值类指针 (ll_ tick dispatch 目标)
    cpptlm::gpu::LsuLDS* lsu_lds() { return lsu_lds_.get(); }
    // rf(): 返回 RegFileUnit stub 指针 (per Oracle Task 2.11 P-2 命名, 镜像 ll()/lg() 模式)
    sm::RegFileUnit* rf() { return rf_.get(); }
    // reg_file(): 返回 cpptlm::gpu::RegFileUnit 真值类指针 (取代 scalar_regs_, per Oracle F-1 P0)
    cpptlm::gpu::RegFileUnit* reg_file() { return reg_file_.get(); }
    // wb(): 返回 WritebackUnit stub 指针 (per Oracle Task 2.12 Q3, 镜像 rf() 模式)
    sm::WritebackUnit* wb() { return wb_.get(); }
    // writeback(): 返回 cpptlm::gpu::WritebackUnit 真值类指针 (re-scoped rf-only, per Oracle F-2 P0)
    cpptlm::gpu::WritebackUnit* writeback() { return writeback_.get(); }
    // mark_completed(): G8 配套接口 (per Oracle Q12, sa_ tick() dispatch 完成后调)
    void mark_completed(uint64_t instr_id) {
        completed_instr_ids_.insert(instr_id);
    }
    // ring_count(): 返回当前 ring buffer 指令数 (per Oracle 测试断言)
    uint32_t ring_count() const { return ring_count_; }

        // === 4 端口访问器 (GPUTLM 范式, per include/tlm/gpu/gpu_tlm.hh:177-189) ===
        // StreamAdapter::tick() 契约要求 ModuleT 提供 req_out()/resp_in() (master 方向)
        // + req_in()/resp_out() (slave 方向), 全部 4 方向 (per plan Task 1.1 v2 P0-1 修订)
        cpptlm::OutputStreamAdapter<bundles::ComputeReqBundle>& req_out() {
            return req_out_;
        }
        cpptlm::InputStreamAdapter<bundles::ComputeRespBundle>& resp_in() {
            return resp_in_;
        }
        cpptlm::InputStreamAdapter<bundles::ComputeReqBundle>& req_in() {
            return req_in_;
        }
        cpptlm::OutputStreamAdapter<bundles::ComputeRespBundle>& resp_out() {
            return resp_out_;
        }

        // === get/set_scalar_reg (per Oracle P1-7, Task 1.3 依赖) ===
        // 真值源已迁移到 RegFileUnit (per Oracle F-1 P0 修复, Task 2.11)
        // facade 委托保留: set_scalar_reg → reg_file_->write(0, ...), get_scalar_reg → read(0, ...)
        // unset 语义保留: read 未设置返回 false → facade 返回 0 (per Oracle Q3)
        void set_scalar_reg(uint32_t reg_id, uint64_t value) {
            reg_file_->write(0, reg_id, value);
        }
        uint64_t get_scalar_reg(uint32_t reg_id) const {
            uint64_t v = 0;
            return reg_file_->read(0, reg_id, &v) ? v : 0;
        }

        // === IComputeDevice 15 方法 stub (Task 18 完整实现) ===
        // 11 preserved
        bool initialize(const cpptlm::gpu::DeviceConfig& cfg) override {
            (void)cfg;
            reg_file_->clear();
            ring_head_ = 0;
            ring_tail_ = 0;
            ring_count_ = 0;
            completed_instr_ids_.clear();
            return true;
        }
        void shutdown() override {
        }
        int exe_once() override {
            // Task 2.9 P1-9 (per Oracle 预审 Task 2.9 Q5 P0 修正):
            // 异步推进 lsu_global_ pending 队列 — 无条件在 head (异步不依赖 ring,
            // ring 空早退后 pending 仍需推进, 否则归零回调永不执行)
            lsu_global_->tick();            // 无条件推进 pending (异步不依赖 ring)
            // Task 2.12 P1-12 (per Oracle Task 2.12 P-1): WB tick 在链头, 与 lsu_global_ 同处理
            // (ring 空时 WB 队列仍需推进, 否则 drain 永不执行)
            wb_->tick();                    // 无条件推进 WB 队列 (in-flight 写回 RF)
            // Task 2.2 P1-2 (per Oracle 预审 Task 2.2 F-1 P0 修复):
            // 取指/consume 下沉到 FetchUnitTLM.tick() (消除双消费者 bug)
            if (ring_count_ == 0)
                return 0;
            fu_->tick();  // 取指 + consume (Task 2.2 P1-2, 封装 via fetch_next_instr accessor)
            // Task 2.3 P1-3: Decode 真值 (per Oracle Q4 A 策略, pipeline 推进 1 步)
            // 保持 auto d = fu_->fetched().instr_desc; 不动 (不改用 du_->decoded(), 纯拷贝零行为变化)
            du_->tick();  // 字段提取 (pipe + latency_class), 不 consume ring
            // Task 2.4 P1-4: Issue 真值 (per Oracle Q4 A 策略, pipeline 推进 1 步)
            // Round-robin warp 调度, 仍读 decoded (已 Decode, 不再 Decode, 不 consume ring)
            iu_->tick();  // 调度 warp_id (1→2→3→0 wrap-around), 继承字段透传
            // Task 2.5 P1-5: 端口接线 — sa_ tick() 内部 pipe 判断 + dispatch 到 scalar_alu_ 真值
            // (per Oracle Q12: pipe 判断只驻留 sa_, exe_once 不得保留避免双 dispatch, Task 2.2 F-1 教训)
            sa_->tick();
            // Task 2.6 P1-6: 端口接线 — va_ tick() 内部 pipe 判断 + dispatch 到 vector_alu_ 真值
            // (per Oracle Q12 Q3 C 推荐: sa/va 并列, 各 pipe 判断, 读同一 fu()->fetched())
            va_->tick();
            // Task 2.7 P1-7: 端口接线 stub — mc_ tick() 内部 pipe 判断 + dispatch 到 matrix_alu_ stub
            // (per Oracle Q12 Q3 C 推荐: sa/va/mc 并列, pipe 互斥, stub 阶段不标 completed 避免假完成)
            mc_->tick();
            // Task 2.8 P1-8: 端口接线 — sl_ tick() 内部 pipe 判断 + dispatch 到 simt_lane_ 真值
            // (per Oracle Q12 Q3 C 推荐: sa/va/mc/sl 并列, pipe 互斥, stub 阶段不标 completed 避免假完成)
            sl_->tick();
            // Task 2.9 P1-9: 端口接线 — lg_ tick() 内部 pipe 判断 + dispatch 到 lsu_global_ 真值
            // (per Oracle Q5 末尾 dispatch: lg 判断 pipe + is_memory → lsu_global_->execute() enqueue)
            lg_->tick();
            // Task 2.10 P1-10: 端口接线 — ll_ tick() 内部 pipe 判断 + dispatch 到 lsu_lds_ 真值
            // (per Oracle Q4 A 推荐: lg → ll 顺序, LDS 共享内存同步语义, 对照 Global 异步)
            ll_->tick();
            // Task 2.11 P1-11: rf_ tick() 接线准备 (per Oracle P-2, 真值写回由 WritebackUnit Task 2.12 接管)
            // 当前 stub tick() no-op; 真实 write-back 协调推迟 Task 2.12
            rf_->tick();
            return 1;
        }
        int sm_exe_once(uint32_t sm_id) override {
            (void)sm_id;
            return 0;
        }
        int warp_exe_once(uint32_t sm_id, uint32_t warp_id) override {
            (void)sm_id;
            (void)warp_id;
            return 0;
        }
        bool set_scoreboard(uint32_t sm_id, uint32_t warp_id, uint64_t mask) override {
            (void)sm_id;
            (void)warp_id;
            (void)mask;
            return false;
        }
        cpptlm::gpu::ThreadState get_thread_state(uint32_t sm_id, uint32_t warp_id,
                                                  uint32_t lane_id) override {
            (void)sm_id;
            (void)warp_id;
            (void)lane_id;
            return cpptlm::gpu::ThreadState::kIdle;
        }
        bool set_active_mask(uint32_t sm_id, uint32_t warp_id, uint64_t mask) override {
            (void)sm_id;
            (void)warp_id;
            (void)mask;
            return false;
        }
        bool set_next_pc(uint32_t sm_id, uint32_t warp_id, uint32_t lane_id, uint32_t pc) override {
            (void)sm_id;
            (void)warp_id;
            (void)lane_id;
            (void)pc;
            return false;
        }
        cpptlm::gpu::WarpStatus get_warp_status(uint32_t sm_id, uint32_t warp_id) override {
            (void)sm_id;
            (void)warp_id;
            return {};
        }
        bool is_finished() override {
            return false;
        }
        // 1 HSK-9 new
        void set_instr_descriptor_buf(const cpptlm::gpu::InstrDescriptor* buf,
                                      uint32_t count) override {
            // Task 1.5 P1-5: ring buffer (固定 64, 满时覆盖最旧 head, per plan)
            if (!buf || count == 0 || count > 64)
                return;
            for (uint32_t i = 0; i < count; ++i) {
                instr_ring_[ring_tail_] = buf[i]; // 浅拷贝 POD copy (per HSK-9 §3 buf 内存所有权)
                ring_tail_ = (ring_tail_ + 1) % 64;
                if (ring_count_ == 64) {
                    ring_head_ = (ring_head_ + 1) % 64; // 覆盖最旧
                } else {
                    ++ring_count_;
                }
            }
        }
        // 2 Round 4 new
        bool get_register_value(uint32_t sm_id, uint32_t warp_id, uint32_t reg_id,
                                uint64_t* out_value, uint32_t lane_id = 0xFFFFFFFF) override {
            (void)sm_id;
            (void)lane_id;
            if (!out_value)
                return false;
            // 委托到 RegFileUnit 真值 (per Oracle F-1 P0 修复, per-warp key via warp_id)
            return reg_file_->read(warp_id, reg_id, out_value);
        }
        bool is_instruction_completed(uint64_t instr_id) override {
            // Task 1.4 P1-4: 已完成 instr_id 集合 (per HSK-9 §3 协议)
            return completed_instr_ids_.count(instr_id) > 0;
        }
        // 1 reset
        // Per Oracle P0-2 Task 8 review: IComputeDevice::reset() 与 SimObject::reset(const
        // ResetConfig&) 名字遮蔽. 桥接方案: IComputeDevice::reset() 委托给框架 do_reset(), 并 using
        // SimObject::reset 恢复框架签名.
        void reset() override {
            do_reset({});
        }
        using ChStreamModuleBase::reset; // 恢复框架 reset(const ResetConfig&) 重载可见性

        // === ChStreamModuleBase tick() ===
        void tick() override;

    private:
        // 12 子模块 (per architecture/15 §15.2)
        std::unique_ptr<sm::FetchUnitTLM> fu_;
        std::unique_ptr<sm::DecodeUnitTLM> du_;
        std::unique_ptr<sm::IssueUnitTLM> iu_;
        std::unique_ptr<sm::ScalarALU> sa_;
        std::unique_ptr<sm::VectorALU> va_;
        std::unique_ptr<sm::MatrixCore> mc_;
        std::unique_ptr<sm::SIMTLane> sl_;
        std::unique_ptr<sm::LsuGlobal> lg_;
        std::unique_ptr<sm::LsuLDS> ll_;
        std::unique_ptr<sm::RegFileUnit> rf_;
        std::unique_ptr<sm::WritebackUnit> wb_;
        std::unique_ptr<sm::HazardTracker> ht_;

        // === 4 适配器成员 (GPUTLM 范式, per include/tlm/gpu/gpu_tlm.hh:28-29) ===
        cpptlm::OutputStreamAdapter<bundles::ComputeReqBundle> req_out_;
        cpptlm::InputStreamAdapter<bundles::ComputeRespBundle> resp_in_;
        cpptlm::InputStreamAdapter<bundles::ComputeReqBundle> req_in_;
        cpptlm::OutputStreamAdapter<bundles::ComputeRespBundle> resp_out_;

        // === scalar_regs_ 已删除 (per Oracle F-1 P0 修复, Task 2.11) ===
        // 真值源迁至 reg_file_ (cpptlm::gpu::RegFileUnit), per-warp flat key
        // facade set_scalar_reg/get_scalar_reg/get_register_value 委托 reg_file_

        // === Task 1.3 P1-3 ScalarALU 真值 (per plan) ===
        // cpptlm::gpu::ScalarALU 独立真值类 (include/tlm/gpu/sm/scalar_alu.hh),
        // SM 顶层持 unique_ptr, exe_once() 调用其 execute() 处理 ring buffer 中 kScalarALU 指令
        std::unique_ptr<cpptlm::gpu::ScalarALU> scalar_alu_;
        // === Task 2.6 P1-6 VectorALU 真值 (per plan, 镜像 ScalarALU 模式) ===
        // cpptlm::gpu::VectorALU 独立真值类 (include/tlm/gpu/sm/vector_alu.hh),
        // va_ tick() 内部 pipe 判断 + dispatch 到 vector_alu_->execute() 处理 kVectorALU 指令
        std::unique_ptr<cpptlm::gpu::VectorALU> vector_alu_;
        // === Task 2.7 P1-7 MatrixCore stub (per plan, 真值推迟 Task 4.6) ===
        // cpptlm::gpu::MatrixCore stub 真值类 (include/tlm/gpu/sm/matrix_alu.hh),
        // mc_ tick() 内部 pipe 判断 + dispatch 到 matrix_alu_->execute() stub (return 0)
        std::unique_ptr<cpptlm::gpu::MatrixCore> matrix_alu_;
        // === Task 2.8 P1-8 SIMTLane 真值 (per plan, EXEC mask 64-bit + 分歧检测) ===
        // cpptlm::gpu::SIMTLane 真值类 (include/tlm/gpu/sm/simt_lane.hh),
        // sl_ tick() 内部 pipe 判断 + dispatch 到 simt_lane_->execute() 更新 EXEC mask
        std::unique_ptr<cpptlm::gpu::SIMTLane> simt_lane_;
        // === Task 2.9 P1-9 LsuGlobal 真值 (per plan, 异步内存回调骨架) ===
        // cpptlm::gpu::LsuGlobal 真值类 (include/tlm/gpu/sm/lsu_global.hh),
        // lg_ tick() dispatch → lsu_global_->execute() enqueue; lsu_global_->tick() head 推进归零回调
        std::unique_ptr<cpptlm::gpu::LsuGlobal> lsu_global_;
        // === Task 2.10 P1-10 LsuLDS 真值 (per plan, 共享内存 bank conflict 检测 stub) ===
        // cpptlm::gpu::LsuLDS 真值类 (include/tlm/gpu/sm/lsu_lds.hh),
        // ll_ tick() dispatch → lsu_lds_->execute() 同步回写 (对照 LsuGlobal 异步)
        std::unique_ptr<cpptlm::gpu::LsuLDS> lsu_lds_;
        // === Task 2.11 P1-11 RegFileUnit 真值 (per plan, 取代 scalar_regs_, per Oracle F-1 P0) ===
        // cpptlm::gpu::RegFileUnit 真值类 (include/tlm/gpu/sm/reg_file.hh),
        // SM-owns-state: 持寄存器唯一真值源 (per architecture/15 §15.5.6)
        // facade 委托保留 (set_scalar_reg/get_scalar_reg/get_register_value/initialize 全部走 reg_file_)
        std::unique_ptr<cpptlm::gpu::RegFileUnit> reg_file_;
        // === Task 2.12 P1-12 WritebackUnit 真值 (per plan, in-flight 队列, per Oracle F-2 re-scope) ===
        // cpptlm::gpu::WritebackUnit 真值类 (include/tlm/gpu/sm/writeback.hh),
        // wb_ tick() dispatch → writeback_->tick(current_cycle) 推进 in-flight 队列 + 写回 RF
        // re-scoped to rf-only (HT-release 推迟 Task 2.13, per Oracle F-2 P0)
        std::unique_ptr<cpptlm::gpu::WritebackUnit> writeback_;

        // Task 1.5 P1-5: instr_descriptor ring buffer (固定大小 64, 覆盖最旧)
        // 浅拷贝 PTX-EMU 注入的 InstrDescriptor buf (per HSK-9 §3 buf 内存所有权语义)
        std::array<cpptlm::gpu::InstrDescriptor, 64> instr_ring_{};
        uint32_t ring_head_ = 0;  // 最早未处理 desc 位置 (consume 位置)
        uint32_t ring_tail_ = 0;  // 下一个写入位置 (produce 位置)
        uint32_t ring_count_ = 0; // 当前未处理 desc 数 (满时 = 64, 覆盖 head)
        // Task 1.4 P1-4: 已完成 instr_id 集合 (ScalarALU.execute 后 insert,
        // is_instruction_completed 查)
        std::unordered_set<uint64_t> completed_instr_ids_;

        cpptlm::StreamAdapterBase* adapter_ = nullptr;
    };

} // namespace tlm

#endif
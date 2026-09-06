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
        // interim 真值源, Task 2.11 须迁移到 RegFileUnit. 见 plan Task 1.1 Step 3.
        void set_scalar_reg(uint32_t reg_id, uint64_t value) {
            scalar_regs_[reg_id] = value;
        }
        uint64_t get_scalar_reg(uint32_t reg_id) const {
            auto it = scalar_regs_.find(reg_id);
            return it != scalar_regs_.end() ? it->second : 0;
        }

        // === IComputeDevice 15 方法 stub (Task 18 完整实现) ===
        // 11 preserved
        bool initialize(const cpptlm::gpu::DeviceConfig& cfg) override {
            (void)cfg;
            scalar_regs_.clear();
            ring_head_ = 0;
            ring_tail_ = 0;
            ring_count_ = 0;
            completed_instr_ids_.clear();
            return true;
        }
        void shutdown() override {
        }
        int exe_once() override {
            // Task 2.2 P1-2 (per Oracle 预审 Task 2.2 F-1 P0 修复):
            // 取指/consume 下沉到 FetchUnitTLM.tick() (消除双消费者 bug)
            // 行为等价: 1 cycle consume 一条, ScalarALU 真值仍调, completed_instr_ids_ 仍标记
            if (ring_count_ == 0)
                return 0;
            fu_->tick();  // 取指 + consume (Task 2.2 P1-2, 封装 via fetch_next_instr accessor)
            // Task 2.3 P1-3: Decode 真值 (per Oracle Q4 A 策略, pipeline 推进 1 步)
            // 保持 auto d = fu_->fetched().instr_desc; 不动 (不改用 du_->decoded(), 纯拷贝零行为变化)
            du_->tick();  // 字段提取 (pipe + latency_class), 不 consume ring
            // Task 2.2 P1-2: copy fetched_.instr_desc (per HSK-9 §3 buf 内存所有权: PTX-EMU 持有,
            // SM 仅在调用期间浅拷贝; ScalarALU::execute 接受 non-const ref 需 copy)
            auto d = fu_->fetched().instr_desc;
            if (d.pipe == cpptlm::gpu::PipeClass::kScalarALU) {
                scalar_alu_->execute(d);
                // Task 1.4 P1-4: ScalarALU 完成后, 标记 instr_id 已完成
                completed_instr_ids_.insert(d.instr_id);
            }
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
            (void)warp_id;
            (void)lane_id;
            if (!out_value)
                return false;
            auto it = scalar_regs_.find(reg_id);
            if (it == scalar_regs_.end())
                return false;
            *out_value = it->second;
            return true;
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

        // === scalar_regs_ (per Oracle P1-7 Task 1.3 依赖, interim 真值源) ===
        // Task 2.11 须迁移到 RegFileUnit
        std::unordered_map<uint32_t, uint64_t> scalar_regs_;

        // === Task 1.3 P1-3 ScalarALU 真值 (per plan) ===
        // cpptlm::gpu::ScalarALU 独立真值类 (include/tlm/gpu/sm/scalar_alu.hh),
        // SM 顶层持 unique_ptr, exe_once() 调用其 execute() 处理 ring buffer 中 kScalarALU 指令
        std::unique_ptr<cpptlm::gpu::ScalarALU> scalar_alu_;
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
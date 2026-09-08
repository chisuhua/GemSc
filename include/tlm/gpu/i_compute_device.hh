// include/tlm/gpu/i_compute_device.hh
// IComputeDevice: CppTLM 端 SM 微架构抽象接口 (per architecture/15 §15.5 + ADR-SOC-16 §2.3 + HSK-9
// §3)
//
// 功能:
//   - 15 个纯虚方法 = 11 preserved from IPtxEmuDevice + 1 new (HSK-9 同步通道) + 2 new (Round 4) +
//   1 reset
//   - 11 preserved 方法签名与 IPtxEmuDevice 逐字同构 (含 get_thread_state 返回 ThreadState per
//   device_api.h:104)
//   - attach_timing 不在此接口 (per HSK-9 F3.1, 保留为 IPtxEmuDevice deprecated stub)
//
// 协议语义 (per architecture/15 §15.5.6 + Oracle Round 4 F1.4):
//   - tick 驱动方: PTX-EMU 在 set_instr_descriptor_buf 后必须调用 exe_once() 推进 SM cycle
//   - 末批指令结果取回: kernel 最后一批指令无后续 set_instr_descriptor_buf 调用,
//                       PTX-EMU 必须调 is_finished() + 轮询 get_register_value() 取回剩余寄存器真值
//   - buf 内存所有权: PTX-EMU 持有, SM 仅在调用期间浅拷贝; PTX-EMU 可在调用返回后立即复用/释放
//   - get_register_value lane_id 默认 0xFFFFFFFF 语义: 表示 warp 所有 lane 寄存器值相同, 返回 lane
//   0 的值
//   - is_instruction_completed 轮询语义: PTX-EMU spin 直到返回 true
//
// 作者 CppTLM Team / 日期 2027-02-09 (SM 重构 Task 4)
// 参考: docs/soc_arch/architecture/15-sm-microarchitecture-design.md §15.5
//       docs/soc_arch/adr/ADR-SOC-16-sm-microarchitecture.md §2.3
//       docs/soc_arch/adr/hsk9-announcement-draft.md §3
//       external/PTX-EMU/include/ptxemu/device_api.h (IPtxEmuDevice 同构)
#ifndef TLM_GPU_I_COMPUTE_DEVICE_HH
#define TLM_GPU_I_COMPUTE_DEVICE_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

// ICOMPUTE_API_VERSION: CppTLM 端 IComputeDevice 契约版本号 (per HSK-9 OpenSpec change
// hsk9-icompute-api-v1-consumer-pinning Tasks 1.1-1.2). 镜像 PTX-EMU 端 PTXEMU_API_VERSION
// 位置 (全局宏, 非 constexpr, 便于跨仓 static_assert; per contract spec Reg 1 + Oracle R4 Fix 10).
#define ICOMPUTE_API_VERSION 1

static_assert(ICOMPUTE_API_VERSION == 1, "IComputeDevice contract version mismatch");

namespace cpptlm {
    namespace gpu {

        // === DeviceConfig (per architecture/15 §15.5) ===
        struct DeviceConfig {
            uint32_t num_sms = 1;
            uint32_t max_warps_per_sm = 64;
            uint32_t max_threads_per_sm = 2048;
            std::size_t shared_mem_size_per_sm = 48 * 1024;
        };

        // === ThreadState (per IPtxEmuDevice::ThreadState device_api.h:51-62) ===
        // Re-exported here for CppTLM-side convenience; same enum values.
        enum class ThreadState : uint32_t {
            kIdle = 0,
            kRun = 1,
            kExit = 2,
            kBarSync = 3,
        };

        // === WarpStatus (per IPtxEmuDevice::WarpStatus device_api.h:65-77) ===
        struct LaneStatus {
            uint32_t lane_id = 0;
            ThreadState state = ThreadState::kIdle;
            uint32_t pc = 0;
        };

        struct WarpStatus {
            uint32_t warp_id = 0;
            uint32_t sm_id = 0;
            std::vector<LaneStatus> lanes;
            uint32_t active_count = 0;
            int32_t blocked_cycles = 0;
        };

        // === IComputeDevice 接口 (15 个纯虚方法) ===
        class IComputeDevice {
        public:
            virtual ~IComputeDevice() = default;

            // === 11 preserved from IPtxEmuDevice (签名同构 per device_api.h:85-114) ===
            virtual bool initialize(const DeviceConfig& cfg) = 0;
            virtual void shutdown() = 0;
            virtual int exe_once() = 0;
            virtual int sm_exe_once(uint32_t sm_id) = 0;
            virtual int warp_exe_once(uint32_t sm_id, uint32_t warp_id) = 0;
            virtual bool set_scoreboard(uint32_t sm_id, uint32_t warp_id, uint64_t mask) = 0;
            virtual ThreadState get_thread_state(uint32_t sm_id, uint32_t warp_id,
                                                 uint32_t lane_id) = 0;
            virtual bool set_active_mask(uint32_t sm_id, uint32_t warp_id, uint64_t mask) = 0;
            virtual bool set_next_pc(uint32_t sm_id, uint32_t warp_id, uint32_t lane_id,
                                     uint32_t pc) = 0;
            virtual WarpStatus get_warp_status(uint32_t sm_id, uint32_t warp_id) = 0;
            virtual bool is_finished() = 0;

            // === 1 new: HSK-9 同步通道 (PTX-EMU 上行注入已解码 InstrDescriptor[]) ===
            virtual void set_instr_descriptor_buf(const InstrDescriptor* buf, uint32_t count) = 0;

            // === 2 new (Round 4 user decisions) ===
            virtual bool get_register_value(uint32_t sm_id, uint32_t warp_id, uint32_t reg_id,
                                            uint64_t* out_value, uint32_t lane_id = 0xFFFFFFFF) = 0;
            virtual bool is_instruction_completed(uint64_t instr_id) = 0;

            // === 1 reset (SM 顶层全局状态清零) ===
            virtual void reset() = 0;
        };

        // 15 method-signature static_asserts (per HSK-9 §3 + ADR-SOC-16 §2.3 + Oracle Round 4 Fix
        // 5) 任何方法签名变更 (参数类型/返回类型/const 性) 必须 bump ICOMPUTE_API_VERSION,
        // 否则编译失败 C++ 无惯用表达式对虚方法计数, 逐方法签名断言是实际可行机制 (Fix 5)
        static_assert(std::is_same_v<decltype(&IComputeDevice::initialize),
                                     bool (IComputeDevice::*)(const DeviceConfig&)>,
                      "IComputeDevice::initialize signature (HSK-9 §3 method 1/15)");
        static_assert(
            std::is_same_v<decltype(&IComputeDevice::shutdown), void (IComputeDevice::*)()>,
            "IComputeDevice::shutdown signature (HSK-9 §3 method 2/15)");
        static_assert(
            std::is_same_v<decltype(&IComputeDevice::exe_once), int (IComputeDevice::*)()>,
            "IComputeDevice::exe_once signature (HSK-9 §3 method 3/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::sm_exe_once),
                                     int (IComputeDevice::*)(uint32_t)>,
                      "IComputeDevice::sm_exe_once signature (HSK-9 §3 method 4/15, 1-param per "
                      "i_compute_device.hh:74)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::warp_exe_once),
                                     int (IComputeDevice::*)(uint32_t, uint32_t)>,
                      "IComputeDevice::warp_exe_once signature (HSK-9 §3 method 5/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::set_scoreboard),
                                     bool (IComputeDevice::*)(uint32_t, uint32_t, uint64_t)>,
                      "IComputeDevice::set_scoreboard signature (HSK-9 §3 method 6/15)");
        static_assert(
            std::is_same_v<decltype(&IComputeDevice::get_thread_state),
                           cpptlm::gpu::ThreadState (IComputeDevice::*)(uint32_t, uint32_t,
                                                                        uint32_t)>,
            "IComputeDevice::get_thread_state returns ThreadState NOT int (HSK-9 §3 method 7/15 + "
            "archive cpptlm-dgpu-d1-cdna-isa-sm-rewrite Task 3.5 P0 prevention)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::set_active_mask),
                                     bool (IComputeDevice::*)(uint32_t, uint32_t, uint64_t)>,
                      "IComputeDevice::set_active_mask signature (HSK-9 §3 method 8/15)");
        static_assert(
            std::is_same_v<decltype(&IComputeDevice::set_next_pc),
                           bool (IComputeDevice::*)(uint32_t, uint32_t, uint32_t, uint32_t)>,
            "IComputeDevice::set_next_pc signature (HSK-9 §3 method 9/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::get_warp_status),
                                     WarpStatus (IComputeDevice::*)(uint32_t, uint32_t)>,
                      "IComputeDevice::get_warp_status signature (HSK-9 §3 method 10/15)");
        static_assert(
            std::is_same_v<decltype(&IComputeDevice::is_finished), bool (IComputeDevice::*)()>,
            "IComputeDevice::is_finished signature (HSK-9 §3 method 11/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::set_instr_descriptor_buf),
                                     void (IComputeDevice::*)(const InstrDescriptor*, uint32_t)>,
                      "IComputeDevice::set_instr_descriptor_buf signature (HSK-9 §3 method 12/15, "
                      "const InstrDescriptor* per i_compute_device.hh:84)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::get_register_value),
                                     bool (IComputeDevice::*)(uint32_t, uint32_t, uint32_t,
                                                              uint64_t*, uint32_t)>,
                      "IComputeDevice::get_register_value signature (HSK-9 §3 method 13/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::is_instruction_completed),
                                     bool (IComputeDevice::*)(uint64_t)>,
                      "IComputeDevice::is_instruction_completed signature (HSK-9 §3 method 14/15)");
        static_assert(std::is_same_v<decltype(&IComputeDevice::reset), void (IComputeDevice::*)()>,
                      "IComputeDevice::reset signature (HSK-9 §3 method 15/15)");

        // Task 1.4 额外明示: get_thread_state 必须返回 ThreadState 而非 int
        // (防 archive cpptlm-dgpu-d1-cdna-isa-sm-rewrite Task 3.5 P0 重现; Oracle Round 4 Fix 1)
        static_assert(
            std::is_same_v<decltype(std::declval<IComputeDevice>().get_thread_state(0, 0, 0)),
                           cpptlm::gpu::ThreadState>,
            "get_thread_state must return ThreadState, not int (HSK-9 §3 + archive P0)");

    } // namespace gpu
} // namespace cpptlm

#endif
// include/tlm/gpu/sm/hazard_tracker.hh
// HazardTracker: 冒险跟踪器真值类 (per SM 微架构 §15.5.6)
//
// 功能 (per plan line 801-854 + Oracle F-2 P0 修正, plan v3 修订):
//   - increment_vmcnt(warp, vgpr) / decrement_vmcnt(warp, vgpr) / vmcnt(warp, vgpr)
//   - is_stalled_vmcnt(warp, vgpr, N): true if vmcnt > N
//   - can_allocate(warp, vgpr) / allocate(warp, vgpr) / release(warp, vgpr): kVirtualReg RAW
//   - tick(): no-op placeholder (per Oracle Q4, HT 由 producer/release 驱动, 不自调度)
//
// 架构定位 (per Oracle F-2 P0 + Q7):
//   - 构造签名 (parent) — 镜像 LsuLDS/RegFileUnit/WritebackUnit, 不需要 (name, EventQueue*)
//   - 两张容器: vmcnts_ map (kHardwareCounter) + allocated_vregs_ set (kVirtualReg)
//   - 同 key codec: (warp_id<<32) | idx (per Oracle P-2, 镜像 RegFileUnit)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18 L5 真值, per Oracle 预审 Task 2.13)
#ifndef TLM_GPU_SM_HAZARD_TRACKER_HH
#define TLM_GPU_SM_HAZARD_TRACKER_HH

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明

}

namespace cpptlm {
namespace gpu {

class HazardTracker {
public:
    explicit HazardTracker(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // === kHardwareCounter (vmcnt) 接口 ===
    // increment_vmcnt: 发送 memory load 后 vmcnt++
    void increment_vmcnt(uint32_t warp, uint32_t vgpr);
    // decrement_vmcnt: memory 完成后 vmcnt-- (per plan v3 修订: 通常 ×2 触发完成)
    void decrement_vmcnt(uint32_t warp, uint32_t vgpr);
    // vmcnt: 查询当前计数 (per plan 测试断言)
    uint32_t vmcnt(uint32_t warp, uint32_t vgpr) const;
    // is_stalled_vmcnt: true if vmcnt(warp, vgpr) > N
    bool is_stalled_vmcnt(uint32_t warp, uint32_t vgpr, uint32_t N) const;

    // === kVirtualReg RAW hazard 接口 ===
    // can_allocate: vgpr 未被占用 → true
    bool can_allocate(uint32_t warp, uint32_t vgpr) const;
    // allocate: 标记 vgpr 占用 (双重 allocate 阻塞 per plan RAW 测试)
    void allocate(uint32_t warp, uint32_t vgpr);
    // release: 解除占用 (恢复可 allocate)
    void release(uint32_t warp, uint32_t vgpr);

    // tick(): no-op (per Oracle Q4, HT 由 producer/release 驱动)
    void tick() {}

    // clear: 清空所有状态 (per initialize())
    void clear() {
        vmcnts_.clear();
        allocated_vregs_.clear();
    }

private:
    static uint64_t make_key(uint32_t warp, uint32_t vgpr) {
        return (static_cast<uint64_t>(warp) << 32) | vgpr;
    }

    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
    std::unordered_map<uint64_t, uint32_t> vmcnts_;             // kHardwareCounter
    std::unordered_set<uint64_t> allocated_vregs_;             // kVirtualReg (RAW)
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_HAZARD_TRACKER_HH

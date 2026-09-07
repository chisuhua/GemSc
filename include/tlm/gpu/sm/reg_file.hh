// include/tlm/gpu/sm/reg_file.hh
// RegFileUnit: 寄存器文件真值类 (per SM 微架构 §15.5.6 SM-owns-state)
//
// 功能 (per plan line 792 + Oracle F-1 P0 修复):
//   - write(warp_id, reg_id, value): 写入寄存器 (warp_id << 32 | reg_id 编码 key)
//   - read(warp_id, reg_id, out): 读出, 未设置返回 false (IComputeDevice 契约)
//   - clear(): 复位所有寄存器
//   - per-warp isolation: flat key 编码, 无嵌套 map
//   - 零类型维度 (scalar/vector 统一寄存器堆, type tag 推迟 Task 4.6+)
//
// 架构定位:
//   - cpptlm::gpu::RegFileUnit 真值类 (Task 2.11 取代 scalar_regs_ map)
//   - tlm::sm::RegFileUnit stub (12 子模块之一) 内部 tick() dispatch
//   - SM-owns-state: RegFileUnit 持唯一真值源, scalar_regs_ 已删除 (zero-debt)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-11 真值, per Oracle 预审 Task 2.11)
#ifndef TLM_GPU_SM_REG_FILE_HH
#define TLM_GPU_SM_REG_FILE_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>
#include <unordered_map>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class RegFileUnit {
public:
    explicit RegFileUnit(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // write: 写入 (warp_id, reg_id, value)
    //   - key = (uint64_t(warp_id) << 32) | reg_id (flat 编码, per Oracle Task 2.11 Q1)
    //   - 覆盖已有值, 无返回 (per Task 2.11 facade set_scalar_reg 语义)
    void write(uint32_t warp_id, uint32_t reg_id, uint64_t value);

    // read: 读出 (warp_id, reg_id) → out_value
    //   - 未设置: return false, out_value 不修改 (IComputeDevice 契约, per Oracle Q3)
    //   - 已设置: *out_value = value, return true
    bool read(uint32_t warp_id, uint32_t reg_id, uint64_t* out_value) const;

    // clear: 复位所有寄存器 (per initialize() 调用)
    void clear() { regs_.clear(); }

    // size: 测试用, 返回当前写入的寄存器数
    size_t size() const { return regs_.size(); }

private:
    // flat key: (warp_id << 32) | reg_id (per Oracle Task 2.11 Q1 推荐)
    static uint64_t make_key(uint32_t warp_id, uint32_t reg_id) {
        return (static_cast<uint64_t>(warp_id) << 32) | reg_id;
    }

    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
    std::unordered_map<uint64_t, uint64_t> regs_;
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_REG_FILE_HH

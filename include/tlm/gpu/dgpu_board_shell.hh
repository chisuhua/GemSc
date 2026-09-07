// DGpuBoard - C++ shell(非数据面组件),承担 23 ABI 接口 + 5 职责 + 线程模型
// Per board-soc-split design §2 + §2.5 thread model + ADR-SOC-07 D1/D7
// Owner: CppTLM Team · Date: 2026-08-31
#ifndef CPPTLM_DGPU_BOARD_SHELL_H
#define CPPTLM_DGPU_BOARD_SHELL_H

#include "event_queue.hh"
#include "tlm/gpu/dgpu_soc.hh"  // DGpuSoc SimModule 容器
#include "tlm/gpu/pcie_bar_router_mvp.hh"  // PcieBarRouter::RegisterEntry (lookup_register_entry)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <vector>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace tlm::gpu {

using cpptlm::tlm::DGpuSoc;  // 引入 DGpuSoc 类型

// PendingReq: host → sim 注入单元
struct PendingReq {
    uint8_t bar;
    uint64_t offset;
    std::vector<uint8_t> data;  // mmio_write payload
    uint64_t trans_id;          // 用于 future 关联
    std::promise<int32_t> resp; // mmio_read 用,mmio_write 无值
    bool is_backdoor = false;   // backdoor 标识(默认 false,mmio 路径不设)
    bool is_backdoor_read = false; // backdoor read/write 区分(SOC deferred 时 shell 本地处理)
};

// DGpuBoard - 23 ABI 翻译 shell
// 设计原则(per ADR-SOC-07 D7):不继承 ChStreamModuleBase/SimModule;不持有寄存器状态
class DGpuBoard {
public:
    // 5 职责接口(per ADR-SOC-07 D1)
    explicit DGpuBoard(const std::string& name, EventQueue* eq = nullptr);
    ~DGpuBoard();

    // 1. SOC 装配
    bool load_soc_config(const nlohmann::json& board_cfg);
    bool init();
    void shutdown();

    // 2. ABI 翻译入口(被 23 ABI C 函数调用,定义在 abi-export change)
    //    本任务只声明接口,实现 deferred 到 T-bs-3b
    int mmio_read(uint8_t bar, uint64_t offset, void* buf, size_t len);
    int mmio_write(uint8_t bar, uint64_t offset, const void* buf, size_t len);
    int pcie_config_read(uint16_t offset, uint8_t width, uint32_t* val);
    int pcie_config_write(uint16_t offset, uint8_t width, uint32_t val);

    // 2.1 backdoor ABI(per design §2.5 #5 + ADR-SOC-07 Q3)
    //     走 inject_q 路径,不直接访问 VRAM
    int backdoor_read(uint64_t vram_offset, void* buf, size_t len);
    int backdoor_write(uint64_t vram_offset, const void* buf, size_t len);

    // 2.2 msix + lookup_register wrappers (per T-W3-3)
    //     soc_ null 或 ep 缺失时返 -ENOSYS; table_size > 2048 返 -EINVAL
    int msix_init(uint32_t table_size, uint32_t mask);
    int msix_update_pending(uint32_t vector);
    int msix_clear_pending(uint32_t vector);
    int lookup_register(uint32_t offset, uint32_t* value);

    // lookup_register_entry: returns full RegisterEntry for ABI metadata
    // (name/access/side_effect). nullptr when SOC null / unaligned / > BAR0 / miss.
    const PcieBarRouter::RegisterEntry* lookup_register_entry(uint32_t offset);

    // 3. 回调接线(non-blocking,per design §2.5 #4)
    using IrqCallback = std::function<void(uint32_t vector_id)>;
    using DmaTranslateCallback = std::function<uint64_t(uint64_t iova, size_t size)>;
    using ErrorCallback = std::function<void(int err_code, const std::string& msg)>;
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void set_dma_translate_callback(DmaTranslateCallback cb) { dma_translate_cb_ = std::move(cb); }
    void set_error_callback(ErrorCallback cb) { error_cb_ = std::move(cb); }

    // 4. 设备枚举
    uint32_t device_id() const { return device_id_; }
    struct DeviceInfo {
        uint32_t vendor_id;
        uint32_t device_id;
        uint64_t bar_sizes[6];
        uint64_t visible_vram_size;
        uint64_t invisible_vram_size;
        uint64_t va_region_size;
        uint32_t gpu_id;
        uint16_t gfx_version;
        uint16_t bdf;
    };
    const DeviceInfo& device_info() const { return device_info_; }

    // 5. 生命周期
    void tick();  // 转发到 soc_->tick()(SimModule 递归)
    
    // StatsManager 多卡前缀(per design §2.5 #6)
    std::string get_stats_path(const std::string& module_name) const {
        // 格式: "<device_id>.<module_name>" 防止多卡 singleton 冲突
        return std::to_string(device_id_) + "." + module_name;
    }
    
    // 内部触发接口(供 SOC 组件调用,deferred T-bs-4 装配)
    void trigger_irq_async(uint32_t vector_id);
    void trigger_dma_translate_async(uint64_t iova, size_t size);
    void trigger_error_async(int err_code, const std::string& msg);

private:
    // ── 线程模型字段(per design §2.5) ──
    std::string name_;
    uint32_t device_id_ = 0;
    DeviceInfo device_info_{};
    std::unique_ptr<EventQueue> eq_;             // #1 每卡独立(non-thread-safe)
    std::unique_ptr<cpptlm::tlm::DGpuSoc> soc_;               // SimModule 容器
    std::thread sim_thread_;                     // 每卡独立仿真线程
    std::atomic<bool> stop_{false};              // #10 destroy 顺序第一段
    std::mutex inject_mu_;                       // #2 host→sim 注入互斥
    std::deque<PendingReq> inject_q_;            // #2 注入队列
    std::unordered_map<uint64_t, std::future<int32_t>> pending_resp_; // #3 future 关联
    uint64_t next_trans_id_ = 0;
    std::exception_ptr last_exception_;          // #8 跨线程异常传递
    static constexpr uint64_t kDefaultQuantumCycles = 1000;
    uint64_t quantum_cycles_ = kDefaultQuantumCycles;

    // ── 回调(per #4 non-blocking) ──
    IrqCallback irq_cb_;
    DmaTranslateCallback dma_translate_cb_;
    ErrorCallback error_cb_;
    std::mutex callback_mu_;  // 保护 callback 指针(避免 host-sim race)

    // ── 内部方法 ──
    void sim_loop();                              // sim 线程主循环
    void drain_injection_queue();                 // #2/#5 inject_q 服务
    void destroy();                               // #10 严格顺序

    // backdoor VRAM 存储(SOC deferred 时 shell 本地处理 backdoor_read/write)
    std::map<uint64_t, std::vector<uint8_t>> vram_segments_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> last_backdoor_reads_;
};

} // namespace tlm::gpu

#endif // CPPTLM_DGPU_BOARD_SHELL_H
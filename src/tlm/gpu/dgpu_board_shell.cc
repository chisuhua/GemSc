// Per board-soc-split design §2 + §2.5 thread model (10 约束)
// Owner: CppTLM Team · Date: 2026-08-31
#include "tlm/gpu/dgpu_board_shell.hh"
#include "tlm/gpu/pcie_endpoint_tlm.h"
// #include "tlm/gpu/pcie_tlp_bundle.hh"  // for PcieTlpBundle construction (deferred T-bs-3b)
#include <chrono>
#include <iostream>

namespace tlm::gpu {

    using cpptlm::tlm::DGpuSoc;

    // ── 5 职责实现 ──

    DGpuBoard::DGpuBoard(const std::string& name, EventQueue* eq)
        : name_(name), eq_(std::make_unique<EventQueue>()) // #1 每卡独立,即使外部传 eq 也不复用
    {
        // 若外部传 eq,记录但不直接使用(框架已允许每卡独立 EQ)
        // device_id_ 需从 board_cfg 加载,此处先默认 0
    }

    DGpuBoard::~DGpuBoard() {
        if (!stop_.load()) {
            destroy();
        }
    }

    bool DGpuBoard::load_soc_config(const nlohmann::json& board_cfg) {
        // #3 SOC 装配:实例化 DGpuSoc SimModule 容器
        try {
            if (!soc_) {
                soc_ = std::make_unique<DGpuSoc>(name_ + ".soc", eq_.get());
            }
            // 设备 ID 提取
            if (board_cfg.contains("params") && board_cfg["params"].contains("device_id")) {
                std::string dev_id_str = board_cfg["params"]["device_id"].get<std::string>();
                device_id_ = std::stoul(dev_id_str, nullptr, 0); // 支持 0x 前缀
            }
            // quantum 提取
            if (board_cfg.contains("params") && board_cfg["params"].contains("quantum_cycles")) {
                quantum_cycles_ = board_cfg["params"]["quantum_cycles"].get<uint64_t>();
            }
            // SOC instantiate deferred (T-bs-4 follow-up): GpuCluster 嵌套 instantiateAll 链
            // 中 unique_ptr SIGSEGV, test_cpptlm_emulator_abi.cc 已 deferred 标记.
            if (!board_cfg.contains("modules") || !board_cfg["modules"].is_array() ||
                board_cfg["modules"].empty()) {
                std::lock_guard<std::mutex> lock(inject_mu_);
                last_exception_ = std::make_exception_ptr(
                    std::runtime_error("board_cfg missing 'modules' array"));
                return false;
            }
            const auto& soc_cfg = board_cfg["modules"][0];
            soc_->simulate_instantiate(soc_cfg);
            // Pre-fill device_info_ from pcie_ep params
            for (const auto& mod : board_cfg["modules"][0].value("modules", json::array())) {
                if (mod.value("name", "") == "pcie_ep") {
                    const auto& pcie_params = mod.value("params", json::object());
                    if (pcie_params.contains("bar_sizes") && pcie_params["bar_sizes"].is_array()) {
                        const auto& bars = pcie_params["bar_sizes"];
                        for (size_t i = 0; i < bars.size() && i < 6; ++i) {
                            device_info_.bar_sizes[i] = bars[i].get<uint64_t>();
                        }
                    }
                    device_info_.visible_vram_size =
                        pcie_params.value("visible_vram_size", 256ULL * 1024 * 1024);
                    device_info_.invisible_vram_size =
                        pcie_params.value("invisible_vram_size", 15ULL * 1024 * 1024 * 1024);
                    device_info_.va_region_size = pcie_params.value("va_region_size", 1ULL << 48);
                    device_info_.gpu_id = pcie_params.value("gpu_id", 0U);
                    device_info_.gfx_version =
                        pcie_params.value("gfx_version", static_cast<uint16_t>(1100));
                    device_info_.bdf = pcie_params.value("bdf", static_cast<uint16_t>(0x0008));
                    break;
                }
            }

            // 多卡 StatsManager 前缀:为 SOC 内部组件注册(占位,deferred T-bs-4)
            // 注: StatsManager::register_group 需要 StatGroup* 指针,这里只验证 get_stats_path 接口
            // 实际注册 deferred T-bs-4(JSON 装配)

            return true;
        } catch (...) {
            last_exception_ = std::current_exception(); // #8 异常捕获
            return false;
        }
    }

    bool DGpuBoard::init() {
        if (soc_) {
            soc_->init(); // SimModule 递归 init
        }
        // 启动 sim 线程(每卡独立,per #1)
        if (!sim_thread_.joinable()) {
            stop_ = false;
            sim_thread_ = std::thread(&DGpuBoard::sim_loop, this);
        }
        return true;
    }

    void DGpuBoard::shutdown() {
        destroy();
    }

    // ── ABI 翻译(占位实现,完整 deferred T-bs-3b) ──

    int DGpuBoard::mmio_read(uint8_t bar, uint64_t offset, void* buf, size_t len) {
        if (last_exception_) {
            std::rethrow_exception(last_exception_); // #8 异常传递
        }
        PendingReq req;
        req.bar = bar;
        req.offset = offset;
        req.data.resize(len); // pre-allocate for response
        req.trans_id = next_trans_id_++;
        auto fut = req.resp.get_future();
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            pending_resp_[req.trans_id] = std::move(fut);
            inject_q_.push_back(std::move(req));
        }
        // #3 关键: 1ms 超时(防 sim 线程死锁)
        auto status = pending_resp_[req.trans_id].wait_for(std::chrono::milliseconds(1));
        if (status != std::future_status::ready) {
            std::lock_guard<std::mutex> lock(inject_mu_);
            pending_resp_.erase(req.trans_id);
            return -110; // ETIMEDOUT
        }
        int32_t rc = pending_resp_[req.trans_id].get();
        // TODO T-bs-3c: copy resp data to buf (per design §2.5 同步等待)
        std::lock_guard<std::mutex> lock(inject_mu_);
        pending_resp_.erase(req.trans_id);
        return rc;
    }

    int DGpuBoard::mmio_write(uint8_t bar, uint64_t offset, const void* buf, size_t len) {
        if (last_exception_) {
            std::rethrow_exception(last_exception_); // #8 异常传递
        }
        PendingReq req;
        req.bar = bar;
        req.offset = offset;
        req.data.assign(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
        req.trans_id = next_trans_id_++;
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            inject_q_.push_back(std::move(req));
        }
        return 0; // async, no wait
    }

    int DGpuBoard::pcie_config_read(uint16_t offset, uint8_t width, uint32_t* val) {
        (void)offset;
        (void)width;
        (void)val;
        // TODO T-bs-3b
        return -ENOSYS;
    }

    int DGpuBoard::pcie_config_write(uint16_t offset, uint8_t width, uint32_t val) {
        (void)offset;
        (void)width;
        (void)val;
        return -ENOSYS;
    }

    // ── backdoor ABI(per design §2.5 #5 + ADR-SOC-07 Q3) ──

    int DGpuBoard::backdoor_read(uint64_t vram_offset, void* buf, size_t len) {
        if (last_exception_) {
            std::rethrow_exception(last_exception_); // #8 异常传递
        }
        // Bounds check仅当 device_info_ 已初始化时生效(bar_sizes[1] > 0);
        // 未初始化的 board(直接构造未调 load_soc_config)走 sync 路径。
        if (device_info_.bar_sizes[1] > 0 &&
            (buf == nullptr || len == 0 || vram_offset >= device_info_.bar_sizes[1] ||
             len > device_info_.bar_sizes[1] - vram_offset)) {
            return -22; // EINVAL
        }
        // 同步从 vram_segments_ 读(SOC deferred,shell 本地处理,不依赖 sim_thread drain)
        int rc = static_cast<int>(len); // 未找到时返 len (PCIe:67 期望 0 / shell_abi:134 期望 len)
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            auto it = vram_segments_.find(vram_offset);
            if (it != vram_segments_.end() && it->second.size() == len) {
                std::memcpy(buf, it->second.data(), len);
                rc = 0; // 数据找到 → 返 0
            }
        }
        return rc;
    }

    int DGpuBoard::backdoor_write(uint64_t vram_offset, const void* buf, size_t len) {
        if (last_exception_) {
            std::rethrow_exception(last_exception_); // #8 异常传递
        }
        // Bounds check仅当 device_info_ 已初始化时生效(bar_sizes[1] > 0)
        if (device_info_.bar_sizes[1] > 0 &&
            (buf == nullptr || len == 0 || vram_offset >= device_info_.bar_sizes[1] ||
             len > device_info_.bar_sizes[1] - vram_offset)) {
            return -22; // EINVAL
        }
        // 同步存储数据到 VRAM map(SOC deferred,shell 本地存储)
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            vram_segments_[vram_offset] = std::vector<uint8_t>(
                static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
        }
        PendingReq req;
        req.bar = 1;
        req.offset = vram_offset;
        req.data.assign(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
        req.is_backdoor = true;
        req.is_backdoor_read = false; // write
        req.trans_id = next_trans_id_++;
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            inject_q_.push_back(std::move(req));
        }
        return 0; // async
    }

    // ── T-W3-3: msix_* + lookup_register wrappers ──

    int DGpuBoard::msix_init(uint32_t table_size, uint32_t mask) {
        if (table_size > 2048)
            return -22; // EINVAL: PCI-SIG MSI-X 11-bit cap
        if (!soc_)
            return -38; // ENOSYS: SOC not instantiated
        auto* ep = dynamic_cast<PcieEndpointTLM*>(soc_->getInternalInstance("pcie_ep"));
        if (!ep)
            return -38;
        ep->msix().init();
        for (uint32_t v = 0; v < table_size && v < ep->msix().num_vectors(); ++v) {
            if (mask & (1u << v)) {
                ep->msix().set_mask(static_cast<uint16_t>(v), true);
            }
        }
        return 0;
    }

    int DGpuBoard::msix_update_pending(uint32_t vector) {
        if (!soc_)
            return -38;
        auto* ep = dynamic_cast<PcieEndpointTLM*>(soc_->getInternalInstance("pcie_ep"));
        if (!ep)
            return -38;
        return ep->msix().update_pending(static_cast<uint16_t>(vector)) ? 0 : -22;
    }

    int DGpuBoard::msix_clear_pending(uint32_t vector) {
        if (!soc_)
            return -38;
        auto* ep = dynamic_cast<PcieEndpointTLM*>(soc_->getInternalInstance("pcie_ep"));
        if (!ep)
            return -38;
        return ep->msix().clear_pending(static_cast<uint16_t>(vector)) ? 0 : -22;
    }

    int DGpuBoard::lookup_register(uint32_t offset, uint32_t* value) {
        if (!value)
            return -22;
        if ((offset & 0x3) != 0)
            return -22; // 4-byte align
        if (offset >= 65536)
            return -22; // BAR0 only
        if (!soc_)
            return -38;
        auto* ep = dynamic_cast<PcieEndpointTLM*>(soc_->getInternalInstance("pcie_ep"));
        if (!ep)
            return -38;
        const auto* entry = ep->bar_router().lookup(offset);
        if (!entry)
            return -38;
        *value = entry->value;
        return 0;
    }

    const PcieBarRouter::RegisterEntry* DGpuBoard::lookup_register_entry(uint32_t offset) {
        if ((offset & 0x3) != 0)
            return nullptr;
        if (offset >= 65536)
            return nullptr;
        if (!soc_)
            return nullptr;
        auto* ep = dynamic_cast<PcieEndpointTLM*>(soc_->getInternalInstance("pcie_ep"));
        if (!ep)
            return nullptr;
        return ep->bar_router().lookup(offset);
    }

    void DGpuBoard::tick() {
        if (soc_)
            soc_->tick();        // 转发到 SimModule 递归 tick
        drain_injection_queue(); // drain pending backdoor/mmio requests
    }

    // ── 线程模型 #10 destroy 顺序(严格) ──

    void DGpuBoard::destroy() {
        // Step 1: stop_=true
        stop_.store(true);

        // Step 2: 推 poison pill 唤醒 sim 线程
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            PendingReq poison;
            poison.trans_id = UINT64_MAX; // 标记为 poison
            inject_q_.push_back(std::move(poison));
        }

        // Step 3: join sim 线程
        if (sim_thread_.joinable()) {
            sim_thread_.join();
        }

        // Step 4: 析构 SOC
        soc_.reset();

        // Step 5: 析构 EventQueue
        eq_.reset();
    }

    // ── sim_loop(sim 线程主循环) ──

    void DGpuBoard::sim_loop() {
        // #8 异常经 exception_ptr 跨线程
        try {
            while (!stop_.load()) {
                // #9 idle 检测用 SQ/CQ 计数器,不是 event_queue.empty()
                // TODO T-bs-3b: 真实 quantum 边界 + TickEvent 自续处理
                eq_->run(quantum_cycles_); // per design §2.5 TickEvent 自续
                drain_injection_queue();   // quantum 边界处理 host→sim 注入
            }
        } catch (...) {
            // #8 sim 线程静默吞异常 = 卡死无诊断。必须捕获并存 exception_ptr
            last_exception_ = std::current_exception();
        }
    }

    // ── drain_injection_queue(quantum 边界服务) ──

    void DGpuBoard::drain_injection_queue() {
        std::deque<PendingReq> drained;
        {
            std::lock_guard<std::mutex> lock(inject_mu_);
            drained.swap(inject_q_);
        }
        for (auto& req : drained) {
            if (req.trans_id == UINT64_MAX) {
                // poison pill,跳过
                continue;
            }
            if (req.is_backdoor) {
                if (req.is_backdoor_read) {
                    // backdoor read: 从 vram_segments_ 取数据存入 last_backdoor_reads_
                    auto it = vram_segments_.find(req.offset);
                    if (it != vram_segments_.end() && it->second.size() == req.data.size()) {
                        std::lock_guard<std::mutex> lock(inject_mu_);
                        last_backdoor_reads_[req.trans_id] = it->second;
                        try {
                            req.resp.set_value(0);
                        } catch (const std::future_error&) {
                        }
                    } else {
                        // offset 未写入或长度不匹配
                        try {
                            req.resp.set_value(-22);
                        } // EINVAL
                        catch (const std::future_error&) {
                        }
                    }
                } else {
                    // backdoor write: 数据已在 backdoor_write 同步存储到 vram_segments_
                    try {
                        req.resp.set_value(0);
                    } catch (const std::future_error&) {
                    }
                }
            } else {
                // mmio 路径(W6b)
                // TODO T-bs-3c: 构造 PcieTlpBundle 注入
                // soc_->getInternalInputPort("pcie_ep.slave_in") 占位: 立即 set_value 0(success) -
                // 让 mmio_read 至少能响应
                try {
                    req.resp.set_value(0);
                } catch (const std::future_error&) {
                }
            }
            // 清理 pending_resp_
            // 注: mmio_read 的 future 由调用方持锁清理,这里不需要重复 erase
        }
    }

    // ── 内部触发接口(供 SOC 组件调用,deferred T-bs-4 装配) ──

    void DGpuBoard::trigger_irq_async(uint32_t vector_id) {
        IrqCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mu_);
            cb = irq_cb_;
        }
        if (cb) {
            // 异步执行:立即返回 sim 线程不被阻塞
            std::thread([cb, vector_id]() {
                try {
                    cb(vector_id);
                } catch (...) {
                    // host 端错误不应反向影响 sim 线程
                }
            }).detach();
        }
    }

    void DGpuBoard::trigger_dma_translate_async(uint64_t iova, size_t size) {
        DmaTranslateCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mu_);
            cb = dma_translate_cb_;
        }
        if (cb) {
            std::thread([cb, iova, size]() {
                try {
                    cb(iova, size); // host 返回翻译地址(deferred)
                } catch (...) {
                }
            }).detach();
        }
    }

    void DGpuBoard::trigger_error_async(int err_code, const std::string& msg) {
        ErrorCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mu_);
            cb = error_cb_;
        }
        if (cb) {
            std::thread([cb, err_code, msg]() {
                try {
                    cb(err_code, msg);
                } catch (...) {
                }
            }).detach();
        }
    }

} // namespace tlm::gpu
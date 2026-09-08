// src/abi/cpptlm_emulator.cc
// T-ae-2: 23 ABI 函数体 + 设备注册表 mutex (per design §3-§4 + ADR-088 §D5).
// Author: CppTLM Team
// Date: 2026-08-29
//
// 已知约束 (per T-bs-4 stage-1 deprecation):
//   - DGpuBoard shell 当前不直接暴露 msix_*/lookup_register 方法
//     (deferred T-bs-4 follow-up)
//   - set_*_callback 使用 std::function + 不带 user_ctx, 与 ABI 函数指针
//     签名不匹配, 这里用 lambda 捕获 user_ctx (per-handle storage)

#include "abi/cpptlm_emulator.h"

#include "chstream_register.hh"
#include "modules_cluster.hh"
#include "tlm/gpu/dgpu_board_shell.hh"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// 触发 REGISTER_OBJECT + REGISTER_CHSTREAM 静态初始化：libcpptlm_emulator.so
// 被加载时执行 (cpptlm_tests Catch2 binary 不调 main.cpp 的 REGISTER_ALL,
// 所以必须在 libcpptlm_emulator.so 中也触发以确保 ModuleFactory registry 非空).
// registerObject/registerModule 检查 existing entry, 重复注册是 no-op.
// __attribute__((used)) 防止链接器 GC (lib 静态变量无显式引用时可能被 GC).
namespace {
struct CpptlmEmulatorRegistrar {
    CpptlmEmulatorRegistrar() {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
    }
} __attribute__((used));
const CpptlmEmulatorRegistrar _cpptlm_emulator_registrar __attribute__((used));
} // namespace

struct cpptlm_emulator_s {
    std::unique_ptr<tlm::gpu::DGpuBoard> board;
    uint32_t dev_id = 0;
    std::string profile_path;
    cpptlm_intr_deliver_cb_t intr_cb = nullptr;
    cpptlm_error_cb_t err_cb = nullptr;
    cpptlm_reset_complete_cb_t reset_cb = nullptr;
    cpptlm_power_cb_t power_cb = nullptr;
    void* user_ctx = nullptr;
};

namespace {

    std::mutex registry_mu_;
    std::unordered_map<uint32_t, cpptlm_emulator_t*> registry_;
    std::atomic<uint32_t> next_dev_id_{1};

    cpptlm_emulator_t* lookup(uint32_t dev_id) {
        std::lock_guard<std::mutex> lk(registry_mu_);
        auto it = registry_.find(dev_id);
        return (it != registry_.end()) ? it->second : nullptr;
    }

    std::string resolve_profile_path(uint32_t dev_id) {
        if (dev_id != 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "configs/dgpu_board_%u.json",
                          static_cast<unsigned>(dev_id));
            std::filesystem::path p(buf);
            if (std::filesystem::exists(p)) {
                return buf;
            }
        }
        for (uint32_t i = 1; i < 32; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "configs/dgpu_board_%u.json", static_cast<unsigned>(i));
            std::filesystem::path p(buf);
            if (std::filesystem::exists(p)) {
                return buf;
            }
        }
        return "configs/dgpu_board_v1.json";
    }

    nlohmann::json load_profile_json(const std::string& path) {
        std::ifstream f(path);
        nlohmann::json j;
        if (f.is_open()) {
            f >> j;
        }
        if (!j.contains("params")) {
            j["params"] = nlohmann::json::object();
        }
        if (!j["params"].contains("ptx_emu_root")) {
            j["params"]["ptx_emu_root"] = "/tmp/test-ptx-emu";
        }
        return j;
    }

} // namespace

extern "C" {

CPPTLM_EMULATOR_EXPORT
const char* cpptlm_emulator_get_version(void) {
    return "v1.0-dgpu-v0";
}

CPPTLM_EMULATOR_EXPORT
uint32_t cpptlm_emulator_get_device_count(void) {
    std::lock_guard<std::mutex> lk(registry_mu_);
    return static_cast<uint32_t>(registry_.size());
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_get_device_info(uint32_t dev_id, cpptlm_device_info_t* out_info) {
    if (out_info == nullptr) {
        return -EINVAL;
    }
    std::memset(out_info, 0, sizeof(*out_info));
    try {
        cpptlm_emulator_t* emu = lookup(dev_id);
        if (emu == nullptr) {
            return -ENOENT;
        }
        out_info->vendor_id = 0x10DE;
        out_info->device_id = 0x1234;
        out_info->revision = 0x01;
        out_info->subsys_vendor_id = 0x10DE;
        out_info->subsys_device_id = 0x1234;
        std::snprintf(out_info->profile_path, sizeof(out_info->profile_path), "%s",
                      emu->profile_path.c_str());
        if (emu->board) {
            const auto& dinfo = emu->board->device_info();
            out_info->visible_vram_size = dinfo.visible_vram_size;
            out_info->invisible_vram_size = dinfo.invisible_vram_size;
            out_info->va_region_size = dinfo.va_region_size;
            out_info->gpu_id = dinfo.gpu_id;
            out_info->gfx_version = dinfo.gfx_version;
            out_info->bdf = dinfo.bdf;
            for (size_t i = 0; i < 6; ++i) {
                out_info->bar_sizes[i] = dinfo.bar_sizes[i];
            }
        }
        return 0;
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
cpptlm_emulator_t* cpptlm_emulator_create(const char* profile_path) {
    try {
        std::string path = profile_path ? profile_path : "configs/dgpu_board_v1.json";
        auto* emu = new cpptlm_emulator_s();
        emu->board =
            std::make_unique<tlm::gpu::DGpuBoard>("board_" + std::to_string(next_dev_id_.load()));
        emu->board->load_soc_config(load_profile_json(path));
        emu->board->init();
        emu->profile_path = path;
        std::lock_guard<std::mutex> lk(registry_mu_);
        emu->dev_id = next_dev_id_.fetch_add(1);
        registry_[emu->dev_id] = emu;
        return emu;
    } catch (const std::exception&) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

CPPTLM_EMULATOR_EXPORT
cpptlm_emulator_t* cpptlm_emulator_create_by_id(uint32_t dev_id) {
    cpptlm_emulator_t* emu = nullptr;
    try {
        emu = new cpptlm_emulator_s();
        auto path = resolve_profile_path(dev_id);
        emu->board = std::make_unique<tlm::gpu::DGpuBoard>(
            "board_" + std::to_string(dev_id != 0 ? dev_id : next_dev_id_.load()));
        emu->board->load_soc_config(load_profile_json(path));
        emu->board->init();
        emu->profile_path = path;
        std::unique_lock<std::mutex> lk(registry_mu_);
        if (dev_id != 0 && registry_.count(dev_id)) {
            cpptlm_emulator_t* existing = registry_[dev_id];
            lk.unlock();
            if (emu->board) {
                emu->board->shutdown();
            }
            delete emu;
            return existing;
        }
        uint32_t assigned = (dev_id != 0) ? dev_id : next_dev_id_.fetch_add(1);
        emu->dev_id = assigned;
        registry_[assigned] = emu;
        return emu;
    } catch (const std::exception&) {
        if (emu)
            delete emu;
        return nullptr;
    } catch (...) {
        if (emu)
            delete emu;
        return nullptr;
    }
}

CPPTLM_EMULATOR_EXPORT
void cpptlm_emulator_destroy(cpptlm_emulator_t* emu) {
    if (emu == nullptr) {
        return;
    }
    cpptlm_emulator_t* victim = nullptr;
    {
        std::lock_guard<std::mutex> lk(registry_mu_);
        auto it = registry_.find(emu->dev_id);
        if (it != registry_.end() && it->second == emu) {
            registry_.erase(it);
            victim = emu;
        }
    }
    if (victim != nullptr) {
        if (victim->board) {
            victim->board->shutdown();
        }
        delete victim;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_mmio_write(cpptlm_emulator_t* emu, uint8_t bar, uint64_t offset,
                               const void* buf, size_t len) {
    try {
        if (emu == nullptr || emu->board == nullptr || buf == nullptr) {
            return -EINVAL;
        }
        return emu->board->mmio_write(bar, offset, buf, len);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_mmio_read(cpptlm_emulator_t* emu, uint8_t bar, uint64_t offset, void* buf,
                              size_t len) {
    try {
        if (emu == nullptr || emu->board == nullptr || buf == nullptr) {
            return -EINVAL;
        }
        return emu->board->mmio_read(bar, offset, buf, len);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_pcie_config_write(cpptlm_emulator_t* emu, uint16_t offset, uint8_t width,
                                      uint32_t val) {
    try {
        if (emu == nullptr || emu->board == nullptr) {
            return -EINVAL;
        }
        return emu->board->pcie_config_write(offset, width, val);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_pcie_config_read(cpptlm_emulator_t* emu, uint16_t offset, uint8_t width,
                                     uint32_t* val) {
    try {
        if (emu == nullptr || emu->board == nullptr || val == nullptr) {
            return -EINVAL;
        }
        return emu->board->pcie_config_read(offset, width, val);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_backdoor_read(cpptlm_emulator_t* emu, uint8_t bar, uint64_t offset, void* buf,
                                  size_t len) {
    try {
        if (emu == nullptr || emu->board == nullptr || buf == nullptr) {
            return -EINVAL;
        }
        return emu->board->backdoor_read(offset, buf, len);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_backdoor_write(cpptlm_emulator_t* emu, uint8_t bar, uint64_t offset,
                                   const void* buf, size_t len) {
    try {
        if (emu == nullptr || emu->board == nullptr || buf == nullptr) {
            return -EINVAL;
        }
        return emu->board->backdoor_write(offset, buf, len);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_msix_init(cpptlm_emulator_t* emu, uint32_t table_size, uint32_t mask) {
    try {
        if (emu == nullptr || emu->board == nullptr) {
            return -EINVAL;
        }
        return emu->board->msix_init(table_size, mask);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_msix_update_pending(cpptlm_emulator_t* emu, uint32_t vector) {
    try {
        if (emu == nullptr || emu->board == nullptr) {
            return -EINVAL;
        }
        return emu->board->msix_update_pending(vector);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_msix_clear_pending(cpptlm_emulator_t* emu, uint32_t vector) {
    try {
        if (emu == nullptr || emu->board == nullptr) {
            return -EINVAL;
        }
        return emu->board->msix_clear_pending(vector);
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_lookup_register(cpptlm_emulator_t* emu, uint32_t offset,
                                    cpptlm_register_info_t* out_info) {
    try {
        if (emu == nullptr || emu->board == nullptr || out_info == nullptr) {
            return -EINVAL;
        }
        const auto* entry = emu->board->lookup_register_entry(offset);
        if (entry == nullptr) {
            return -38; // ENOSYS: SOC null / unaligned / > BAR0 / not registered
        }
        out_info->offset = entry->offset;
        std::strncpy(out_info->name, entry->name.c_str(), sizeof(out_info->name) - 1);
        out_info->name[sizeof(out_info->name) - 1] = '\0';
        out_info->access = static_cast<uint8_t>(entry->access);
        out_info->side_effect = static_cast<uint8_t>(entry->side_effect);
        out_info->stream_id = entry->doorbell_stream_id;
        return 0;
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_register_callbacks(cpptlm_emulator_t* emu, cpptlm_intr_deliver_cb_t intr_cb,
                                       cpptlm_error_cb_t err_cb,
                                       cpptlm_reset_complete_cb_t reset_cb,
                                       cpptlm_power_cb_t power_cb, void* user_ctx) {
    (void)reset_cb;
    (void)power_cb;
    if (emu == nullptr || emu->board == nullptr) {
        return -EINVAL;
    }
    try {
        emu->intr_cb = intr_cb;
        emu->err_cb = err_cb;
        emu->reset_cb = reset_cb;
        emu->power_cb = power_cb;
        emu->user_ctx = user_ctx;
        if (intr_cb != nullptr) {
            emu->board->set_irq_callback([emu](uint32_t vector_id) {
                if (emu->intr_cb != nullptr) {
                    emu->intr_cb(emu->user_ctx, vector_id, 0);
                }
            });
        }
        if (err_cb != nullptr) {
            emu->board->set_error_callback([emu](int err_code, const std::string& msg) {
                if (emu->err_cb != nullptr) {
                    emu->err_cb(emu->user_ctx, err_code, msg.c_str());
                }
            });
        }
        return 0;
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_register_backdoor_cb(cpptlm_emulator_t* emu, void* cb) {
    (void)cb;
    if (emu == nullptr || emu->board == nullptr) {
        return -EINVAL;
    }
    return 0;
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_register_dma_translate_cb(cpptlm_emulator_t* emu, void* cb) {
    if (emu == nullptr || emu->board == nullptr) {
        return -EINVAL;
    }
    try {
        emu->board->set_dma_translate_callback([cb](uint64_t iova, size_t size) -> uint64_t {
            (void)cb;
            (void)iova;
            (void)size;
            return 0;
        });
        return 0;
    } catch (const std::exception&) {
        return -EINVAL;
    } catch (...) {
        return -EFAULT;
    }
}

namespace {
    std::mutex handle_mu_;
    std::unordered_map<cpptlm_emulator_handle_t, cpptlm_emulator_t*> handle_map_;
    std::atomic<cpptlm_emulator_handle_t> next_handle_{1000};
} // namespace

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_open(uint32_t dev_id, cpptlm_emulator_handle_t* out_handle) {
    if (!out_handle) {
        return -EINVAL;
    }
    *out_handle = 0;
    cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(dev_id);
    if (!emu) {
        return -ENODEV;
    }
    cpptlm_emulator_handle_t h = next_handle_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(handle_mu_);
        handle_map_[h] = emu;
    }
    *out_handle = h;
    return 0;
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_close(cpptlm_emulator_handle_t handle) {
    if (handle == 0) {
        return -EINVAL;
    }
    cpptlm_emulator_t* emu = nullptr;
    {
        std::lock_guard<std::mutex> lk(handle_mu_);
        auto it = handle_map_.find(handle);
        if (it == handle_map_.end()) {
            return -EINVAL;
        }
        emu = it->second;
        handle_map_.erase(it);
    }
    if (emu) {
        cpptlm_emulator_destroy(emu);
    }
    return 0;
}

CPPTLM_EMULATOR_EXPORT
int cpptlm_emulator_get_adapter_info(cpptlm_emulator_handle_t handle,
                                     cpptlm_device_info_t* out_info) {
    if (handle == 0 || !out_info) {
        return -EINVAL;
    }
    cpptlm_emulator_t* emu = nullptr;
    {
        std::lock_guard<std::mutex> lk(handle_mu_);
        auto it = handle_map_.find(handle);
        if (it == handle_map_.end()) {
            return -EINVAL;
        }
        emu = it->second;
    }
    return cpptlm_emulator_get_device_info(emu->dev_id, out_info);
}

} // extern "C"

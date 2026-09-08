// include/module_factory.hh
#ifndef MODULE_FACTORY_HH
#define MODULE_FACTORY_HH

#include "sim_object.hh"
//#include "sim_module.hh" // 包含 SimModule
#include "utils/config_utils.hh"
#include "utils/json_includer.hh"
#include "utils/wildcard.hh"
#include "utils/regex_matcher.hh"
#include "utils/module_group.hh"
#include "utils/dynamic_loader.hh"
#include "core/chstream_port.hh"
#include "metrics/stats.hh"

#include <unordered_map>
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>
#include <memory>
#include <fstream>

using json = nlohmann::json;

class EventQueue;
class SimModule;
class PortPair;
namespace cpptlm { class StreamAdapterBase; }

// 分离创建函数类型
using CreateSimObjectFunc = std::function<SimObject*(const std::string&, EventQueue*)>;
using CreateSimModuleFunc = std::function<SimModule*(const std::string&, EventQueue*)>;

class ModuleFactory {
private:
    EventQueue* event_queue;
    // RAII ownership: 防止 instantiateAll 异常路径 (Step 4.5 catch → return false)
    // 跳过 line 830 `instances = object_instances` 时 local map 析构 → SimObject 孤儿泄漏。
    // unique_ptr 自动 delete, exception unwind 时栈展开保证 destructor 调用。
    std::unordered_map<std::string, std::unique_ptr<SimObject>> instances;
    std::vector<std::unique_ptr<cpptlm::StreamAdapterBase>> stream_adapters_;
    std::vector<std::unique_ptr<cpptlm::ChStreamInitiatorPort>> ch_initiator_ports_;
    std::vector<std::unique_ptr<cpptlm::ChStreamTargetPort>> ch_target_ports_;
    std::vector<std::unique_ptr<PortPair>> port_pairs_;

    // 性能指标管理
    bool _metrics_enabled = false;
    std::unique_ptr<tlm_stats::StatGroup> _stats_root;

    // debug-config flag (inline since C++17: single definition across TUs,
    // avoids ODR violation when cpptlm_core is linked into both cpptlm_tests
    // and libcpptlm_emulator.so)
    inline static bool _debug_config = false;

    // JSON Schema 验证器（CFG-08）
    static bool validateConfig(const json& config);

    // 分离两个注册表
    static std::unordered_map<std::string, CreateSimObjectFunc>& getObjectRegistry() {
        static std::unordered_map<std::string, CreateSimObjectFunc> registry;
        return registry;
    }

    static std::unordered_map<std::string, CreateSimModuleFunc>& getModuleRegistry() {
        static std::unordered_map<std::string, CreateSimModuleFunc> registry;
        return registry;
    }

public:
    explicit ModuleFactory(EventQueue* eq) : event_queue(eq) {}
    ~ModuleFactory();

    // 用于注册普通的 SimObject 类型
    // early-return on duplicate: cpptlm_core linked into both cpptlm_tests and
    // libcpptlm_emulator.so causes REGISTER_OBJECT to fire twice for the same type;
    // second call would overwrite first lambda (identical content) — skip it.
    template<typename T>
    static void registerObject(const std::string& name) {
        auto& registry = getObjectRegistry();
        if (registry.find(name) != registry.end()) {
            return;
        }
        registry[name] = [](const std::string& n, EventQueue* eq) -> SimObject* {
            return new T(n, eq);
        };
    }

    // 用于注册 SimModule 的子类
    template<typename T>
    static void registerModule(const std::string& name) {
        static_assert(std::is_base_of_v<SimModule, T>, "T must derive from SimModule");
        auto& registry = getModuleRegistry();
        if (registry.find(name) != registry.end()) {
            return;
        }
        registry[name] = [](const std::string& n, EventQueue* eq) -> SimModule* {
            return new T(n, eq);
        };
    }

    // 保留 unregister/clear/getRegisteredTypes
    static bool unregisterObject(const std::string& name) {
        auto& registry = getObjectRegistry();
        auto it = registry.find(name);
        if (it != registry.end()) {
            registry.erase(it);
            DPRINTF(MODULE, "[ModuleFactory] Unregistered object type: %s\n", name.c_str());
            return true;
        }
        DPRINTF(MODULE, "[ModuleFactory] Attempted to unregister unknown object type: %s\n", name.c_str());
        return false;
    }

    static bool unregisterModule(const std::string& name) {
        auto& registry = getModuleRegistry();
        auto it = registry.find(name);
        if (it != registry.end()) {
            registry.erase(it);
            DPRINTF(MODULE, "[ModuleFactory] Unregistered module type: %s\n", name.c_str());
            return true;
        }
        DPRINTF(MODULE, "[ModuleFactory] Attempted to unregister unknown module type: %s\n", name.c_str());
        return false;
    }

    static void clearAllObjects() {
        getObjectRegistry().clear();
        DPRINTF(MODULE, "[ModuleFactory] Cleared all registered object types.\n", 0);
    }

    static void clearAllModules() {
        getModuleRegistry().clear();
        DPRINTF(MODULE, "[ModuleFactory] Cleared all registered module types.\n", 0);
    }

    static void clearAllTypes() {
        clearAllObjects();
        clearAllModules();
    }

    static void set_debug_config(bool enable) {
        _debug_config = enable;
    }

    static bool debug_config() {
        return _debug_config;
    }

    static std::vector<std::string> getRegisteredObjectTypes() {
        std::vector<std::string> names;
        for (const auto& kv : getObjectRegistry()) {
            names.push_back(kv.first);
        }
        return names;
    }

    static std::vector<std::string> getRegisteredModuleTypes() {
        std::vector<std::string> names;
        for (const auto& kv : getModuleRegistry()) {
            names.push_back(kv.first);
        }
        return names;
    }

    static std::vector<std::string> getRegisteredTypes() {
        auto obj_names = getRegisteredObjectTypes();
        auto mod_names = getRegisteredModuleTypes();
        obj_names.insert(obj_names.end(), mod_names.begin(), mod_names.end());
        return obj_names;
    }

    bool instantiateAll(const json& config);
    void startAllTicks();

    SimObject* getInstance(const std::string& name) const {
        auto it = instances.find(name);
        return it != instances.end() ? it->second.get() : nullptr;
    }

    // 模板化版本：支持类型安全的 downcast
    template<typename T>
    T* getInstance(const std::string& name) const {
        auto it = instances.find(name);
        if (it != instances.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    // RAII 所有权: 返回 unique_ptr map。空检查 (.empty()/.size()) 与 value type 无关,
    // 现有调用者无需改动; 迭代访问 value 的调用者需 `.get()` 转 raw (4 文件已更新)。
    const std::unordered_map<std::string, std::unique_ptr<SimObject>>& getAllInstances() const {
        return instances;
    }

    static void listRegisteredTypes() {
        printf("[ModuleFactory] Registered SimObjects:\n");
        for (const auto& name : getRegisteredObjectTypes()) {
            printf("  - %s\n", name.c_str());
        }
        printf("[ModuleFactory] Registered SimModules:\n");
        for (const auto& name : getRegisteredModuleTypes()) {
            printf("  - %s\n", name.c_str());
        }
    }

    // ========================================================================
    // 性能指标管理
    // ========================================================================

    /**
     * @brief 启用性能指标收集
     * @param enabled true 启用，false 禁用
     * 
     * 必须在 instantiateAll() 之前调用，以便在模块实例化时传递 stats parent
     */
    void enable_metrics(bool enabled = true) {
        _metrics_enabled = enabled;
        if (enabled && !_stats_root) {
            _stats_root = std::make_unique<tlm_stats::StatGroup>("system");
        }
    }

    /**
     * @brief 获取统计根组
     * @return StatGroup* 根组指针，如果 metrics 未启用返回 nullptr
     */
    tlm_stats::StatGroup* stats_root() {
        return _stats_root.get();
    }

    /**
     * @brief 导出所有指标到文件（gem5 风格）
     * @param path 输出文件路径
     */
    void dump_metrics(const std::string& path) {
        if (!_stats_root) return;
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            DPRINTF(MODULE, "[ModuleFactory] Failed to open metrics file: %s\n", path.c_str());
            return;
        }
        ofs << "---------- Begin Simulation Statistics ----------\n";
        _stats_root->dump(ofs);
        ofs << "---------- End Simulation Statistics ----------\n";
        ofs.close();
    }

    /**
     * @brief 重置所有指标
     */
    void reset_metrics() {
        if (_stats_root) {
            _stats_root->reset();
        }
    }

    /**
     * @brief 是否启用指标收集
     */
    bool metrics_enabled() const { return _metrics_enabled; }

    static bool validate_domain_boundary(
        const std::string& src_module,
        const std::string& dst_module,
        const std::string& src_domain,
        const std::string& bridge_name = "");

    static void register_protocol_bridge(const std::string& bridge_name) {
        get_bridges().insert(bridge_name);
    }

#ifdef CPPTLM_TESTING
public:
    // P1 测试辅助: 直接向 instances 添加 (绕过 instantiateAll 的 8 步流程)
    // 接受 unique_ptr 转移动所有权, 与 RAII 设计一致。
    void addInstanceForTesting(const std::string& name, std::unique_ptr<SimObject> obj) {
        instances[name] = std::move(obj);
    }
#endif

private:
    static std::unordered_set<std::string>& get_bridges() {
        static std::unordered_set<std::string> bridges;
        return bridges;
    }
};

// 旧的 parsePortSpec 函数，保留向后兼容性
std::pair<std::string, std::string> parsePortSpec(const std::string& full_name);

#endif // MODULE_FACTORY_HH

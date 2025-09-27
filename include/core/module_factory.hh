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

#include <unordered_map>
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>

using json = nlohmann::json;

class EventQueue;
class SimModule;

// 分离创建函数类型
using CreateSimObjectFunc = std::function<SimObject*(const std::string&, EventQueue*)>;
using CreateSimModuleFunc = std::function<SimModule*(const std::string&, EventQueue*)>;

class ModuleFactory {
private:
    EventQueue* event_queue;
    std::unordered_map<std::string, SimObject*> instances;

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

    // 用于注册普通的 SimObject 类型
    template<typename T>
    static void registerObject(const std::string& name) {
        auto& registry = getObjectRegistry();
        if (registry.find(name) != registry.end()) {
            DPRINTF(MODULE, "[ModuleFactory] Warning: Object type '%s' already registered.\n", name.c_str());
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
            DPRINTF(MODULE, "[ModuleFactory] Warning: Module type '%s' already registered.\n", name.c_str());
        }
        registry[name] = [](const std::string& n, EventQueue* eq) -> SimModule* {
            return new T(n, eq);
        };
    }

    // 保留 unregister/clear/getRegisteredTypes，但需要适配
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
        DPRINTF(MODULE, "[ModuleFactory] Cleared all registered object types.\n");
    }

    static void clearAllModules() {
        getModuleRegistry().clear();
        DPRINTF(MODULE, "[ModuleFactory] Cleared all registered module types.\n");
    }

    static void clearAllTypes() {
        clearAllObjects();
        clearAllModules();
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

    void instantiateAll(const json& config);
    void startAllTicks();

    SimObject* getInstance(const std::string& name) const {
        auto it = instances.find(name);
        return it != instances.end() ? it->second : nullptr;
    }

    const std::unordered_map<std::string, SimObject*>& getAllInstances() const {
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
};

#endif // MODULE_FACTORY_HH

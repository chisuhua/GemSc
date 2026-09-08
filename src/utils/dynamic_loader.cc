// src/utils/dynamic_loader.cc
#include "utils/dynamic_loader.hh"
#include <dlfcn.h>
#include "core/sim_core.hh"

// 在这里定义静态成员变量
std::vector<void*> DynamicLoader::loaded_handles;
std::unordered_set<std::string> DynamicLoader::registered_plugins;

bool DynamicLoader::loadPlugin(const std::string& plugin_path) {
    if (isPluginRegistered(plugin_path)) {
        DPRINTF(LOADER, "[INFO] Plugin already loaded: %s\n", plugin_path.c_str());
        return true;
    }

    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        DPRINTF(LOADER, "[ERROR] Cannot load plugin %s: %s\n", plugin_path.c_str(), dlerror());
        return false;
    }

    loaded_handles.push_back(handle);
    registered_plugins.insert(plugin_path);
    DPRINTF(LOADER, "[INFO] Successfully loaded plugin: %s\n", plugin_path.c_str());
    return true;
}

bool DynamicLoader::isPluginRegistered(const std::string& plugin_path) {
    return registered_plugins.find(plugin_path) != registered_plugins.end();
}

// include/sim_module.hh
#ifndef SIM_MODULE_HH
#define SIM_MODULE_HH

#include "sim_object.hh"
#include "module_factory.hh"
#include <nlohmann/json.hpp>
#include <unordered_map>

class SimObject; // 前向声明
class EventQueue; // 前向声明

// 存储暴露端口配置的结构
struct ExposedPortConfig {
    std::string internal_path;  // 内部端口路径，如"cache.downstream0"
    std::string external_label; // 外部使用的别名，如"cache_request0"
};

class SimModule : public SimObject {
private:
    std::unique_ptr<ModuleFactory> internal_factory;
    json config;

    // 存储外部端口配置
    std::vector<ExposedPortConfig> output_configs;
    std::vector<ExposedPortConfig> input_configs;

    // 内部路径到外部标签的映射，用于快速查找
    std::unordered_map<std::string, std::string> internal_to_external_map;

public:
    explicit SimModule(const std::string& n, EventQueue* eq) 
        : SimObject(n,eq), internal_factory(std::make_unique<ModuleFactory>(eq)) {}

    virtual ~SimModule() = default;

    void instantiate(const json& cfg) {
        config = cfg;
        internal_factory->instantiateAll(config);
        parsePortConfigs(cfg); 
    }

    const std::vector<ExposedPortConfig>& getOutputConfigs() const { return output_configs; }
    const std::vector<ExposedPortConfig>& getInputConfigs() const { return input_configs; }
    
    const ModuleFactory& getInternalFactory() const { return *internal_factory; }

    // 通过名称获取内部实例
    SimObject* getInternalInstance(const std::string& name) {
        return internal_factory->getInstance(name);
    }

    // 获取内部端口实例
    SlavePort* getInternalInputPort(const std::string& internal_path) {
        auto [module_name, port_name] = parsePortSpec(internal_path);
        SimObject* obj = getInternalInstance(module_name);
        if (!obj || !obj->hasPortManager()) return nullptr;
        
        return dynamic_cast<SlavePort*>(obj->getPortManager().getUpstreamPort(port_name));
    }

    MasterPort* getInternalOutputPort(const std::string& internal_path) {
        auto [module_name, port_name] = parsePortSpec(internal_path);
        SimObject* obj = getInternalInstance(module_name);
        if (!obj || !obj->hasPortManager()) return nullptr;
        
        return dynamic_cast<MasterPort*>(obj->getPortManager().getDownstreamPort(port_name));
    }

    // 根据外部标签查找内部路径
    std::string findInternalPath(const std::string& external_label) const {
        for (const auto& config : output_configs) {
            if (config.external_label == external_label) {
                return config.internal_path;
            }
        }
        for (const auto& config : input_configs) {
            if (config.external_label == external_label) {
                return config.internal_path;
            }
        }
        return "";
    }

    // 检查是否为暴露的端口
    bool isExposedPort(const std::string& external_label) const {
        return internal_to_external_map.find(external_label) != internal_to_external_map.end();
    }

private:
    void parsePortConfigs(const json& cfg) {
        // 解析输出端口配置
        if (cfg.contains("outputs")) {
            for (auto& item : cfg["outputs"]) {
                ExposedPortConfig config;
                config.internal_path = item.value("internal", "");
                config.external_label = item.value("external", "");
                
                if (!config.internal_path.empty() && !config.external_label.empty()) {
                    output_configs.push_back(config);
                    internal_to_external_map[config.external_label] = config.internal_path;
                }
            }
        }

        // 解析输入端口配置
        if (cfg.contains("inputs")) {
            for (auto& item : cfg["inputs"]) {
                ExposedPortConfig config;
                config.internal_path = item.value("internal", "");
                config.external_label = item.value("external", "");
                
                if (!config.internal_path.empty() && !config.external_label.empty()) {
                    input_configs.push_back(config);
                    internal_to_external_map[config.external_label] = config.internal_path;
                }
            }
        }
    }

    std::pair<std::string, std::string> parsePortSpec(const std::string& full_name) {
        size_t dot_pos = full_name.find('.');
        if (dot_pos == std::string::npos) {
            return {full_name, ""};
        }
        return {full_name.substr(0, dot_pos), full_name.substr(dot_pos + 1)};
    }

    std::string name;
    EventQueue* event_queue;
};

#endif // SIM_MODULE_HH

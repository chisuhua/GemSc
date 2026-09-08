// include/core/sim_module.hh
// SimModule - 分层模块基类,支持 JSON 多层嵌套 + 限深保护 (v2.2 新增)
//
// 设计意图:
//   - SimModule 持有 internal_factory (ModuleFactory 实例),通过 instantiate(cfg) 解析子模块
//   - simulate_instantiate(cfg) 公共方法用于递归激活子 SimModule,带 depth_ 计数
//   - 限深 MAX_DEPTH=8 默认,可通过 CPPTLM_SIMMODULE_MAX_DEPTH 编译宏覆盖
//   - depth_ 为 thread_local,RAII DepthGuard 保证 throw 路径下计数恢复
//
// 作者: CppTLM Team
// 日期: 2026-06-18
#ifndef SIM_MODULE_HH
#define SIM_MODULE_HH

#include "sim_object.hh"
#include "module_factory.hh"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

class SimObject; // 前向声明
class EventQueue; // 前向声明

// 存储暴露端口配置的结构
struct ExposedPortConfig {
    std::string internal_path;  // 内部端口路径,如"cache.downstream0"
    std::string external_label; // 外部使用的别名,如"cache_request0"
};

class SimModule : public SimObject {
private:
    // ========================
    // 嵌套深度限制 (v2.2 新增)
    // ========================
    // 默认 MAX_DEPTH = 8;通过编译宏 CPPTLM_SIMMODULE_MAX_DEPTH 覆盖
#ifndef CPPTLM_SIMMODULE_MAX_DEPTH
    static constexpr int MAX_DEPTH = 8;
#else
    static constexpr int MAX_DEPTH = (CPPTLM_SIMMODULE_MAX_DEPTH);
#endif

    // thread_local 计数器:多线程仿真间相互隔离
    static thread_local int depth_;

    // RAII 守卫:作用域结束时 --depth_,包括 throw 路径(异常安全)
    struct DepthGuard {
        ~DepthGuard() noexcept { --depth_; }
    };

protected:
    // 子类需要访问 internal_factory 以遍历子模块 (CpuCluster::tick 等)
    std::unique_ptr<ModuleFactory> internal_factory;

private:
    json config;

    // 存储外部端口配置
    std::vector<ExposedPortConfig> output_configs;
    std::vector<ExposedPortConfig> input_configs;

    // 内部路径到外部标签的映射,用于快速查找
    std::unordered_map<std::string, std::string> internal_to_external_map;

public:
    explicit SimModule(const std::string& n, EventQueue* eq)
        : SimObject(n,eq), internal_factory(std::make_unique<ModuleFactory>(eq)) {}

    virtual ~SimModule() = default;

    // ========================
    // 构造期入口 (向后兼容): 委托给 simulate_instantiate
    // ========================
    void instantiate(const json& cfg) {
        simulate_instantiate(cfg);
    }

    // ========================
    // 模拟激活入口 (v2.2 新增): 幂等守卫 + 限深检查 + 递归激活子 SimModule
    // ========================
    virtual void simulate_instantiate(const json& cfg) {
        // 幂等守卫:已 populate 则不重复构造 (只触发子 SimModule 递归激活)
        if (internal_factory && !internal_factory->getAllInstances().empty()) {
            return;
        }
        ++depth_;
        DepthGuard guard{};  // 析构时 --depth_,即使 throw 也执行
        if (depth_ > MAX_DEPTH) {
            throw std::runtime_error(
                "SimModule::simulate_instantiate depth limit "
                + std::to_string(MAX_DEPTH) + " exceeded at current depth "
                + std::to_string(depth_) + " (path: " + buildDepthPath() + ")");
        }
        config = cfg;
        if (!internal_factory->instantiateAll(config)) {
            DPRINTF(MODULE, "[ERROR] SimModule '%s' instantiation failed: schema validation error\n", name.c_str());
        }
        parsePortConfigs(cfg);
        // 递归激活内部 SimModule 子模块
        if (cfg.contains("modules")) {
            for (auto& child_cfg : cfg["modules"]) {
                if (!child_cfg.contains("name")) continue;
                auto* child = internal_factory->getInstance(child_cfg["name"]);
                if (auto* sub = dynamic_cast<SimModule*>(child)) {
                    sub->simulate_instantiate(child_cfg);
                }
            }
        }
    }

    const std::vector<ExposedPortConfig>& getOutputConfigs() const { return output_configs; }
    const std::vector<ExposedPortConfig>& getInputConfigs() const { return input_configs; }

    const ModuleFactory& getInternalFactory() const { return *internal_factory; }

    // ========================
    // 测试用 public getter (Stage 3 testing API)
    // 用途: 验证 RAII DepthGuard 在抛错路径下能正确恢复 depth_ == 0
    // thread_local 读取, 与 simulate_instantiate 调用同线程
    // ========================
    static int getCurrentDepth() { return depth_; }

    // 暴露 MAX_DEPTH 供测试断言 (编译期常量)
    static constexpr int getMaxDepth() { return MAX_DEPTH; }

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

    // 根据外部标签查找内部路径 (P1 修复 D.4: 递归到子 SimModule)
    std::string findInternalPath(const std::string& external_label) const {
        // 先查当前层
        auto it = internal_to_external_map.find(external_label);
        if (it != internal_to_external_map.end()) return it->second;
        // 递归到子 SimModule (返回 "<子模块名>.<内部路径>")
        for (const auto& kv : internal_factory->getAllInstances()) {
            if (auto* sub = dynamic_cast<SimModule*>(kv.second.get())) {
                std::string sub_path = sub->findInternalPath(external_label);
                if (!sub_path.empty()) return kv.first + "." + sub_path;
            }
        }
        return "";
    }

    // P1 Fix D.5: 默认递归 tick - 遍历所有内部子模块
    // 向后兼容: CpuCluster::tick 显式 override 保留, 不影响现有行为
    virtual void tick() override {
        for (const auto& kv : internal_factory->getAllInstances()) {
            if (kv.second) kv.second->tick();
        }
    }

    // P5: 父对象在挂载后调用 - 默认递归到子模块
    // 灵感: gem5 AbstractCacheHierarchy.incorporate_cache(board)
    // 用于 ApuSoC 等顶层容器在 JSON 实例化完成后, 通知子模块"父对象已就位",
    // 子模块可借此建立跨域 wiring (如 CPU cluster 连接 GPU 端 CoherentXBar)。
    virtual void incorporate_parent(SimModule* parent) {
        for (const auto& kv : internal_factory->getAllInstances()) {
            if (auto* sub = dynamic_cast<SimModule*>(kv.second.get())) {
                sub->incorporate_parent(this);
            }
        }
    }

#ifdef CPPTLM_TESTING
public:
    // P1 测试辅助: 直接向 internal_factory 添加实例 (绕过 instantiateAll 的 8 步流程)
    // 所有权转移: 包装 unique_ptr 后传给 factory, caller 不应再 delete obj。
    void addInternalInstance(SimObject* obj) {
        // 实际字段: instances (无下划线, 见 module_factory.hh:37)
        internal_factory->addInstanceForTesting(obj->getName(),
                                               std::unique_ptr<SimObject>(obj));
    }
    // P1 测试辅助: 直接添加 outputs 暴露端口配置 (绕过 parsePortConfigs JSON 路径)
    void addOutputConfig(const std::string& internal, const std::string& external) {
        output_configs.push_back({internal, external});
        internal_to_external_map[external] = internal;
    }
    // P1 测试辅助: 设置 SimObject::name 字段 (用于 rename cu0_proxy -> cu0)
    void setName(const std::string& n) { name = n; }  // SimObject 字段名 (无下划线)
#endif

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

    // 构造调试用深度路径字符串 (Stage 1 简化版:返回嵌套深度计数)
    std::string buildDepthPath() const {
        // 简化版:返回当前深度计数;Stage 3 可扩展为实际 JSON 路径追踪
        std::string path = "modules";
        for (int i = 0; i < depth_; ++i) {
            path += "[0]";
            if (i + 1 < depth_) path += ".";
        }
        return path;
    }
};

// thread_local 静态成员定义
// 注意:SimModule 是头文件,此定义需要 inline 防止多重定义 (C++17)
inline thread_local int SimModule::depth_ = 0;

#endif // SIM_MODULE_HH
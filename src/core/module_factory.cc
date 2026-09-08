/**
 * @file module_factory.cc
 * @brief ModuleFactory 核心实现 - 双注册表、实例化、拓扑连接、StreamAdapter 注入
 *
 * 本文件实现 ModuleFactory 类的核心流程，负责：
 * - **双注册表管理**：getObjectRegistry() / getModuleRegistry() 分离对象与模块注册
 * - **JSON 拓扑实例化**：instantiateAll() 从配置创建所有模块实例
 * - **端口连接解析**：resolveConnections() 处理 "module.port_index" 语法
 * - **StreamAdapter 注入**：Step 7 为 ChStreamModuleBase 模块注入适配器
 *
 * ## 核心流程（8 步）
 * 1. registerAllObjects()    → 从 REGISTRY 注入所有 SimObject
 * 2. registerAllModules()    → 从 REGISTRY 注入所有 SimModule
 * 3. loadPlugins()           → dlopen 动态加载 .so 插件
 * 4. instantiateAll()        → 从 JSON 配置实例化所有模块
 * 5. resolveConnections()    → 根据 connections 数组连接端口
 * 6. loadGroupTopology()     → 处理 module_groups 层级拓扑
 * 7. injectStreamAdapters()  → 为 ChStreamModuleBase 创建并注入 StreamAdapter
 * 8. startAllTicks()         → 启动所有模块 tick() 循环
 *
 * ## 关键 API
 * - `instantiateAll(config)` — 主入口，执行完整实例化流程
 * - `parsePortSpec(name)` — 解析 "xbar.0" → ("xbar", 0)
 * - `injectStreamAdapters()` — Step 7，创建 ChStreamAdapterFactory 适配器
 * - `stream_adapters_` — vector<unique_ptr<StreamAdapterBase>> 适配器生命周期管理
 *
 * ## 使用注意事项
 * - **显式源文件**：CMakeLists.txt 使用 set(CORE_SOURCES ...) 显式列举，禁用 GLOB
 * - **端口索引语法**：JSON 连接支持 "dst": "xbar.0" 表示模块 xbar 的第 0 端口
 * - **StreamAdapter 生命周期**：由 ModuleFactory::stream_adapters_ 管理，模块析构前自动清理
 * - **插件加载**：loadPlugins() 使用 dlopen/dlsym，需确保 .so 符合 REGISTER_OBJECT 约定
 *
 * @author CppTLM Team
 * @date 2024-05
 * @see module_factory.hh
 * @see core/connection_resolver.hh
 * @see framework/stream_adapter.hh
 */

#include "module_factory.hh"
#include <algorithm>
#include <fstream>
#include <set>
#include "bundles/cache_bundles_tlm.hh"
#include "bundles/noc_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "core/chstream_port.hh"
#include "core/coherence_domain.hh"
#include "core/connection_resolver.hh"
#include "core/load_policy.hh"
#include "core/param_errors.hh"
#include "core/param_parser.hh"
#include "core/plugin_load_exception.hh"
#include "core/plugin_loader.hh"
#include "core/port_compatibility.hh"
#include "core/port_types.hh"
#include "core/topology_parser.hh"
#include "framework/chstream_adapter_factory.hh"
#include "framework/stream_adapter.hh"
#include "metrics/stats_manager.hh"
#include "sim_module.hh"
#include "tlm/router_tlm.hh"
#include "utils/config_utils.hh"
#include "utils/json_includer.hh"
#include "utils/module_group.hh"
#include "utils/regex_matcher.hh"
#include "utils/var_resolver.hh"
#include "utils/wildcard.hh"

using json = nlohmann::json;

json processExtends(const json& config, int depth = 0);
bool validate_nic_pe_connection(const std::string& src_type, unsigned src_port,
                                const std::string& dst_type, unsigned dst_port);
bool check_port_compatibility(const std::string& src_name, const std::string& dst_name,
                              unsigned src_idx, unsigned dst_idx,
                              const std::map<std::string, cpptlm::ModulePortSpec>& port_specs);
std::map<std::string, cpptlm::ModulePortSpec> get_default_port_specs();

// P3.x ASan: clear StatsManager paths, then erase own entries from
// ModuleGroup global registry (so cascade destruction does not see
// stale pointers), then delete own SimObject* instances in a
// second-pass vector to avoid iterator invalidation when SimModule's
// internal_factory sub-factory re-enters ModuleGroup::clearAll() on
// its own destruction. Each factory owns its own SimObjects; the
// global ModuleGroup is just a name->pointer lookup, not an
// ownership structure.
ModuleFactory::~ModuleFactory() {
    for (auto& [name, obj_ptr] : instances) {
        if (!obj_ptr)
            continue;
        if (auto* ch_mod = dynamic_cast<ChStreamModuleBase*>(obj_ptr.get())) {
            if (auto* sg = ch_mod->get_stats_group()) {
                std::vector<std::string> paths;
                for (const auto& kv : tlm_stats::StatsManager::instance().groups()) {
                    if (kv.second == sg)
                        paths.push_back(kv.first);
                }
                for (const auto& p : paths) {
                    tlm_stats::StatsManager::instance().unregister_group(p);
                }
            }
        }
    }
    // RAII: instances.clear() 触发 unique_ptr 析构 → SimObject 自动 delete,
    // 不再需要 to_release + 手动 delete 循环。
    // ModuleGroup::eraseInstance 必须在 SimObject delete 之前完成, 否则
    // ModuleGroup::clearAllGroups 会在 cascade destruction 中撞到 stale pointer。
    std::vector<std::string> names;
    names.reserve(instances.size());
    for (const auto& [name, _] : instances) {
        names.push_back(name);
    }
    instances.clear();
    for (const auto& name : names) {
        ModuleGroup::eraseInstance(name);
    }
    ModuleGroup::clearAllGroups();
}

std::pair<std::string, std::string> parsePortSpec(const std::string& full_name) {
    size_t dot_pos = full_name.find('.');
    if (dot_pos == std::string::npos) {
        return {full_name, ""};
    }
    return {full_name.substr(0, dot_pos), full_name.substr(dot_pos + 1)};
}

// Phase 3.2: Resolve port alias (T3.2-07)
// Handles deprecated port names like NORTH/EAST/SOUTH/WEST/LOCAL
static std::string resolve_port_alias(const std::string& port_spec) {
    auto it = cpptlm::PortSpec::deprecated_names().find(port_spec);
    if (it != cpptlm::PortSpec::deprecated_names().end()) {
        DPRINTF(CONN, "[WARN] Deprecated port name '%s' resolved to index %u\n", port_spec.c_str(),
                it->second);
        return std::to_string(it->second);
    }
    return port_spec;
}

bool ModuleFactory::instantiateAll(const json& config) {
    json extended_config = processExtends(config);
    if (extended_config.is_object() && extended_config.empty()) {
        DPRINTF(MODULE, "[CONFIG ERROR] extends processing failed\n");
        return false;
    }
    json final_config = JsonIncluder::loadAndInclude(extended_config);

    // ========================
    // 1. 解析变量引用 ${path}
    // ========================
    cpptlm::VarResolver var_resolver(final_config);
    final_config = var_resolver.resolveAll(final_config);

    // ========================
    // 0. JSON Schema 验证（CFG-08）
    // ========================
    if (!validateConfig(final_config)) {
        DPRINTF(MODULE, "[CONFIG ERROR] Schema validation failed, aborting instantiation\n");
        return false;
    }

    // ========================
    // 0.5 Hierarchy 树解析 (TGMS v4.0 Phase 4.1)
    // ========================
    if (final_config.contains("hierarchy")) {
        DPRINTF(MODULE, "[HIERARCHY] Parsing hierarchy tree...\n");
        try {
            auto hierarchy_root = cpptlm::parse_hierarchy_tree_with_validation(
                final_config["hierarchy"], final_config.contains("coherence_domains")
                                               ? final_config["coherence_domains"]
                                               : json::array());
            (void)hierarchy_root;
            DPRINTF(MODULE, "[HIERARCHY] Hierarchy tree parsed successfully\n");
        } catch (const cpptlm::TopologyParseException& e) {
            DPRINTF(MODULE, "[HIERARCHY ERROR] %s\n", e.what());
            return false;
        }
    } else {
        DPRINTF(MODULE, "[HIERARCHY] No hierarchy section found in config\n");
    }

    for (auto& mod : final_config["modules"]) {
        std::string type = mod["type"];
        if (mod.contains("params") && type == "RouterTLM") {
            auto rules = tlm::RouterTLM::get_param_rules();
            for (const auto& [name, rule] : rules) {
                if (!mod["params"].contains(name.c_str())) {
                    if (rule.required) {
                        DPRINTF(MODULE,
                                "[PARAM ERROR] Required parameter '%s' missing for module '%s'\n",
                                name.c_str(), type.c_str());
                        return false;
                    }
                    if (rule.default_int.has_value()) {
                        mod["params"][name] = rule.default_int.value();
                    }
                }
            }
        }
    }

    // ========================
    // 1.5 解析 port_specs (Phase 3.2: T3.2-04)
    // ========================
    std::map<std::string, cpptlm::ModulePortSpec> port_specs;
    for (const auto& mod : final_config["modules"]) {
        if (!mod.contains("name") || !mod.contains("type"))
            continue;
        std::string name = mod["name"];
        if (mod.contains("port_spec")) {
            try {
                auto spec = mod["port_spec"].get<cpptlm::ModulePortSpec>();
                port_specs[name] = spec;
            } catch (const std::exception& e) {
                DPRINTF(MODULE, "[WARN] Failed to parse port_spec for module '%s': %s\n",
                        name.c_str(), e.what());
            }
        }
    }

    auto default_specs = get_default_port_specs();
    for (const auto& mod : final_config["modules"]) {
        if (!mod.contains("name") || !mod.contains("type"))
            continue;
        std::string name = mod["name"];
        std::string type = mod["type"];
        if (port_specs.find(name) == port_specs.end()) {
            if (default_specs.find(type) != default_specs.end()) {
                port_specs[name] = default_specs[type];
                DPRINTF(MODULE, "[PORT INFO] Using default port spec for module '%s' (type: %s)\n",
                        name.c_str(), type.c_str());
            }
        }
    }

    // 使用 PluginLoader 加载所有插件
    PluginLoader loader;
    if (final_config.contains("plugin")) {
        for (auto& plugin_path : final_config["plugin"]) {
            if (!PluginLoader{}.loadPlugin(plugin_path.get<std::string>(),
                                           LoadPolicy::CRITICAL_ONLY, true)) {
                DPRINTF(MODULE, "[ERROR] Failed to load plugin: %s\n",
                        plugin_path.get<std::string>().c_str());
            }
        }
    }

    // ========================
    // 2. 创建所有模块实例
    // ========================
    // RAII: unique_ptr 持有所有权, 防 Step 4.5 异常路径 (line 379-382 return false)
    // 跳过 line 830 transfer 时 local map 析构 → SimObject 孤儿泄漏。
    // 与 member `instances` 类型一致, line 830 `instances = object_instances`
    // 走 move-assign 即可。
    std::unordered_map<std::string, std::unique_ptr<SimObject>> object_instances;
    std::unordered_map<std::string, SimModule*> module_instances;

    for (auto& mod : final_config["modules"]) {
        if (!mod.contains("name") || !mod.contains("type"))
            continue;
        std::string name = mod["name"];
        std::string type = mod["type"];

        // 尝试在 SimModule 注册表中查找
        auto& module_registry = ModuleFactory::getModuleRegistry();
        auto module_it = module_registry.find(type);
        if (module_it != module_registry.end()) {
            // 这是一个 SimModule。RAII: unique_ptr 持有所有权, module_instances
            // 保存 raw 视图 (非所有权, 用于 Step 4.5/incorporate_parent 迭代)。
            auto new_module =
                std::unique_ptr<SimModule>(module_it->second(name, event_queue));
            module_instances[name] = new_module.get();
            object_instances[name] = std::move(new_module);
        } else {
            // 在 SimObject 注册表中查找
            auto& object_registry = ModuleFactory::getObjectRegistry();
            auto object_it = object_registry.find(type);
            if (object_it != object_registry.end()) {
                object_instances[name] =
                    std::unique_ptr<SimObject>(object_it->second(name, event_queue));
            } else {
                DPRINTF(MODULE, "[ERROR] Unknown or unregistered type: %s\n", type.c_str());
            }
        }

        // 处理 layout
        if (mod.contains("layout")) {
            auto& l = mod["layout"];
            double x = l.value("x", -1);
            double y = l.value("y", -1);
            if (x >= 0 && y >= 0) {
                auto it = object_instances.find(name);
                if (it != object_instances.end() && it->second) {
                    // unique_ptr::operator-> 返回 raw pointer, setLayout 直接调用
                    it->second->setLayout(x, y);
                }
            }
        }

        const json* cfg_src = mod.contains("params") ? &mod["params"] : nullptr;
        if (!cfg_src && type == "RouterTLM" && mod.contains("node_x"))
            cfg_src = &mod;
        if (!cfg_src && type == "NICTLM" && mod.contains("node_id"))
            cfg_src = &mod;
        if (cfg_src) {
            auto it = object_instances.find(name);
            if (it != object_instances.end() && it->second) {
                it->second->set_config(*cfg_src);
                it->second->on_config_loaded();
                DPRINTF(MODULE, "[CONFIG] Set params for module: %s\n", name.c_str());
            }
        }

        // 注册实例到 ModuleGroup（供通配符展开使用）——使用 find 避免 operator[]
        // 在类型未注册时隐式插入 nullptr 条目。.get() 转 raw pointer 给 registerInstance。
        auto reg_it = object_instances.find(name);
        if (reg_it != object_instances.end() && reg_it->second) {
            ModuleGroup::registerInstance(name, reg_it->second.get());
        }
    }

    // ========================
    // 3. 解析 groups
    // ========================
    if (final_config.contains("groups")) {
        for (auto& [group_name, members] : final_config["groups"].items()) {
            std::vector<std::string> member_list;
            for (auto& m : members) {
                member_list.push_back(m.get<std::string>());
            }
            ModuleGroup::define(group_name, member_list);
        }
    }

    // ========================
    // 4. 实例化 SimModule 内部配置
    // ========================
    for (auto& mod : final_config["modules"]) {
        if (mod.contains("config")) {
            // 防御性类型检查: config 字段必须是 string (file path)。
            // 如果是 object/array, 用户大概率是把参数 dict 错放在 config 字段
            // (canonical 字段名是 params, 见 openspec/changes/field-name-unification)。
            if (!mod["config"].is_string()) {
                DPRINTF(MODULE,
                        "[CONFIG WARN] module '%s' has 'config' field that is %s; "
                        "expected string (file path). Did you mean 'params' for "
                        "module configuration? Use 'params' field instead.\n",
                        mod["name"].get<std::string>().c_str(), mod["config"].type_name());
                continue;
            }
            std::string name = mod["name"];
            auto* sim_mod = module_instances[name];
            if (sim_mod) {
                std::string config_file = mod["config"];
                std::ifstream f(config_file);
                if (f.is_open()) {
                    json internal_cfg = json::parse(f);
                    sim_mod->instantiate(internal_cfg);
                } else {
                    DPRINTF(MODULE, "[ERROR] Cannot open config: %s\n", config_file.c_str());
                }
            }
        }
    }

    // ========================
    // 4.5. 触发顶层 SimModule 的 simulate_instantiate（处理 inline `modules` 嵌套）
    // ========================
    // Per design.md Decision 1: 顶层 instantiateAll 构造完顶层 SimObject 后,
    // 对每个顶层 SimModule 派生类调 simulate_instantiate(top_cfg),
    // 触发内部子模块 instantiateAll + 递归激活嵌套的孙子 SimModule.
    // 内部递归由 SimModule::simulate_instantiate 自管理(1 次 RTTI / 子模块).
    for (auto& mod : final_config["modules"]) {
        if (!mod.contains("name") || !mod.contains("type"))
            continue;
        std::string name = mod["name"];
        auto sim_it = module_instances.find(name);
        if (sim_it == module_instances.end())
            continue;
        SimModule* sim = sim_it->second;
        if (!sim)
            continue;
        try {
            sim->simulate_instantiate(mod);
        } catch (const std::exception& e) {
            DPRINTF(MODULE, "[ERROR] simulate_instantiate failed for '%s': %s\n", name.c_str(),
                    e.what());
            return false;
        }
    }

    // ========================
    // 5. 使用 ConnectionResolver 处理 connections
    // ========================

    // DEF-02: 在 ConnectionResolver 之前去重 connections
    json deduplicated_connections = json::array();
    std::set<std::string> seen_connections;
    std::map<std::string, int> connection_latencies;
    for (const auto& conn : final_config["connections"]) {
        if (!conn.contains("src") || !conn.contains("dst"))
            continue;
        std::string conn_key =
            conn["src"].get<std::string>() + "->" + conn["dst"].get<std::string>();
        if (seen_connections.count(conn_key)) {
            int existing_latency = connection_latencies[conn_key];
            int this_latency = conn.value("latency", 0);
            if (this_latency != existing_latency) {
                DPRINTF(CONN,
                        "[WARN] Duplicate connection %s has conflicting latency (first=%d, "
                        "this=%d) - using first\n",
                        conn_key.c_str(), existing_latency, this_latency);
            } else {
                DPRINTF(CONN, "[CONN] Skipped duplicate connection at resolver stage: %s\n",
                        conn_key.c_str());
            }
            continue;
        }
        seen_connections.insert(conn_key);
        connection_latencies[conn_key] = conn.value("latency", 0);
        deduplicated_connections.push_back(conn);
    }

    ConnectionResolver resolver;

    // 简化的端口创建函数
    auto createPortFunc = [&object_instances](const std::string& owner, const std::string& port,
                                              size_t buffer_size, bool is_upstream) -> bool {
        auto it = object_instances.find(owner);
        if (it != object_instances.end() && it->second && it->second->hasPortManager()) {
            auto& pm = it->second->getPortManager();
            SimObject* raw_obj = it->second.get();
            if (is_upstream) {
                pm.addUpstreamPort(raw_obj, {buffer_size}, {}, port);
            } else {
                pm.addDownstreamPort(raw_obj, {buffer_size}, {}, port);
            }
            return true;
        }
        return false;
    };

    auto port_creations =
        resolver.resolveConnections(deduplicated_connections, module_instances, createPortFunc);

    // 创建端口
    for (const auto& info : port_creations) {
        auto it = object_instances.find(info.owner_name);
        if (it != object_instances.end() && it->second && it->second->hasPortManager()) {
            auto& pm = it->second->getPortManager();
            SimObject* raw_obj = it->second.get();

            if (info.is_upstream) {
                auto* port = pm.addUpstreamPort(raw_obj, info.buffer_sizes, info.priorities,
                                                info.port_name);
                if (port)
                    port->setDelay(info.latency);
            } else {
                auto* port = pm.addDownstreamPort(raw_obj, info.buffer_sizes, info.priorities,
                                                  info.port_name);
                if (port)
                    port->setDelay(info.latency);
            }
        }
    }

    // ========================
    // 6. 建立连接
    // ========================
    std::unordered_map<std::string, size_t> src_indices;
    std::unordered_map<std::string, size_t> dst_indices;
    std::set<std::pair<std::string, std::string>> processed_connections;

    for (auto& conn : deduplicated_connections) {
        if (!conn.contains("src") || !conn.contains("dst"))
            continue;

        std::string src_spec = conn["src"];
        std::string dst_spec = conn["dst"];
        int latency = conn.value("latency", 0);
        json exclude_list = conn.value("exclude", json::array());

        std::vector<std::string> src_names, dst_names;

        // 处理通配符和组连接
        if (ModuleGroup::isGroupReference(src_spec)) {
            src_names = ModuleGroup::resolve(src_spec);
        } else if (RegexMatcher::isRegexPattern(src_spec) || Wildcard::match("*", src_spec)) {
            for (auto& [name, obj] : object_instances) {
                if (RegexMatcher::match(src_spec, name)) {
                    src_names.push_back(name);
                }
            }
        } else {
            src_names.push_back(src_spec);
        }

        if (ModuleGroup::isGroupReference(dst_spec)) {
            dst_names = ModuleGroup::resolve(dst_spec);
        } else if (RegexMatcher::isRegexPattern(dst_spec) || Wildcard::match("*", dst_spec)) {
            for (auto& [name, obj] : object_instances) {
                if (RegexMatcher::match(dst_spec, name)) {
                    dst_names.push_back(name);
                }
            }
        } else {
            dst_names.push_back(dst_spec);
        }

        src_names = filterExcluded(src_names, exclude_list);
        dst_names = filterExcluded(dst_names, exclude_list);

        for (const std::string& src_full : src_names) {
            auto [src_module_name, src_port_name] = parsePortSpec(src_full);

            MasterPort* src_port = nullptr;
            if (auto mod_it = module_instances.find(src_module_name);
                mod_it != module_instances.end() && !src_port_name.empty()) {
                std::string internal_path = mod_it->second->findInternalPath(src_port_name);
                if (!internal_path.empty()) {
                    auto [internal_owner, internal_port] = parsePortSpec(internal_path);
                    auto obj_it = object_instances.find(internal_owner);
                    if (obj_it != object_instances.end() && obj_it->second &&
                        obj_it->second->hasPortManager()) {
                        src_port = dynamic_cast<MasterPort*>(
                            obj_it->second->getPortManager().getDownstreamPort(internal_port));
                    }
                }
            } else if (!src_port_name.empty()) {
                if (auto obj_it = object_instances.find(src_module_name);
                    obj_it != object_instances.end() && obj_it->second) {
                    src_port = dynamic_cast<MasterPort*>(
                        obj_it->second->getPortManager().getDownstreamPort(src_port_name));
                }
            } else if (auto obj_it = object_instances.find(src_module_name);
                       obj_it != object_instances.end() && obj_it->second) {
                // Wildcard/group expansion: create default downstream port
                src_port = obj_it->second->getPortManager().addDownstreamPort(obj_it->second.get(), {4},
                                                                              {}, src_module_name);
            }

            for (const std::string& dst_full : dst_names) {
                auto [dst_module_name, dst_port_name] = parsePortSpec(dst_full);

                SlavePort* dst_port = nullptr;
                if (auto mod_it = module_instances.find(dst_module_name);
                    mod_it != module_instances.end() && !dst_port_name.empty()) {
                    std::string internal_path = mod_it->second->findInternalPath(dst_port_name);
                    if (!internal_path.empty()) {
                        auto [internal_owner, internal_port] = parsePortSpec(internal_path);
                        auto obj_it = object_instances.find(internal_owner);
                        if (obj_it != object_instances.end() && obj_it->second &&
                            obj_it->second->hasPortManager()) {
                            dst_port = dynamic_cast<SlavePort*>(
                                obj_it->second->getPortManager().getUpstreamPort(internal_port));
                        }
                    }
                } else if (!dst_port_name.empty()) {
                    if (auto obj_it = object_instances.find(dst_module_name);
                        obj_it != object_instances.end() && obj_it->second) {
                        dst_port = dynamic_cast<SlavePort*>(
                            obj_it->second->getPortManager().getUpstreamPort(dst_port_name));
                    }
                } else if (auto obj_it = object_instances.find(dst_module_name);
                           obj_it != object_instances.end() && obj_it->second) {
                    // Wildcard/group expansion: create default upstream port
                    dst_port = obj_it->second->getPortManager().addUpstreamPort(
                        obj_it->second.get(), {4}, {}, dst_module_name);
                }

                if (src_port && dst_port) {
                    auto conn_key = std::make_pair(src_full, dst_full);
                    if (processed_connections.count(conn_key)) {
                        DPRINTF(CONN, "[CONN] Skipped duplicate connection %s -> %s\n",
                                src_full.c_str(), dst_full.c_str());
                    } else {
                        processed_connections.insert(conn_key);
                        port_pairs_.push_back(std::make_unique<PortPair>(src_port, dst_port));
                        src_port->setDelay(latency);
                        DPRINTF(CONN, "[CONN] Connected %s -> %s (latency=%d)\n", src_full.c_str(),
                                dst_full.c_str(), latency);
                    }
                } else if (!src_port) {
                    DPRINTF(CONN, "[WARN] Source port not found: %s\n", src_full.c_str());
                } else if (!dst_port) {
                    DPRINTF(CONN, "[WARN] Destination port not found: %s\n", dst_full.c_str());
                }
            }
        }
    }

    // ========================
    // 7. 为 ChStream 模块注入 StreamAdapter（多端口感知）
    // ========================
    // 7a. 为每个 ChStream 模块创建适配器和端口
    using ChStreamInitiatorPtr = cpptlm::ChStreamInitiatorPort*;
    using ChStreamTargetPtr = cpptlm::ChStreamTargetPort*;
    std::unordered_map<std::string, cpptlm::StreamAdapterBase*> ch_adapters;
    std::unordered_map<std::string, std::vector<ChStreamInitiatorPtr>> ch_req_out;
    std::unordered_map<std::string, std::vector<ChStreamTargetPtr>> ch_resp_in;
    std::unordered_map<std::string, std::vector<ChStreamTargetPtr>> ch_req_in;
    std::unordered_map<std::string, std::vector<ChStreamInitiatorPtr>> ch_resp_out;

    auto& factory = ChStreamAdapterFactory::get();
    std::unordered_map<std::string, std::string> module_types;
    for (auto& mod : final_config["modules"]) {
        if (mod.contains("name") && mod.contains("type"))
            module_types[mod["name"]] = mod["type"];
    }

    for (auto& [name, obj_ptr] : object_instances) {
        if (!obj_ptr)
            continue;
        auto* ch_mod = dynamic_cast<ChStreamModuleBase*>(obj_ptr.get());
        if (!ch_mod)
            continue;

        const std::string& type = module_types[name];
        bool is_multi = factory.isMultiPort(type);
        bool is_dual = factory.isDualPort(type);
        unsigned n_ports = is_multi || is_dual ? factory.getPortCount(type) : 1;

        if (!factory.knows(type)) {
            DPRINTF(MODULE, "[ERROR] No adapter factory for ChStream type: %s (%s)\n", type.c_str(),
                    name.c_str());
            continue;
        }

        auto adapter = factory.create(type, obj_ptr.get());
        if (!adapter) {
            DPRINTF(MODULE, "[ERROR] Failed to create adapter for %s (type: %s)\n", name.c_str(),
                    type.c_str());
            continue;
        }

        // 创建 N 组端口
        auto& req_out_vec = ch_req_out[name];
        auto& resp_in_vec = ch_resp_in[name];
        auto& req_in_vec = ch_req_in[name];
        auto& resp_out_vec = ch_resp_out[name];
        req_out_vec.resize(n_ports);
        resp_in_vec.resize(n_ports);
        req_in_vec.resize(n_ports);
        resp_out_vec.resize(n_ports);

        for (unsigned i = 0; i < n_ports; i++) {
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "[%u]", i);

            req_out_vec[i] = new cpptlm::ChStreamInitiatorPort(
                name + ".req_out" + (n_ports > 1 ? suffix : ""), event_queue);
            resp_in_vec[i] = new cpptlm::ChStreamTargetPort(
                name + ".resp_in" + (n_ports > 1 ? suffix : ""), adapter, event_queue, i);
            req_in_vec[i] = new cpptlm::ChStreamTargetPort(
                name + ".req_in" + (n_ports > 1 ? suffix : ""), adapter, event_queue, i);
            resp_out_vec[i] = new cpptlm::ChStreamInitiatorPort(
                name + ".resp_out" + (n_ports > 1 ? suffix : ""), event_queue);

            ch_initiator_ports_.emplace_back(req_out_vec[i]);
            ch_target_ports_.emplace_back(resp_in_vec[i]);
            ch_target_ports_.emplace_back(req_in_vec[i]);
            ch_initiator_ports_.emplace_back(resp_out_vec[i]);

            // P0 D.1 fix: 让 SimModule::getInternalOutputPort 能查到 ChStream 端口
            // ch_mod 已是 dynamic_cast<ChStreamModuleBase*> 结果, 这里 cast 回 SimObject*
            // 因 ChStreamModuleBase : public SimObject
            if (auto* ch_obj = dynamic_cast<SimObject*>(ch_mod); ch_obj) {
                // 注意: 单端口模块的 port_manager 尚未创建, 直接调用 getPortManager()
                // 触发 lazy-create; 多端口模块首次 mirror 后已存在, 后续调用复用同一实例.
                auto& pm = ch_obj->getPortManager();
                std::string suffix =
                    (n_ports > 1) ? (std::string("[") + std::to_string(i) + "]") : std::string("");
                pm.mirrorExistingDownstreamPort("req_out" + suffix, req_out_vec[i]);
                pm.mirrorExistingUpstreamPort("resp_in" + suffix, resp_in_vec[i]);
                pm.mirrorExistingUpstreamPort("req_in" + suffix, req_in_vec[i]);
                pm.mirrorExistingDownstreamPort("resp_out" + suffix, resp_out_vec[i]);
            }
        }

        // 注入 StreamAdapter（区分单端口 / 多端口 / 双端口）
        if (is_dual) {
            // 双端口非对称：组 0 = PE 侧，组 1 = Network 侧
            auto* dual = static_cast<cpptlm::DualPortStreamAdapter<
                ChStreamModuleBase, bundles::CacheReqBundle, bundles::CacheRespBundle,
                bundles::NoCFlitBundle, bundles::NoCFlitBundle>*>(adapter);
            if (dual) {
                dual->bind_pe_ports(req_out_vec[0], resp_in_vec[0], resp_out_vec[0], req_in_vec[0]);
                dual->bind_net_ports(req_out_vec[1], resp_in_vec[1], resp_out_vec[1],
                                     req_in_vec[1]);
            }
            ch_mod->set_stream_adapter(adapter);
            DPRINTF(MODULE, "[ChStream] Created DualPort adapter for %s (type: %s, PE+Net)\n",
                    name.c_str(), type.c_str());
        } else if (is_multi) {
            for (unsigned i = 0; i < n_ports; i++) {
                adapter->bind_port_pair(i, req_out_vec[i], resp_in_vec[i], resp_out_vec[i],
                                        req_in_vec[i]);
            }
            ch_mod->set_stream_adapter(adapter);
            DPRINTF(MODULE, "[ChStream] Created multi-port adapter for %s (%u ports, type: %s)\n",
                    name.c_str(), n_ports, type.c_str());
        } else {
            adapter->bind_ports(req_out_vec[0], resp_in_vec[0], resp_out_vec[0], req_in_vec[0]);
            ch_mod->set_stream_adapter(adapter);
            DPRINTF(MODULE, "[ChStream] Created SinglePort adapter for %s (type: %s)\n",
                    name.c_str(), type.c_str());
        }

        ch_adapters[name] = adapter;
        stream_adapters_.emplace_back(adapter);
    }

    // ========================
    // 8. 自动注册 StatGroup 到 StatsManager（Phase 0 修复）
    // ========================
    for (auto& [name, obj_ptr] : object_instances) {
        if (!obj_ptr)
            continue;
        auto* ch_mod = dynamic_cast<ChStreamModuleBase*>(obj_ptr.get());
        if (!ch_mod)
            continue;

        auto* stat_group = ch_mod->get_stats_group();
        if (stat_group) {
            std::string stats_path = ch_mod->get_stats_path();
            tlm_stats::StatsManager::instance().register_group(stat_group, stats_path);
            DPRINTF(MODULE, "[Stats] Registered %s -> %s\n", name.c_str(), stats_path.c_str());
        }
    }

    // 7b. 创建 PortPairs（支持端口索引语法：xbar.0 → xbar.req_in[0]）
    // DEF-02: 使用同一个 processed_connections 集合去重（Step 6 已填充）
    bool connection_failed = false;
    for (auto& conn : final_config["connections"]) {
        if (!conn.contains("src") || !conn.contains("dst"))
            continue;
        std::string src_full = conn["src"];
        std::string dst_full = conn["dst"];

        auto conn_key = std::make_pair(src_full, dst_full);
        if (processed_connections.count(conn_key)) {
            DPRINTF(CONN, "[ChStream] Skipped duplicate connection %s -> %s\n", src_full.c_str(),
                    dst_full.c_str());
            continue;
        }

        int latency = conn.value("latency", 0);
        auto [src_name, src_spec_raw] = parsePortSpec(src_full);
        auto [dst_name, dst_spec_raw] = parsePortSpec(dst_full);

        std::string src_spec = resolve_port_alias(src_spec_raw);
        std::string dst_spec = resolve_port_alias(dst_spec_raw);

        unsigned src_idx = 0, dst_idx = 0;
        if (!src_spec.empty() && std::isdigit(src_spec[0])) {
            bool all_digits = std::all_of(src_spec.begin(), src_spec.end(), ::isdigit);
            if (all_digits) {
                src_idx = std::stoul(src_spec);
            } else {
                DPRINTF(CONN,
                        "[WARN] Invalid port index '%s' (expected digits only), defaulting to 0\n",
                        src_spec.c_str());
            }
        }
        if (!dst_spec.empty() && std::isdigit(dst_spec[0])) {
            bool all_digits = std::all_of(dst_spec.begin(), dst_spec.end(), ::isdigit);
            if (all_digits) {
                dst_idx = std::stoul(dst_spec);
            } else {
                DPRINTF(CONN,
                        "[WARN] Invalid port index '%s' (expected digits only), defaulting to 0\n",
                        dst_spec.c_str());
            }
        }

        if (!validate_nic_pe_connection(module_types[src_name], src_idx, module_types[dst_name],
                                        dst_idx)) {
            connection_failed = true;
            continue;
        }

        // 单端口模块忽略端口索引
        if (ch_adapters.count(src_name) && !factory.isMultiPort(module_types[src_name]))
            src_idx = 0;
        if (ch_adapters.count(dst_name) && !factory.isMultiPort(module_types[dst_name]))
            dst_idx = 0;

        bool src_ch = (ch_adapters.count(src_name) > 0 && ch_req_out.count(src_name) &&
                       ch_req_out[src_name].size() > src_idx);
        bool dst_ch = (ch_adapters.count(dst_name) > 0 && ch_req_in.count(dst_name) &&
                       ch_req_in[dst_name].size() > dst_idx);

        if (!src_ch || !dst_ch)
            continue;

        // Phase 3.2: L1/L2/L3 port compatibility check
        if (!check_port_compatibility(src_name, dst_name, src_idx, dst_idx, port_specs)) {
            connection_failed = true;
            continue;
        }

        processed_connections.insert(conn_key);

        // 请求路径: src → dst
        port_pairs_.push_back(std::make_unique<PortPair>(ch_req_out[src_name][src_idx],
                                                         ch_req_in[dst_name][dst_idx]));
        ch_req_out[src_name][src_idx]->setDelay(latency);
        DPRINTF(CONN, "[ChStream] Connected %s.req_out[%u] -> %s.req_in[%u] (latency=%d)\n",
                src_name.c_str(), src_idx, dst_name.c_str(), dst_idx, latency);

        // 响应路径: dst → src
        // P0-5b fix: count(name) 是键存在性(0/1),不是 vector 长度。
        // 旧代码 `count(dst_name) > dst_idx` 在 dst_idx >= 1 时永远 false,
        // 导致 xbar.resp_out[1/2/3] 永远没绑到 cpu1/2/3.resp_in,响应被丢弃。
        if (ch_resp_out.count(dst_name) && ch_resp_out[dst_name].size() > dst_idx &&
            ch_resp_in.count(src_name) && ch_resp_in[src_name].size() > src_idx) {
            port_pairs_.push_back(std::make_unique<PortPair>(ch_resp_out[dst_name][dst_idx],
                                                             ch_resp_in[src_name][src_idx]));
            ch_resp_out[dst_name][dst_idx]->setDelay(latency);
            DPRINTF(CONN, "[ChStream] Connected %s.resp_out[%u] -> %s.resp_in[%u] (latency=%d)\n",
                    dst_name.c_str(), dst_idx, src_name.c_str(), src_idx, latency);
        }
    }

    // ========================
    // 9. P1: 触发 SimModule::incorporate_parent late-binding
    //    对每个顶层 SimModule 调用一次, parent 传 nullptr
    //    ApuSoC 等会重写 incorporate_parent 完成跨域 wiring
    //    (Step 7 已注入 StreamAdapter + D.1 mirror, peer cache req_out 此时可查)
    // ========================
    for (auto& [name, mod] : module_instances) {
        if (!mod)
            continue;
        mod->incorporate_parent(nullptr);
    }

    // 保存所有实例。unique_ptr 非可拷贝, 必须 std::move 触发 move-assign
    // (instances 与 object_instances 类型一致, move 后 object_instances 为空,
    //  栈展开自动析构空 map 无副作用)。
    instances = std::move(object_instances);
    return !connection_failed;
}

void ModuleFactory::startAllTicks() {
    for (auto& [name, obj_ptr] : instances) {
        // unique_ptr::operator-> 返回 raw pointer, 直接调 initiate_tick
        obj_ptr->initiate_tick();
        DPRINTF(MODULE, "[MODULE] Started tick for %s\n", name.c_str());
    }
}

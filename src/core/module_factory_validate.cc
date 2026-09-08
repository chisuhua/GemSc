/**
 * @file module_factory_validate.cc
 * @brief ModuleFactory 配置验证与 extends 处理实现
 *
 * 本文件实现 ModuleFactory 的配置验证逻辑，负责：
 * - **extends 处理**：mergeConfigs() 递归合并继承配置
 * - **配置验证**：validateConfig() 检查 modules/connections 必需字段
 * - **参数解析**：parseModuleParams() 处理模块参数注入
 * - **拓扑验证**：validateTopology() 检查连接完整性
 *
 * ## 核心功能
 * - `mergeConfigs(base, child, depth)` — 深度合并 JSON 配置，支持 circular reference 检测
 * - `validateConfig(config)` — 验证 JSON 拓扑配置合法性
 * - `parseModuleParams(module_name, config)` — 解析模块参数并注入实例
 *
 * ## extends 处理流程
 * 1. 检测 circular reference（depth > MAX_DEPTH）
 * 2. 深度合并 modules 数组（按 name 匹配）
 * 3. 合并 connections 数组（追加）
 * 4. 合并 module_groups（递归）
 *
 * ## 使用注意事项
 * - **深度限制**：MAX_DEPTH = 10，防止 circular reference
 * - **模块合并**：同名模块配置深度合并，不同名追加
 * - **验证时机**：instantiateAll() 调用前执行 validateConfig()
 *
 * @author CppTLM Team
 * @date 2024-05
 * @see module_factory.hh
 * @see utils/config_utils.hh
 */

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
#include "module_factory.hh"
#include "sim_module.hh"
#include "tlm/router_tlm.hh"
#include "utils/config_utils.hh"
#include "utils/json_includer.hh"
#include "utils/module_group.hh"
#include "utils/regex_matcher.hh"
#include "utils/var_resolver.hh"
#include "utils/wildcard.hh"

using json = nlohmann::json;

// ============================================================================
// Config Extends Processing (Task 2.1-2.5)
// ============================================================================

static json mergeConfigs(const json& base, const json& child, int depth = 0) {
    if (depth > 10) {
        DPRINTF(MODULE,
                "[CONFIG ERROR] extends depth limit exceeded (possible circular reference)\n");
        return child;
    }

    json result = base;

    // Deep merge modules by name
    if (child.contains("modules")) {
        std::unordered_map<std::string, json> module_map;
        if (result.contains("modules")) {
            for (const auto& mod : result["modules"]) {
                module_map[mod["name"].get<std::string>()] = mod;
            }
        }
        for (const auto& mod : child["modules"]) {
            std::string name = mod["name"].get<std::string>();
            if (module_map.count(name)) {
                // Merge params
                json merged = module_map[name];
                if (mod.contains("params") && merged.contains("params")) {
                    for (const auto& [key, val] : mod["params"].items()) {
                        merged["params"][key] = val;
                    }
                } else if (mod.contains("params")) {
                    merged["params"] = mod["params"];
                }
                module_map[name] = merged;
            } else {
                module_map[name] = mod;
            }
        }
        result["modules"] = json::array();
        for (const auto& [name, mod] : module_map) {
            result["modules"].push_back(mod);
        }
    }

    // Append connections (not merge)
    if (child.contains("connections") && result.contains("connections")) {
        for (const auto& conn : child["connections"]) {
            result["connections"].push_back(conn);
        }
    } else if (child.contains("connections")) {
        result["connections"] = child["connections"];
    }

    if (child.contains("groups")) {
        if (!result.contains("groups")) {
            result["groups"] = json::object();
        }
        for (const auto& [group_name, members] : child["groups"].items()) {
            if (result["groups"].contains(group_name)) {
                for (const auto& m : members) {
                    result["groups"][group_name].push_back(m);
                }
            } else {
                result["groups"][group_name] = members;
            }
        }
    }

    for (const auto& [key, val] : child.items()) {
        if (key == "modules" || key == "connections" || key == "groups" || key == "extends") {
            continue;
        }
        if (!val.is_object() && !val.is_array()) {
            result[key] = val;
        }
    }

    return result;
}

json processExtends(const json& config, int depth = 0) {
    if (depth > 10) {
        DPRINTF(MODULE,
                "[CONFIG ERROR] extends depth limit exceeded (possible circular reference)\n");
        return json::object();
    }

    if (!config.contains("extends")) {
        return config;
    }

    std::string extends_path = config["extends"].get<std::string>();
    if (ModuleFactory::debug_config()) {
        for (int i = 0; i < depth; ++i)
            DPRINTF(MODULE, "  ");
        DPRINTF(MODULE, "[DEBUG] Processing extends: %s (depth: %d)\n", extends_path.c_str(),
                depth);
    }

    std::ifstream f(extends_path);
    if (!f.is_open()) {
        DPRINTF(MODULE, "[CONFIG ERROR] Cannot open extends file: %s\n", extends_path.c_str());
        return json::object();
    }

    json base_config;
    try {
        base_config = json::parse(f);
    } catch (const json::parse_error& e) {
        DPRINTF(MODULE, "[CONFIG ERROR] Failed to parse extends file '%s': %s\n",
                extends_path.c_str(), e.what());
        return json::object();
    }
    f.close();

    // Recursively process extends in base config
    json processed_base = processExtends(base_config, depth + 1);
    if (processed_base.is_object() && processed_base.empty()) {
        return json::object();
    }

    // Merge child over base
    json result = mergeConfigs(processed_base, config, depth);

    if (result.contains("extends")) {
        result.erase("extends");
    }

    return result;
}

// ============================================================================
// JSON Schema 验证器（CFG-08）
// ============================================================================
bool ModuleFactory::validateConfig(const json& config) {
    // 1. 检查顶层必需字段
    if (!config.contains("modules")) {
        DPRINTF(MODULE, "[CONFIG ERROR] Missing required field 'modules'\n");
        return false;
    }
    if (!config["modules"].is_array()) {
        DPRINTF(MODULE, "[CONFIG ERROR] Field 'modules' must be an array\n");
        return false;
    }

    if (!config.contains("connections")) {
        DPRINTF(MODULE, "[CONFIG ERROR] Missing required field 'connections'\n");
        return false;
    }
    if (!config["connections"].is_array()) {
        DPRINTF(MODULE, "[CONFIG ERROR] Field 'connections' must be an array\n");
        return false;
    }

    // version 字段可选，缺失时警告
    if (!config.contains("version")) {
        DPRINTF(MODULE, "[CONFIG WARN] Missing optional field 'version'\n");
    }

    // 2. 检查每个模块的必需字段
    for (const auto& mod : config["modules"]) {
        // name 字段检查
        if (!mod.contains("name")) {
            DPRINTF(MODULE, "[CONFIG ERROR] Module missing required field 'name'\n");
            return false;
        }
        if (!mod["name"].is_string()) {
            DPRINTF(MODULE, "[CONFIG ERROR] Module field 'name' must be a string\n");
            return false;
        }
        std::string name = mod["name"].get<std::string>();

        // type 字段检查
        if (!mod.contains("type")) {
            DPRINTF(MODULE, "[CONFIG ERROR] Module '%s' missing required field 'type'\n",
                    name.c_str());
            return false;
        }
        if (!mod["type"].is_string()) {
            DPRINTF(MODULE, "[CONFIG ERROR] Module '%s' field 'type' must be a string\n",
                    name.c_str());
            return false;
        }
        std::string type = mod["type"].get<std::string>();

        // LINT005: config 字段必须是 string (file path) 或不存在。
        // 把参数 dict 错放在 config 字段会触发 C++ 端静默忽略 (SimObject 路径)
        // 或 json::type_error 崩溃 (SimModule 路径)。
        // 详见 openspec/changes/field-name-unification (config-lint spec)。
        if (mod.contains("config") && !mod["config"].is_string()) {
            DPRINTF(MODULE,
                    "[CONFIG ERROR] module '%s' uses 'config' for module configuration; "
                    "use 'params' instead (LINT005)\n",
                    name.c_str());
            return false;
        }

        // LINT007 (F7): params.ports vs class max_ports 校验 (warning-only)。
        // 已知 forward-looking 场景: apu_soc_v1.json 中 CoherentXBarTLM 指定 ports=8
        // (Phase 7.C 8-port upgrade 前的占位), 当前 NUM_PORTS=4, 应警告而非阻塞。
        // 参考 docs/superpowers/plans/2026-06-20-future-work-roadmap.md F7。
        static const std::unordered_map<std::string, unsigned> kMaxPortsByType = {
            {"CrossbarTLM", 4}, {"CoherentXBarTLM", 4}, {"ArbiterTLM", 4},
            {"ArbiterTLM2", 2}, {"ArbiterTLM4", 4},
        };
        if (mod.contains("params") && mod["params"].is_object() &&
            mod["params"].contains("ports")) {
            const auto& ports_param = mod["params"]["ports"];
            if (ports_param.is_number_integer()) {
                unsigned requested_ports = ports_param.get<unsigned>();
                auto it = kMaxPortsByType.find(type);
                if (it != kMaxPortsByType.end()) {
                    unsigned max_ports = it->second;
                    if (requested_ports > max_ports) {
                        DPRINTF(MODULE,
                                "[CONFIG WARN] module '%s' (type %s): params.ports=%u "
                                "exceeds class max_ports=%u; will be ignored until "
                                "port count upgrade (see roadmap F4/F9)\n",
                                name.c_str(), type.c_str(), requested_ports, max_ports);
                    } else if (requested_ports < max_ports) {
                        DPRINTF(MODULE,
                                "[CONFIG WARN] module '%s' (type %s): params.ports=%u "
                                "is less than class max_ports=%u; "
                                "module will use full max_ports=%u\n",
                                name.c_str(), type.c_str(), requested_ports, max_ports);
                    }
                }
            } else if (!ports_param.is_array()) {
                DPRINTF(MODULE,
                        "[CONFIG WARN] module '%s' (type %s): params.ports "
                        "should be integer (not array or other type); "
                        "validation skipped\n",
                        name.c_str(), type.c_str());
            }
        }

        const json* params_src = nullptr;
        if (mod.contains("params"))
            params_src = &mod["params"];
        else if (type == "RouterTLM" && mod.contains("node_x"))
            params_src = &mod;
        else if (type == "NICTLM" && mod.contains("node_id"))
            params_src = &mod;

        if (params_src) {
            const auto& params = *params_src;
            if (type == "RouterTLM") {
                for (auto p : {"node_x", "node_y", "mesh_x", "mesh_y"}) {
                    if (!params.contains(p) || !params[p].is_number_integer()) {
                        DPRINTF(MODULE, "[CONFIG ERROR] Module '%s' missing/invalid '%s'\n",
                                name.c_str(), p);
                        return false;
                    }
                }
            }
            if (type == "NICTLM") {
                if (!params.contains("node_id") || !params["node_id"].is_number_integer()) {
                    DPRINTF(MODULE, "[CONFIG ERROR] Module '%s' missing/invalid 'node_id'\n",
                            name.c_str());
                    return false;
                }
            }
        } else if (type == "RouterTLM" || type == "NICTLM") {
            DPRINTF(MODULE, "[CONFIG ERROR] Module '%s' missing required params\n", name.c_str());
            return false;
        }
    }

    DPRINTF(MODULE, "[CONFIG] Schema validation passed\n");
    return true;
}

bool validate_nic_pe_connection(const std::string& src_type, unsigned src_port,
                                const std::string& dst_type, unsigned dst_port) {
    if (src_type == "NICTLM" && src_port == 0) {
        if (dst_type == "RouterTLM") {
            DPRINTF(CONN, "[CONN ERROR] NICTLM PE-side port (port 0) cannot connect directly "
                          "to RouterTLM. Use NETWORK side (port 1) for router connections.\n");
            return false;
        }
    }
    if (dst_type == "NICTLM" && dst_port == 0) {
        if (src_type == "RouterTLM") {
            DPRINTF(CONN, "[CONN ERROR] NICTLM PE-side port (port 0) cannot connect directly "
                          "to RouterTLM. Use NETWORK side (port 1) for router connections.\n");
            return false;
        }
    }
    return true;
}

// Phase 3.2: Port compatibility checking (T3.2-04~06)
bool check_port_compatibility(const std::string& src_name, const std::string& dst_name,
                              unsigned src_idx, unsigned dst_idx,
                              const std::map<std::string, cpptlm::ModulePortSpec>& port_specs) {
    auto src_it = port_specs.find(src_name);
    auto dst_it = port_specs.find(dst_name);

    if (src_it == port_specs.end() || dst_it == port_specs.end()) {
        return true;
    }

    const auto& src_ports = src_it->second.ports;
    const auto& dst_ports = dst_it->second.ports;

    if (src_idx >= src_ports.size() || dst_idx >= dst_ports.size()) {
        DPRINTF(CONN, "[WARN] Port index out of range: %s[%u] or %s[%u]\n", src_name.c_str(),
                src_idx, dst_name.c_str(), dst_idx);
        return true;
    }

    const auto& src_spec = src_ports[src_idx];
    const auto& dst_spec = dst_ports[dst_idx];

    if (!cpptlm::PortCompatibility::is_role_compatible(src_spec.role, dst_spec.role)) {
        DPRINTF(CONN, "[PORT ERROR] Incompatible port roles: %s.%u (%s) -> %s.%u (%s)\n",
                src_name.c_str(), src_idx, cpptlm::PortCompatibility::role_name(src_spec.role),
                dst_name.c_str(), dst_idx, cpptlm::PortCompatibility::role_name(dst_spec.role));
        return false;
    }

    if (!cpptlm::PortCompatibility::is_bundle_compatible(src_spec.bundle, dst_spec.bundle)) {
        DPRINTF(CONN, "[PORT ERROR] Incompatible bundle types: %s.%u (%s) -> %s.%u (%s)\n",
                src_name.c_str(), src_idx, cpptlm::PortCompatibility::bundle_name(src_spec.bundle),
                dst_name.c_str(), dst_idx, cpptlm::PortCompatibility::bundle_name(dst_spec.bundle));
        return false;
    }

    if (!cpptlm::PortCompatibility::is_width_compatible(src_spec.width, dst_spec.width)) {
        DPRINTF(CONN, "[PORT WARN] Width mismatch: %s.%u (%u bits) -> %s.%u (%u bits)\n",
                src_name.c_str(), src_idx, src_spec.width, dst_name.c_str(), dst_idx,
                dst_spec.width);
    }

    return true;
}

// Phase 3.2: T3.2-10 - Default port specs for known module types
std::map<std::string, cpptlm::ModulePortSpec> get_default_port_specs() {
    std::map<std::string, cpptlm::ModulePortSpec> defaults;

    // RouterTLM: 5 ports (NORTH=0, EAST=1, SOUTH=2, WEST=3, LOCAL=4)
    {
        cpptlm::ModulePortSpec spec;
        spec.module_name = "RouterTLM";
        std::vector<cpptlm::PortSpec> ports = {
            {"NORTH", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64},
            {"EAST", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64},
            {"SOUTH", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64},
            {"WEST", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64},
            {"LOCAL", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64}};
        spec.ports = ports;
        defaults["RouterTLM"] = spec;
        defaults["MeshRouter"] = spec;
    }

    // NICTLM: 2 ports (PE=0, NETWORK=1)
    {
        cpptlm::ModulePortSpec spec;
        spec.module_name = "NICTLM";
        std::vector<cpptlm::PortSpec> ports = {
            {"pe", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::CACHE_REQ, 64},
            {"network", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::NOC_FLIT, 64}};
        spec.ports = ports;
        defaults["NICTLM"] = spec;
        defaults["NetworkInterface"] = spec;
    }

    // CacheTLM: 2 ports (req_out=INITIATOR, req_in=TARGET)
    {
        cpptlm::ModulePortSpec spec;
        spec.module_name = "CacheTLM";
        std::vector<cpptlm::PortSpec> ports = {
            {"req_out", cpptlm::PortRole::INITIATOR, cpptlm::BundleType::CACHE_REQ, 64},
            {"req_in", cpptlm::PortRole::TARGET, cpptlm::BundleType::CACHE_REQ, 64}};
        spec.ports = ports;
        defaults["CacheTLM"] = spec;
        defaults["Cache"] = spec;
    }

    // CrossbarTLM: 4 bidirectional ports (Cache side)
    {
        cpptlm::ModulePortSpec spec;
        spec.module_name = "CrossbarTLM";
        std::vector<cpptlm::PortSpec> ports = {
            {"port_0", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::CACHE_REQ, 64},
            {"port_1", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::CACHE_REQ, 64},
            {"port_2", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::CACHE_REQ, 64},
            {"port_3", cpptlm::PortRole::BI_DIRECTIONAL, cpptlm::BundleType::CACHE_REQ, 64}};
        spec.ports = ports;
        defaults["CrossbarTLM"] = spec;
        defaults["Crossbar"] = spec;
    }

    // MemoryTLM: 1 port (target)
    {
        cpptlm::ModulePortSpec spec;
        spec.module_name = "MemoryTLM";
        std::vector<cpptlm::PortSpec> ports = {
            {"mem", cpptlm::PortRole::TARGET, cpptlm::BundleType::GENERIC, 64}};
        spec.ports = ports;
        defaults["MemoryTLM"] = spec;
        defaults["Memory"] = spec;
    }

    return defaults;
}

// Phase 3.3: Parameter validation helpers
static bool validate_module_params(const std::string& module_type, const json& params,
                                   const cpptlm::ParamRules& rules) {
    for (const auto& [param_name, rule] : rules) {
        if (rule.required && !params.contains(param_name)) {
            DPRINTF(MODULE, "[PARAM ERROR] Module '%s' missing required param '%s'\n",
                    module_type.c_str(), param_name.c_str());
            return false;
        }
        if (params.contains(param_name)) {
            auto result = cpptlm::ParamParser::parse(
                params[param_name].is_string() ? params[param_name].get<std::string>()
                                               : std::to_string(params[param_name].get<int64_t>()),
                rule.type);
            if (!result.success) {
                DPRINTF(MODULE, "[PARAM ERROR] Module '%s' param '%s' parse failed: %s\n",
                        module_type.c_str(), param_name.c_str(), result.error_message.c_str());
                return false;
            }
            if (!cpptlm::ParamParser::validate(result, rule)) {
                DPRINTF(MODULE, "[PARAM ERROR] Module '%s' param '%s' validation failed\n",
                        module_type.c_str(), param_name.c_str());
                return false;
            }
        }
    }
    return true;
}

bool ModuleFactory::validate_domain_boundary(const std::string& src_module,
                                             const std::string& dst_module,
                                             const std::string& src_domain,
                                             const std::string& bridge_name) {
    if (src_module.empty() || dst_module.empty() || src_domain.empty()) {
        DPRINTF(MODULE,
                "[DOMAIN ERROR] Invalid domain boundary check: empty module or domain name\n");
        return false;
    }

    auto domain = DomainRegistry::get_domain(src_domain);
    if (!domain) {
        DPRINTF(MODULE, "[DOMAIN ERROR] Domain '%s' not found in registry\n", src_domain.c_str());
        return false;
    }

    if (domain->is_member(dst_module)) {
        return true;
    }

    if (!bridge_name.empty() && domain->has_bridge_to(dst_module)) {
        return true;
    }

    if (!bridge_name.empty()) {
        domain->register_bridge(dst_module, bridge_name);
        return true;
    }

    DPRINTF(MODULE, "[DOMAIN WARNING] Cross-domain connection detected: %s -> %s without bridge\n",
            src_module.c_str(), dst_module.c_str());
    return false;
}

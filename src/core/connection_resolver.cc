#include "core/connection_resolver.hh"

#include <iostream>
#include "core/sim_module.hh"
#include "nlohmann/json.hpp"

std::pair<std::string, std::string>
ConnectionResolver::parsePortSpec(const std::string& spec) const {
    size_t dot_pos = spec.find('.');
    if (dot_pos == std::string::npos) {
        // No dot, just module name
        return {spec, ""};
    }

    std::string module_name = spec.substr(0, dot_pos);
    std::string port_name = spec.substr(dot_pos + 1);

    return {module_name, port_name};
}

std::string ConnectionResolver::findInternalPath(SimModule* module,
                                                 const std::string& port_name) const {
    if (module) {
        return module->findInternalPath(port_name);
    }
    return "";
}

std::vector<PortCreationInfo> ConnectionResolver::resolveConnections(
    const nlohmann::json& connections,
    const std::unordered_map<std::string, SimModule*>& module_instances,
    std::function<bool(const std::string&, const std::string&, size_t, bool)>) const {
    std::vector<PortCreationInfo> port_creations;

    for (const auto& conn : connections) {
        if (!conn.contains("src") || !conn.contains("dst")) {
            continue;
        }

        std::string src_spec = conn["src"];
        std::string dst_spec = conn["dst"];
        int latency = conn.value("latency", 0); // P1.2: 提取 latency

        // Resolve source port
        auto [src_module_name, src_port_name] = parsePortSpec(src_spec);

        // Check if source port belongs to SimModule exposed port
        if (auto mod_it = module_instances.find(src_module_name);
            mod_it != module_instances.end() && !src_port_name.empty()) {
            std::string internal_path = findInternalPath(mod_it->second, src_port_name);
            if (!internal_path.empty()) {
                // Exposed output port - create downstream port
                auto [internal_owner, internal_port] = parsePortSpec(internal_path);

                std::vector<size_t> out_sizes = {4};
                std::vector<size_t> priorities = {};

                if (conn.contains("buffer_sizes")) {
                    for (const auto& size : conn["buffer_sizes"]) {
                        out_sizes.push_back(size.get<size_t>());
                    }
                }
                if (conn.contains("vc_priorities")) {
                    for (const auto& pri : conn["vc_priorities"]) {
                        priorities.push_back(pri.get<size_t>());
                    }
                }

                port_creations.emplace_back(internal_owner, internal_port, out_sizes, priorities,
                                            false, latency);
            }
        } else if (!src_port_name.empty()) {
            // Regular module downstream port
            std::vector<size_t> out_sizes = {4};
            std::vector<size_t> priorities = {};

            if (conn.contains("buffer_sizes")) {
                for (const auto& size : conn["buffer_sizes"]) {
                    out_sizes.push_back(size.get<size_t>());
                }
            }
            if (conn.contains("vc_priorities")) {
                for (const auto& pri : conn["vc_priorities"]) {
                    priorities.push_back(pri.get<size_t>());
                }
            }

            port_creations.emplace_back(src_module_name, src_port_name, out_sizes, priorities,
                                        false, latency);
        }

        // Resolve destination port
        auto [dst_module_name, dst_port_name] = parsePortSpec(dst_spec);

        // Check if destination port belongs to SimModule exposed port
        if (auto mod_it = module_instances.find(dst_module_name);
            mod_it != module_instances.end() && !dst_port_name.empty()) {
            std::string internal_path = findInternalPath(mod_it->second, dst_port_name);
            if (!internal_path.empty()) {
                // Exposed input port - create upstream port
                auto [internal_owner, internal_port] = parsePortSpec(internal_path);

                std::vector<size_t> in_sizes = {4};
                std::vector<size_t> priorities = {};

                if (conn.contains("buffer_sizes")) {
                    for (const auto& size : conn["buffer_sizes"]) {
                        in_sizes.push_back(size.get<size_t>());
                    }
                }
                if (conn.contains("vc_priorities")) {
                    for (const auto& pri : conn["vc_priorities"]) {
                        priorities.push_back(pri.get<size_t>());
                    }
                }

                port_creations.emplace_back(internal_owner, internal_port, in_sizes, priorities,
                                            true, latency);
            }
        } else if (!dst_port_name.empty()) {
            // Regular module upstream port
            std::vector<size_t> in_sizes = {4};
            std::vector<size_t> priorities = {};

            if (conn.contains("buffer_sizes")) {
                for (const auto& size : conn["buffer_sizes"]) {
                    in_sizes.push_back(size.get<size_t>());
                }
            }

            port_creations.emplace_back(dst_module_name, dst_port_name, in_sizes, priorities, true,
                                        latency);
        }
    }

    return port_creations;
}

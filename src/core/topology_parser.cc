/**
 * @file topology_parser.cc
 * @brief 层级拓扑树解析器实现
 */

#include "core/topology_parser.hh"
#include <algorithm>
#include <unordered_set>
#include "core/sim_core.hh"
#include "core/topology_node.hh"

namespace cpptlm {

    bool detect_circular_reference(const std::shared_ptr<TopologyNode>& node,
                                   std::unordered_set<std::string>& visited) {
        if (node == nullptr) {
            return false;
        }

        const std::string& node_name = node->get_name();
        if (node_name.empty()) {
            return false;
        }

        if (visited.count(node_name) > 0) {
            return true;
        }

        visited.insert(node_name);

        for (const auto& child : node->get_children()) {
            if (detect_circular_reference(child, visited)) {
                return true;
            }
        }

        visited.erase(node_name);
        return false;
    }

    std::vector<std::string> parse_coherence_domains_array(const nlohmann::json& coherence_json) {
        std::vector<std::string> domains;

        if (!coherence_json.is_array()) {
            return domains;
        }

        for (const auto& item : coherence_json) {
            if (item.is_string()) {
                domains.push_back(item.get<std::string>());
            } else if (item.is_object() && item.contains("name")) {
                domains.push_back(item["name"].get<std::string>());
            }
        }

        return domains;
    }

    std::shared_ptr<TopologyNode>
    parse_hierarchy_node_internal(const nlohmann::json& hierarchy_json,
                                  std::shared_ptr<TopologyNode> parent,
                                  std::unordered_set<std::string>& seen_names) {
        if (!hierarchy_json.is_object()) {
            throw TopologyParseException("Hierarchy JSON must be an object");
        }

        if (!hierarchy_json.contains("name")) {
            throw TopologyParseException("Hierarchy node must have a 'name' field");
        }

        std::string node_name = hierarchy_json["name"].get<std::string>();

        if (seen_names.count(node_name) > 0) {
            throw TopologyParseException("Circular reference detected for node: " + node_name);
        }
        seen_names.insert(node_name);

        auto node = std::make_shared<TopologyNode>(node_name);

        if (parent) {
            node->set_parent(parent->get_name());
            parent->add_child(node);
        }

        if (hierarchy_json.contains("children")) {
            const auto& children = hierarchy_json["children"];
            if (children.is_array()) {
                for (const auto& child_json : children) {
                    parse_hierarchy_node_internal(child_json, node, seen_names);
                }
            }
        }

        seen_names.erase(node_name);
        return node;
    }

    std::shared_ptr<TopologyNode> parse_hierarchy_tree(const nlohmann::json& hierarchy_json) {
        if (!hierarchy_json.is_object()) {
            throw TopologyParseException("Hierarchy JSON must be an object");
        }

        std::unordered_set<std::string> seen_names;
        return parse_hierarchy_node_internal(hierarchy_json, nullptr, seen_names);
    }

    std::shared_ptr<TopologyNode>
    parse_hierarchy_tree_with_validation(const nlohmann::json& hierarchy_json,
                                         const nlohmann::json& coherence_json) {
        if (!coherence_json.is_null() && !coherence_json.empty()) {
            DPRINTF(PARSER,
                    "[STUB] coherence_domains field accepted but not consumed; see ADR-X.14 "
                    "(members=%zu)\n",
                    coherence_json.is_array() ? coherence_json.size() : 0);
        }
        auto root = parse_hierarchy_tree(hierarchy_json);
        return root;
    }

} // namespace cpptlm
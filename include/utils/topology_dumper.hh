// include/utils/topology_dumper.hh
#ifndef TOPOLOGY_DUMPER_HH
#define TOPOLOGY_DUMPER_HH

#include "module_factory.hh"
#include "force_directed_layout.hh"
#include <fstream>

class TopologyDumper {
public:
    static void dumpToDot(const ModuleFactory& factory, const json& config, const std::string& filename) {
        std::ofstream f(filename);
        if (!f.is_open()) {
            printf("[TopologyDumper] Cannot open file: %s\n", filename.c_str());
            return;
        }

        double width = 800, height = 600;
        std::string layout_style = config.value("layout.style", "auto");

        f << "digraph System {\n";
        f << "  rankdir=TB;\n";
        f << "  node [shape=box];\n";
        f << "  overlap=false;\n";
        f << "  splines=true;\n";
        f << "  graph [bb=\"0,0," << (int)width << "," << (int)height << "\"];\n\n";

        // ========================
        // 收集节点、边、分组
        // ========================
        std::vector<std::string> all_nodes;
        std::vector<std::pair<std::string, std::string>> all_edges;
        std::unordered_map<std::string, std::string> node_to_group;

        for (const auto& [name, obj] : factory.getAllInstances()) {
            all_nodes.push_back(name);

            const auto& layout = obj->getLayout();
            if (layout.valid()) {
                node_to_group[name] = "";  // 固定坐标，不参与分组布局
            }
        }

        // 解析 groups
        for (const std::string& group_name : ModuleGroup::getAllGroupNames()) {
            for (const std::string& member : ModuleGroup::getMembers(group_name)) {
                if (node_to_group.find(member) == node_to_group.end()) {
                    node_to_group[member] = group_name;
                }
            }
        }

        // 收集连接
        std::unordered_set<std::string> edge_set;
        for (const auto& [src_name, src_obj] : factory.getAllInstances()) {
            const auto& src_pm = src_obj->getPortManager();
            for (auto* port_base : src_pm.getDownstreamPorts()) {
                auto* port = dynamic_cast<MasterPort*>(port_base);
                if (!port || !port->pair) continue;

                SlavePort* peer = (port->my_side == 0)
                    ? dynamic_cast<SlavePort*>(port->pair->side_b)
                    : dynamic_cast<SlavePort*>(port->pair->side_a);

                SimObject* dst_obj = findOwner(peer, factory.getAllInstances());
                if (!dst_obj) continue;

                std::string dst_name = dst_obj->getName();
                std::string key = src_name + "->" + dst_name;
                if (edge_set.count(key)) continue;
                edge_set.insert(key);
                all_edges.emplace_back(src_name, dst_name);
            }
        }

        // ========================
        // 布局计算
        // ========================
        ForceDirectedLayout layout_engine;

        // 添加所有节点
        for (const std::string& node : all_nodes) {
            const auto& obj = factory.getInstance(node);
            const auto& l = obj->getLayout();
            layout_engine.addNode(node, l.valid() ? l.x : -1, l.valid() ? l.y : -1);
        }

        // 设置全局初始布局（仅当 style=auto）
        if (layout_style == "auto") {
            layout_engine.setInitialPlacement("grid", width, height);
        }

        // 设置分组布局策略
        for (const std::string& group_name : ModuleGroup::getAllGroupNames()) {
            std::vector<std::string> members = ModuleGroup::getMembers(group_name);

            // 从 JSON 获取 placement 策略
            std::string placement = getGroupPlacement(config, group_name, "circle");
            double center_x = width / 2;
            double center_y = height / 2;
            double radius = 120;

            // 特殊处理：为每个 group 设置中心区域
            if (placement == "grid" || placement == "linear" || placement == "radial" || placement == "circle") {
                layout_engine.setGroupPlacement(group_name, members, placement, center_x, center_y, radius);
            }

            // 注册 group 关系（用于力导向中的聚类）
            for (const std::string& member : members) {
                if (!factory.getInstance(member)->getLayout().valid()) {
                    layout_engine.setGroup(member, group_name);
                }
            }
        }

        // 运行力导向（仅 auto 模式）
        if (layout_style == "auto") {
            layout_engine.run(width, height);
        }

        const auto& positions = layout_engine.getPositions();

        // ========================
        // 输出节点
        // ========================
        for (const std::string& node : all_nodes) {
            const auto& pos = positions.at(node);
            f << "  \"" << node << "\" [label=\"" << node << "\\n("
              << getModuleType(factory.getInstance(node)) << ")\"";

            if (layout_style == "manual") {
                const auto& l = factory.getInstance(node)->getLayout();
                if (l.valid()) {
                    f << ", pos=\"" << l.x << "," << l.y << "!\"";
                } else {
                    DPRINTF(LAYOUT, "[WARNING] %s missing layout in manual mode\n", node.c_str());
                }
            } else {
                f << ", pos=\"" << pos.x << "," << pos.y << "\"";
            }

            f << "];\n";
        }
        f << "\n";

        // ========================
        // 输出组（子图）
        // ========================
        for (const std::string& group_name : ModuleGroup::getAllGroupNames()) {
            f << "  subgraph cluster_" << group_name << " {\n";
            f << "    label = \"" << group_name << "\";\n";
            f << "    style = dashed;\n";
            for (const std::string& member : ModuleGroup::getMembers(group_name)) {
                f << "    \"" << member << "\";\n";
            }
            f << "  }\n\n";
        }

        // ========================
        // 输出连接
        // ========================
        for (const auto& [src, dst] : all_edges) {
            MasterPort* port = nullptr;
            for (auto* p : factory.getInstance(src)->getPortManager().getDownstreamPorts()) {
                if (auto* mp = dynamic_cast<MasterPort*>(p)) {
                    if (mp->pair && (
                        (mp->my_side == 0 && mp->pair->side_b->getOwner()->getName() == dst) ||
                        (mp->my_side == 1 && mp->pair->side_a->getOwner()->getName() == dst))) {
                        port = mp;
                        break;
                    }
                }
            }

            f << "  \"" << src << "\" -> \"" << dst
              << "\" [label=\"delay:" << (port ? port->getDelay() : 0) << "\"];\n";
        }

        f << "}\n";
        f.close();
        printf("[TopologyDumper] Layout saved to %s (style=%s)\n", filename.c_str(), layout_style.c_str());
    }

private:
    // 从配置中提取 group 的 placement 策略
    static std::string getGroupPlacement(const json& config, const std::string& group_name, const std::string& default_val) {
        if (config.contains("groups")) {
            auto it = config["groups"].find(group_name);
            if (it != config["groups"].end()) {
                if (it->is_object() && it->contains("placement")) {
                    return it->at("placement").get<std::string>();
                }
            }
        }
        return default_val;
    }

    static std::string getModuleType(SimObject* obj) {
        return "Module";
    }

    // RAII ownership: ModuleFactory::getAllInstances() 返回 unique_ptr map
    // (P0-5b leak fix: instances 改为 unique_ptr<SimObject>), 与之签名对齐。
    static SimObject* findOwner(SlavePort* port, const std::unordered_map<std::string, std::unique_ptr<SimObject>>& instances) {
        for (const auto& [name, obj_ptr] : instances) {
            const auto& pm = obj_ptr->getPortManager();
            for (auto* p : pm.getUpstreamPorts()) {
                if (p == port) return obj_ptr.get();
            }
        }
        return nullptr;
    }
};

#endif // TOPOLOGY_DUMPER_HH

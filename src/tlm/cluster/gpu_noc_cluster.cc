// src/tlm/cluster/gpu_noc_cluster.cc
#include "tlm/cluster/gpu_noc_cluster.hh"
#include "core/module_factory.hh"

namespace cpptlm::tlm {

    void GpuNoC::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("mesh_size"))
            mesh_size_ = params["mesh_size"].get<int>();
        if (params.contains("routing"))
            routing_ = params["routing"].get<std::string>();
    }

    void GpuNoC::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 同根因 as 4 cluster fix (PR #27), 当前无活路径触发, 防御性补齐。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        nlohmann::json full_config = {{"modules", nlohmann::json::array()},
                                      {"connections", nlohmann::json::array()}};
        int n = mesh_size_ * mesh_size_;
        for (int i = 0; i < n; ++i) {
            full_config["modules"].push_back({{"name", "router_" + std::to_string(i)},
                                              {"type", "RouterTLM"},
                                              {"params",
                                               {{"node_x", i % mesh_size_},
                                                {"node_y", i / mesh_size_},
                                                {"mesh_x", mesh_size_},
                                                {"mesh_y", mesh_size_},
                                                {"routing", routing_}}}});
        }
        internal_factory->instantiateAll(full_config);
    }

} // namespace cpptlm::tlm

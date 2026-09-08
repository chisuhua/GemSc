// src/tlm/cluster/cache_cluster.cc
#include "tlm/cluster/cache_cluster.hh"
#include "core/module_factory.hh"

namespace cpptlm::tlm {

    void CacheCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("l1_count"))
            l1_count_ = params["l1_count"].get<int>();
        if (params.contains("l1_size"))
            l1_size_ = params["l1_size"].get<std::string>();
        if (params.contains("l2_size"))
            l2_size_ = params["l2_size"].get<std::string>();
    }

    void CacheCluster::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 同根因 as 4 cluster fix (PR #27), 当前无活路径触发, 防御性补齐。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        nlohmann::json full_config = {{"modules", nlohmann::json::array()}};
        full_config["modules"].push_back(
            {{"name", "l2"}, {"type", "CacheTLM"}, {"params", {{"size", l2_size_}, {"level", 2}}}});
        for (int i = 0; i < l1_count_; ++i) {
            full_config["modules"].push_back({{"name", "l1_" + std::to_string(i)},
                                              {"type", "CacheTLM"},
                                              {"params", {{"size", l1_size_}, {"level", 1}}}});
        }
        full_config["connections"] = nlohmann::json::array();
        for (int i = 0; i < l1_count_; ++i) {
            full_config["connections"].push_back(
                {{"src", "l1_" + std::to_string(i)}, {"dst", "l2"}, {"latency", 5}});
        }
        internal_factory->instantiateAll(full_config);
    }

} // namespace cpptlm::tlm

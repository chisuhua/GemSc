// src/tlm/cluster/memory_cluster.cc
#include "tlm/cluster/memory_cluster.hh"
#include "core/module_factory.hh"

namespace cpptlm::tlm {

    void MemoryCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("channel_count"))
            channel_count_ = params["channel_count"].get<int>();
        if (params.contains("channel_size"))
            channel_size_ = params["channel_size"].get<std::string>();
        if (params.contains("memory_type"))
            memory_type_ = params["memory_type"].get<std::string>();
    }

    void MemoryCluster::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 同根因 as 4 cluster fix (PR #27), 当前无活路径触发, 防御性补齐。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        nlohmann::json full_config = {{"modules", nlohmann::json::array()}};
        for (int i = 0; i < channel_count_; ++i) {
            full_config["modules"].push_back(
                {{"name", "channel_" + std::to_string(i)},
                 {"type", "MemoryTLM"},
                 {"params", {{"size", channel_size_}, {"type", memory_type_}}}});
        }
        const std::string arb_type = (channel_count_ == 2) ? "ArbiterTLM2" : "ArbiterTLM4";
        full_config["modules"].push_back(
            {{"name", "arbiter"}, {"type", arb_type}, {"params", {{"ports", channel_count_}}}});
        full_config["connections"] = nlohmann::json::array();
        for (int i = 0; i < channel_count_; ++i) {
            full_config["connections"].push_back({{"src", "channel_" + std::to_string(i)},
                                                  {"dst", "arbiter." + std::to_string(i)},
                                                  {"latency", 10}});
        }
        internal_factory->instantiateAll(full_config);
    }

} // namespace cpptlm::tlm

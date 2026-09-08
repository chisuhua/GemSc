// src/tlm/cluster/gpc_cluster.cc
#include "tlm/cluster/gpc_cluster.hh"
#include "core/module_factory.hh"

namespace cpptlm::tlm {

    void GpcCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("gpc_id"))
            gpc_id_ = params["gpc_id"].get<int>();
        if (params.contains("tpc_per_gpc"))
            tpc_per_gpc_ = params["tpc_per_gpc"].get<int>();
        if (params.contains("cu_per_tpc"))
            cu_per_tpc_ = params["cu_per_tpc"].get<int>();
        if (params.contains("cu_template"))
            cu_template_path_ = params["cu_template"].get<std::string>();
    }

    void GpcCluster::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 防双激活二次生成覆盖 instances map 泄漏第一批子树 (fix-asan 根因 2)。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        nlohmann::json tpc_cfgs = nlohmann::json::array();
        for (int i = 0; i < tpc_per_gpc_; ++i) {
            nlohmann::json tpc_entry;
            tpc_entry["name"] = "tpc" + std::to_string(i);
            tpc_entry["type"] = "TpcCluster";
            tpc_entry["params"] = {
                {"tpc_id", i}, {"cu_per_tpc", cu_per_tpc_}, {"cu_template", cu_template_path_}};
            tpc_entry["modules"] = nlohmann::json::array();
            tpc_entry["connections"] = nlohmann::json::array();
            tpc_cfgs.push_back(tpc_entry);
        }
        nlohmann::json gpc_cfg;
        gpc_cfg["modules"] = tpc_cfgs;
        gpc_cfg["connections"] = nlohmann::json::array();
        internal_factory->instantiateAll(gpc_cfg);
    }

} // namespace cpptlm::tlm
// src/tlm/cluster/tpc_cluster.cc
#include "tlm/cluster/tpc_cluster.hh"
#include <stdexcept>
#include "core/module_factory.hh"
#include "utils/json_includer.hh"

namespace cpptlm::tlm {

    void TpcCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("tpc_id"))
            tpc_id_ = params["tpc_id"].get<int>();
        if (params.contains("cu_per_tpc"))
            cu_per_tpc_ = params["cu_per_tpc"].get<int>();
        if (params.contains("cu_template"))
            cu_template_path_ = params["cu_template"].get<std::string>();
    }

    void TpcCluster::simulate_instantiate(const nlohmann::json& cfg) {
        SimModule::simulate_instantiate(cfg);
        if (cu_template_path_.empty()) {
            throw std::runtime_error("TpcCluster: cu_template must be set");
        }
        nlohmann::json compute_grp_entry;
        compute_grp_entry["name"] = "compute_grp";
        compute_grp_entry["type"] = "ComputeCluster";
        compute_grp_entry["params"] = {{"cu_template", cu_template_path_},
                                       {"cu_count", cu_per_tpc_}};
        compute_grp_entry["modules"] = nlohmann::json::array();
        compute_grp_entry["connections"] = nlohmann::json::array();
        nlohmann::json compute_grp_cfg;
        compute_grp_cfg["modules"] = nlohmann::json::array({compute_grp_entry});
        compute_grp_cfg["connections"] = nlohmann::json::array();
        internal_factory->instantiateAll(compute_grp_cfg);
    }

} // namespace cpptlm::tlm
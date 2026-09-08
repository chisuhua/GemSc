// src/tlm/cluster/gpu_cluster.cc
#include "tlm/cluster/gpu_cluster.hh"
#include "core/module_factory.hh"

namespace cpptlm::tlm {

    void GpuCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("gpc_count"))
            gpc_count_ = params["gpc_count"].get<int>();
        if (params.contains("tpc_per_gpc"))
            tpc_per_gpc_ = params["tpc_per_gpc"].get<int>();
        if (params.contains("cu_per_tpc"))
            cu_per_tpc_ = params["cu_per_tpc"].get<int>();
        if (params.contains("cu_template"))
            cu_template_path_ = params["cu_template"].get<std::string>();
    }

    void GpuCluster::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 双激活 (instantiateAll Step 4.5 + SimModule 递归) 下第二次进入时
        // 直接返回, 避免二次生成覆盖 instances map 泄漏第一批子树 (fix-asan 根因 2)。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        nlohmann::json gpc_cfgs = nlohmann::json::array();
        for (int i = 0; i < gpc_count_; ++i) {
            nlohmann::json gpc_entry;
            gpc_entry["name"] = "gpc" + std::to_string(i);
            gpc_entry["type"] = "GpcCluster";
            gpc_entry["params"] = {{"gpc_id", i},
                                   {"tpc_per_gpc", tpc_per_gpc_},
                                   {"cu_per_tpc", cu_per_tpc_},
                                   {"cu_template", cu_template_path_}};
            gpc_entry["modules"] = nlohmann::json::array();
            gpc_entry["connections"] = nlohmann::json::array();
            gpc_cfgs.push_back(gpc_entry);
        }
        nlohmann::json gpu_cfg;
        gpu_cfg["modules"] = gpc_cfgs;
        gpu_cfg["connections"] = nlohmann::json::array();
        internal_factory->instantiateAll(gpu_cfg);
    }

} // namespace cpptlm::tlm
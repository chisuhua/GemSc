// src/tlm/cluster/compute_cluster.cc
// ComputeCluster 实现 - JSON 蓝图模板 + N 份实例化
#include "tlm/cluster/compute_cluster.hh"
#include <stdexcept>
#include "core/module_factory.hh"
#include "utils/json_includer.hh"

namespace cpptlm::tlm {

    void ComputeCluster::set_config(const nlohmann::json& params) {
        SimModule::set_config(params);
        if (params.contains("cu_template") && !params["cu_template"].get<std::string>().empty()) {
            cu_template_path_ = params["cu_template"].get<std::string>();
        }
        if (params.contains("cu_count")) {
            cu_count_ = params["cu_count"].get<int>();
            if (cu_count_ < 1) {
                cu_count_ = 1;
                DPRINTF(MODULE, "[WARN] ComputeCluster: cu_count < 1, clamped to 1\n");
            }
            if (cu_count_ > 64) {
                cu_count_ = 64;
                DPRINTF(MODULE, "[WARN] ComputeCluster: cu_count > 64, clamped to 64\n");
            }
        }
    }

    void ComputeCluster::simulate_instantiate(const nlohmann::json& cfg) {
        // 幂等守卫: 防双激活二次生成覆盖 instances map 泄漏第一批子树 (fix-asan 根因 2)。
        if (!internal_factory->getAllInstances().empty()) {
            return;
        }
        SimModule::simulate_instantiate(cfg);
        // P1 fix: 子 ComputeCluster 场景下 cu_template_path_ 为空 (默认),
        // 但父 ComputeCluster 已在 cu_entry["modules"] 提供蓝图实例, 通过基类
        // SimModule::simulate_instantiate(cfg) 已实例化 cfg.modules.
        // 此处不再构造额外 cu_cfg (避免 cu_count_ 重复实例化).
        if (cu_template_path_.empty())
            return;
        auto tmpl = JsonIncluder::loadAndInclude(cu_template_path_);
        if (!tmpl.contains("modules")) {
            throw std::runtime_error("ComputeCluster: cu_template must contain 'modules' array: " +
                                     cu_template_path_);
        }
        nlohmann::json cu_cfg;
        cu_cfg["modules"] = nlohmann::json::array();
        cu_cfg["connections"] = nlohmann::json::array();
        for (int i = 0; i < cu_count_; ++i) {
            nlohmann::json cu_entry;
            cu_entry["name"] = "cu" + std::to_string(i);
            cu_entry["type"] = "ComputeCluster";
            cu_entry["params"] = {{"cu_template", ""}, {"cu_count", 0}};
            cu_entry["modules"] = tmpl["modules"];
            cu_entry["connections"] = nlohmann::json::array();
            cu_cfg["modules"].push_back(cu_entry);
        }
        internal_factory->instantiateAll(cu_cfg);
    }

} // namespace cpptlm::tlm
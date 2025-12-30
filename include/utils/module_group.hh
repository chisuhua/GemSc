// include/utils/module_group.hh
#ifndef MODULE_GROUP_HH
#define MODULE_GROUP_HH

#include <unordered_map>
#include <vector>
#include <string>
#include "core/sim_core.hh"

class ModuleGroup {
private:
    static std::unordered_map<std::string, std::vector<std::string>>& getGroups() {
        static std::unordered_map<std::string, std::vector<std::string>> groups;
        return groups;
    }

public:
    // 定义组
    static void define(const std::string& name, const std::vector<std::string>& members) {
        getGroups()[name] = members;
        DPRINTF(GROUP, "[Group] Defined group '%s' with %zu members\n", name.c_str(), members.size());
    }

    // 添加成员
    static void addMember(const std::string& group_name, const std::string& member) {
        getGroups()[group_name].push_back(member);
    }

    // 获取组成员
    static std::vector<std::string> getMembers(const std::string& name) {
        auto it = getGroups().find(name);
        return it != getGroups().end() ? it->second : std::vector<std::string>{};
    }

    // 是否是组引用
    static bool isGroupReference(const std::string& str) {
        return str.rfind("group:", 0) == 0;
    }

    // 解析组名
    static std::string extractGroupName(const std::string& str) {
        if (isGroupReference(str)) {
            return str.substr(6);
        }
        return "";
    }

    // 根据组名获取模块名列表
    static std::vector<std::string> resolve(const std::string& group_ref) {
        if (!isGroupReference(group_ref)) return {};
        std::string name = extractGroupName(group_ref);
        return getMembers(name);
    }

    static void clearAll() {
        getGroups().clear();
    }

    static std::vector<std::string> getAllGroupNames() {
        std::vector<std::string> names;
        for (const auto& kv : getGroups()) {
            names.push_back(kv.first);
        }
        return names;
    }

    // 列出所有组（调试用）
    static void listAllGroups() {
        printf("[ModuleGroup] Defined groups:\n");
        for (const auto& kv : getGroups()) {
            printf("  %s: ", kv.first.c_str());
            for (size_t i = 0; i < kv.second.size(); ++i) {
                printf("%s%s", kv.second[i].c_str(), i == kv.second.size()-1 ? "\n" : ", ");
            }
        }
    }
};
#endif

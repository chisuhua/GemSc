// include/utils/regex_matcher.hh
#ifndef REGEX_MATCHER_HH
#define REGEX_MATCHER_HH

#include <string>
#include <regex>
#include <vector>
#include <nlohmann/json.hpp>
#include "utils/wildcard.hh"

using json = nlohmann::json;

class RegexMatcher {
public:
    // 检查是否是正则模式（以 regex: 开头）
    static bool isRegexPattern(const std::string& pattern) {
        return pattern.rfind("regex:", 0) == 0;
    }

    // 提取正则表达式内容
    static std::string extractRegex(const std::string& pattern) {
        if (isRegexPattern(pattern)) {
            return pattern.substr(6);
        }
        return "";
    }

    // 执行匹配
    static bool match(const std::string& pattern, const std::string& str) {
        if (isRegexPattern(pattern)) {
            try {
                std::regex re(extractRegex(pattern));
                return std::regex_match(str, re);
            } catch (const std::regex_error&) {
                return false;
            }
        }
        // 否则使用通配符匹配
        return Wildcard::match(pattern, str);
    }
};

// 工具函数：从列表中移除被排除的项
inline std::vector<std::string> filterExcluded(
    const std::vector<std::string>& candidates,
    const json& exclude_list)
{
    std::vector<std::string> result;
    for (const std::string& name : candidates) {
        bool excluded = false;
        for (auto& excl : exclude_list) {
            std::string pattern = excl.get<std::string>();
            if (Wildcard::match(pattern, name) || 
                (RegexMatcher::isRegexPattern(pattern) && 
                 RegexMatcher::match(pattern, name))) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            result.push_back(name);
        }
    }
    return result;
}
#endif
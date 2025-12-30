// test/test_module_group.cc
#include "catch_amalgamated.hpp"
#include "utils/module_group.hh"

TEST_CASE("ModuleGroupTest DefineAndResolve", "[module][group]") {
    std::vector<std::string> members = {"cpu0", "cpu1", "cpu2"};
    ModuleGroup::define("cpus", members);

    auto resolved = ModuleGroup::getMembers("cpus");
    REQUIRE(resolved.size() == 3);
    REQUIRE(resolved[0] == "cpu0");

    auto empty = ModuleGroup::getMembers("not_exist");
    REQUIRE(empty.empty());
}

TEST_CASE("ModuleGroupTest GroupReferenceParsing", "[module][group]") {
    REQUIRE(ModuleGroup::isGroupReference("group:cpus"));
    REQUIRE_FALSE(ModuleGroup::isGroupReference("cpus"));

    REQUIRE(ModuleGroup::extractGroupName("group:cpus") == "cpus");
    REQUIRE(ModuleGroup::extractGroupName("group:") == "");
}
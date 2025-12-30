// test/test_wildcard_matching.cc
#include "catch_amalgamated.hpp"
#include "utils/wildcard.hh"

TEST_CASE("WildcardTest BasicMatching", "[wildcard][matching]") {
    REQUIRE(Wildcard::match("cpu*", "cpu0"));
    REQUIRE(Wildcard::match("cpu*", "cpu_core_1"));
    REQUIRE_FALSE(Wildcard::match("cpu*", "gpu0"));

    REQUIRE(Wildcard::match("gpu?", "gpu0"));
    REQUIRE(Wildcard::match("gpu?", "gpu9"));
    REQUIRE_FALSE(Wildcard::match("gpu?", "gpu10"));
    REQUIRE_FALSE(Wildcard::match("gpu?", "gpa0"));

    REQUIRE(Wildcard::match("mem.ctrl", "mem.ctrl"));
    REQUIRE_FALSE(Wildcard::match("mem.ctrl", "memxctrl"));
}
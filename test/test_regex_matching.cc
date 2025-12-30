// test/test_regex_matching.cc
#include "catch_amalgamated.hpp"
#include "utils/regex_matcher.hh"

TEST_CASE("RegexMatcherTest RegexPatternDetection", "[regex][matching]") {
    REQUIRE(RegexMatcher::isRegexPattern("regex:cpu[0-3]"));
    REQUIRE_FALSE(RegexMatcher::isRegexPattern("cpu*"));
}

TEST_CASE("RegexMatcherTest RegexMatching", "[regex][matching]") {
    REQUIRE(RegexMatcher::match("regex:cpu[0-3]", "cpu0"));
    REQUIRE(RegexMatcher::match("regex:cpu[0-3]", "cpu3"));
    REQUIRE_FALSE(RegexMatcher::match("regex:cpu[0-3]", "cpu4"));
    REQUIRE_FALSE(RegexMatcher::match("regex:cpu[0-3]", "gpu0"));

    REQUIRE(RegexMatcher::match("regex:node\\d+", "node1"));
    REQUIRE(RegexMatcher::match("regex:node\\d+", "node123"));
    REQUIRE_FALSE(RegexMatcher::match("regex:node\\d+", "node"));
}

TEST_CASE("RegexMatcherTest FallbackToWildcard", "[regex][matching]") {
    REQUIRE(RegexMatcher::match("cpu*", "cpu0"));
    REQUIRE_FALSE(RegexMatcher::match("invalid[", "cpu0"));  // invalid regex → fallback to wildcard
}
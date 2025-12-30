// test/test_json_includer.cc
#include "catch_amalgamated.hpp"
#include "utils/json_includer.hh"
#include <fstream>
#include <cstdlib>

TEST_CASE("JsonIncluderTest IncludeMergesContent", "[json][includer]") {
    // 创建临时目录和文件
    system("mkdir -p tmp/configs");
    std::ofstream base("tmp/configs/base.json");
    base << R"({
        "include": "common.json",
        "extra": "value"
    })";
    base.close();

    std::ofstream common("tmp/configs/common.json");
    common << R"({
        "modules": [
            { "name": "cpu0", "type": "MockSim" },
            { "name": "l1", "type": "MockSim" }
        ],
        "connections": [
            { "src": "cpu0", "dst": "l1", "latency": 2 }
        ]
    })";
    common.close();

    json config = JsonIncluder::loadAndInclude("tmp/configs/base.json");

    REQUIRE(config.contains("modules"));
    REQUIRE(config.contains("connections"));
    REQUIRE(config.contains("extra"));

    REQUIRE(config["modules"].size() == 2);
    REQUIRE(config["connections"][0]["src"] == "cpu0");
    REQUIRE(config["extra"] == "value");

    // 清理临时文件
    system("rm -rf tmp");
}

TEST_CASE("JsonIncluderTest RelativePathResolution", "[json][includer]") {
    // 创建临时目录和文件
    system("mkdir -p tmp/configs");
    std::ofstream base("tmp/configs/base.json");
    base << R"({
        "include": "common.json"
    })";
    base.close();

    std::ofstream common("tmp/configs/common.json");
    common << R"({
        "modules": [
            { "name": "cpu0", "type": "MockSim" }
        ]
    })";
    common.close();

    json config = JsonIncluder::loadAndInclude("tmp/configs/base.json");
    REQUIRE(config["modules"][0]["name"] == "cpu0");

    // 清理临时文件
    system("rm -rf tmp");
}
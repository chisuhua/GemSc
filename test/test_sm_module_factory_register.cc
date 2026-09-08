// test/test_sm_module_factory_register.cc
// 验证 SM 顶层 + 12 子模块已在 ModuleFactory 注册 (per plan Task 8 Step 3)
//
// 作者 CppTLM Team / 日期 2027-02-09 (SM 重构 Task 8)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

#include <algorithm>
#include <string>

TEST_CASE("SM 顶层 + 12 子模块在 ModuleFactory 注册", "[sm-module-register][sm-microarch]") {
    auto types = ModuleFactory::getRegisteredObjectTypes();
    std::vector<std::string> expected = {
        "StreamingMultiprocessorTLM",
        "FetchUnitTLM",
        "DecodeUnitTLM",
        "IssueUnitTLM",
        "ScalarALU",
        "VectorALU",
        "MatrixCore",
        "SIMTLane",
        "LsuGlobal",
        "LsuLDS",
        "RegFileUnit",
        "WritebackUnit",
        "HazardTracker",
    };
    for (const auto& name : expected) {
        INFO("Missing: " << name);
        REQUIRE(std::find(types.begin(), types.end(), name) != types.end());
    }
}

TEST_CASE("StreamingMultiprocessorTLM 直接实例化 + get_module_type",
          "[sm-module-register][sm-microarch]") {
    EventQueue eq;
    tlm::StreamingMultiprocessorTLM sm("sm0", &eq);
    REQUIRE(sm.get_module_type() == "StreamingMultiprocessorTLM");
}

TEST_CASE("12 SM 子模块直接实例化", "[sm-module-register][sm-microarch]") {
    EventQueue eq;
    REQUIRE(tlm::sm::FetchUnitTLM("fu", &eq).get_module_type() == "FetchUnitTLM");
    REQUIRE(tlm::sm::DecodeUnitTLM("du", &eq).get_module_type() == "DecodeUnitTLM");
    REQUIRE(tlm::sm::IssueUnitTLM("iu", &eq).get_module_type() == "IssueUnitTLM");
    REQUIRE(tlm::sm::ScalarALU("sa", &eq).get_module_type() == "ScalarALU");
    REQUIRE(tlm::sm::VectorALU("va", &eq).get_module_type() == "VectorALU");
    REQUIRE(tlm::sm::MatrixCore("mc", &eq).get_module_type() == "MatrixCore");
    REQUIRE(tlm::sm::SIMTLane("sl", &eq).get_module_type() == "SIMTLane");
    REQUIRE(tlm::sm::LsuGlobal("lg", &eq).get_module_type() == "LsuGlobal");
    REQUIRE(tlm::sm::LsuLDS("ll", &eq).get_module_type() == "LsuLDS");
    REQUIRE(tlm::sm::RegFileUnit("rf", &eq).get_module_type() == "RegFileUnit");
    REQUIRE(tlm::sm::WritebackUnit("wb", &eq).get_module_type() == "WritebackUnit");
    REQUIRE(tlm::sm::HazardTracker("ht", &eq).get_module_type() == "HazardTracker");
}

TEST_CASE("13 SM 类型 ModuleFactory 注册总计 (sanity check)",
          "[sm-module-register][sm-microarch]") {
    auto types = ModuleFactory::getRegisteredObjectTypes();
    int sm_count = 0;
    for (const auto& name : types) {
        if (name == "StreamingMultiprocessorTLM" || name == "FetchUnitTLM" ||
            name == "DecodeUnitTLM" || name == "IssueUnitTLM" || name == "ScalarALU" ||
            name == "VectorALU" || name == "MatrixCore" || name == "SIMTLane" ||
            name == "LsuGlobal" || name == "LsuLDS" || name == "RegFileUnit" ||
            name == "WritebackUnit" || name == "HazardTracker") {
            ++sm_count;
        }
    }
    REQUIRE(sm_count == 13);
}
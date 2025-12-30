// test/test_module_registration.cc
#include "catch_amalgamated.hpp"
#include "core/module_factory.hh"
#include "core/event_queue.hh"

// 测试专用 Mock 模块
class TestModuleA : public SimObject {
public:
    explicit TestModuleA(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}
    void tick() override {}
};

class TestModuleB : public SimObject {
public:
    explicit TestModuleB(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}
    void tick() override {}
};

TEST_CASE("Module Registration and Instantiation Tests", "[module][factory]") {
    EventQueue eq;
    ModuleFactory::clearAllTypes(); // 确保测试开始前是干净的

    SECTION("Register and unregister single module type") {
        // 注册
        ModuleFactory::registerObject<TestModuleA>("TestModuleA");
        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 1);

        // 验证注册的类型存在
        auto types = ModuleFactory::getRegisteredObjectTypes();
        REQUIRE(std::find(types.begin(), types.end(), "TestModuleA") != types.end());

        // 注销
        bool success = ModuleFactory::unregisterObject("TestModuleA");
        REQUIRE(success == true);
        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 0);

        // 再次注销应返回 false
        success = ModuleFactory::unregisterObject("TestModuleA");
        REQUIRE(success == false);
    }

    SECTION("Register multiple types and clear all") {
        ModuleFactory::registerObject<TestModuleA>("TestModuleA");
        ModuleFactory::registerObject<TestModuleB>("TestModuleB");

        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 2);

        ModuleFactory::clearAllTypes();

        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 0);
    }

    SECTION("Instantiate module after registration") {
        ModuleFactory::registerObject<TestModuleA>("TestModuleA");

        json config = R"({
            "modules": [
                { "name": "inst0", "type": "TestModuleA" }
            ],
            "connections": []
        })"_json;

        ModuleFactory factory(&eq);
        REQUIRE_NOTHROW(factory.instantiateAll(config));

        SimObject* obj = factory.getInstance("inst0");
        REQUIRE(obj != nullptr);
        REQUIRE(obj->getName() == "inst0");
    }

    SECTION("Test isolation - initial state should be clean") {
        // 测试开始时应该是干净的
        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 0);

        ModuleFactory::registerObject<TestModuleA>("TestModuleA");
        REQUIRE(ModuleFactory::getRegisteredObjectTypes().size() == 1);

        // 测试结束时会被自动清理（SECTION 结束作用域）
    }

    // 确保清理
    ModuleFactory::clearAllTypes();
}
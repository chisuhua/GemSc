// main.cpp
#include <iostream>
#include "sim_core.hh"
#include "event_queue.hh"
#include "module_factory.hh"
#include "utils/json_includer.hh"  // 包含 loadConfig
#include "utils/topology_dumper.hh"  // 包含 loadConfig
#include "modules.hh"

extern "C" int sc_main(int argc, char* argv[]) {
    // 空函数，只是为了满足链接器需求
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>\n";
        return 1;
    }

    EventQueue eq;

    REGISTER_OBJECT
    REGISTER_MODULE
    ModuleFactory::listRegisteredTypes();


    // 加载配置文件
    json config = JsonIncluder::loadAndInclude(argv[1]);

    // 构建系统
    ModuleFactory factory(&eq);
    factory.instantiateAll(config);
    factory.startAllTicks();

    TopologyDumper::dumpToDot(factory, config, "topology.dot");

    // 运行仿真
    eq.run(10000);

    std::cout << "\n[INFO] Simulation finished.\n";
    return 0;
}
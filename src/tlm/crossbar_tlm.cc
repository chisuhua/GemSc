// src/tlm/crossbar_tlm.cc
// CrossbarTLM 实现 + P3 helper 方法
// 功能描述: 包含 CrossbarTLM 的 ChStream helper 方法实现 (P3 partial, 不依赖 D.1)
// 作者: CppTLM Team
// 日期: 2026-06-19
#include "tlm/crossbar_tlm.hh"
#include <stdexcept>
#include "core/chstream_module.hh"
#include "core/master_port.hh"
#include "core/port_manager.hh"
#include "core/simple_port.hh"
#include "core/slave_port.hh"

void CrossbarTLM::connectCPUSideBus(ChStreamModuleBase* bus) {
    if (!bus) {
        throw std::runtime_error("CrossbarTLM::connectCPUSideBus: bus is null");
    }

    auto* cpu_side = getPortManager().getUpstreamPort("cpu_side");
    if (!cpu_side) {
        throw std::runtime_error(
            "CrossbarTLM::connectCPUSideBus: upstream port 'cpu_side' not registered. "
            "Ensure JSON config defines 'cpu_side' or call addUpstreamPort() "
            "before connectCPUSideBus().");
    }
    auto* bus_mem_side = bus->getPortManager().getUpstreamPort("mem_side");
    if (!bus_mem_side) {
        throw std::runtime_error(
            "CrossbarTLM::connectCPUSideBus: bus upstream port 'mem_side' not registered. "
            "Bus must declare 'mem_side' port in JSON or addUpstreamPort().");
    }

    helper_pairs_.emplace_back(std::make_unique<PortPair>(cpu_side, bus_mem_side));
}

void CrossbarTLM::connectMemSideBus(ChStreamModuleBase* bus) {
    if (!bus) {
        throw std::runtime_error("CrossbarTLM::connectMemSideBus: bus is null");
    }

    auto* mem_side = getPortManager().getUpstreamPort("mem_side");
    if (!mem_side) {
        throw std::runtime_error(
            "CrossbarTLM::connectMemSideBus: upstream port 'mem_side' not registered. "
            "Ensure JSON config defines 'mem_side' or call addUpstreamPort() "
            "before connectMemSideBus().");
    }
    auto* bus_cpu_side = bus->getPortManager().getUpstreamPort("cpu_side");
    if (!bus_cpu_side) {
        throw std::runtime_error(
            "CrossbarTLM::connectMemSideBus: bus upstream port 'cpu_side' not registered. "
            "Bus must declare 'cpu_side' port in JSON or addUpstreamPort().");
    }

    helper_pairs_.emplace_back(std::make_unique<PortPair>(mem_side, bus_cpu_side));
}

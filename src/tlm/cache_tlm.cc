// src/tlm/cache_tlm.cc
// CacheTLM 实现 + P3 helper 方法
// 功能描述: 包含 CacheTLM 的 ChStream helper 方法实现 (P3 partial, 不依赖 D.1)
// 作者: CppTLM Team
// 日期: 2026-06-19
#include "tlm/cache_tlm.hh"
#include <stdexcept>
#include "core/chstream_module.hh"
#include "core/master_port.hh"
#include "core/port_manager.hh"
#include "core/simple_port.hh"
#include "core/slave_port.hh"

// P0 修复: 删 lazy registration. port 必须预注册, 缺失立刻报清晰错误
// 符合零债务原则: 配置错误早暴露优于静默 fallback
void CacheTLM::connectBus(ChStreamModuleBase* bus) {
    if (!bus) {
        throw std::runtime_error("CacheTLM::connectBus: bus is null");
    }

    // 检查 port 必须预注册 (无 lazy fallback)
    auto* mem_side = getPortManager().getUpstreamPort("mem_side");
    if (!mem_side) {
        throw std::runtime_error("CacheTLM::connectBus: upstream port 'mem_side' not registered. "
                                 "Ensure JSON config defines 'mem_side' or call addUpstreamPort() "
                                 "before connectBus().");
    }
    auto* bus_port = bus->getPortManager().getUpstreamPort("cpu_side");
    if (!bus_port) {
        throw std::runtime_error(
            "CacheTLM::connectBus: bus upstream port 'cpu_side' not registered. "
            "Bus must declare 'cpu_side' port in JSON or addUpstreamPort().");
    }

    helper_pairs_.emplace_back(std::make_unique<PortPair>(mem_side, bus_port));
}

// include/systemc_adapter.hh
#ifndef SYSTEMC_ADAPTER_HH
#define SYSTEMC_ADAPTER_HH

#include "../sim_object.hh"
#include "sc_multiport_object.hh"
#include <memory>

// 用户定义的核心模块
//SC_MODULE(RtlCore) {
//    // ... 具有模板化端口的实现 ...
//};

// 定义端口类型
using Inputs = std::tuple<ReadCmdExt, ReadCmdExt>;
using Outputs = std::tuple<ReadRespExt, ReadRespExt>;

using MyMultiPortScObject = sc_core::MultiPortScObject<RtlCore, Inputs, Outputs>;

class SystemcAdapter : public SimObject {
private:
    std::unique_ptr<MyMultiPortScObject> sc_object;

public:
    explicit SystemcAdapter(const std::string& n, EventQueue* eq);
    ~SystemcAdapter() override;

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override;
    void tick() override;
};

// src/systemc_adapter.cc
#include "systemc_adapter.hh"

SystemcAdapter::SystemcAdapter(const std::string& n, EventQueue* eq)
    : SimObject(n, eq), sc_object(nullptr) {
    
    try {
        sc_object = std::make_unique<MyMultiPortScObject>("multi_port_sc_object");
        DPRINTF(ADAPTER, "[%s] Initialized\n", name().c_str());
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
    }
}

bool SystemcAdapter::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    if (!sc_object || !pkt) {
        delete pkt;
        return false;
    }

    if (src_id >= (int)std::tuple_size_v<Inputs>) {
        delete pkt;
        return false;
    }

    auto& to_adapter = std::get<src_id>(sc_object->to_payload_adapters);
    to_adapter.pkt_in.write(pkt);
    to_adapter.valid_in.write(true);
    return true;
}

void SystemcAdapter::tick() override {
    if (!sc_object) return;

    sc_start(sc_time(1, SC_NS));

    for (auto& valid : sc_object->valid_in) {
        valid.write(false);
    }

    for (size_t i = 0; i < std::tuple_size_v<Outputs>; ++i) {
        auto& from_adapter = std::get<i>(sc_object->from_payload_adapters);
        if (from_adapter.valid_out.read()) {
            Packet* pkt = from_adapter.pkt_out.read();
            auto out_port = getPortManager().getDownstreamPorts()[i];
            if (out_port && out_port->sendReq(pkt)) {
                // Success
            }
        }
    }
}

REGISTER_MODULE(SystemcAdapter, "SystemcAdapter")

#endif // SYSTEMC_ADAPTER_HH

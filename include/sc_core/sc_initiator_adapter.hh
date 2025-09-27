// include/sc_initiator_adapter.hh
#ifndef SC_INITIATOR_ADAPTER_HH
#define SC_INITIATOR_ADAPTER_HH

#include "../sim_object.hh"
#include "sc_multiport_initiator.hh"
#include <memory>

// 函数对象：根据 payload 创建新的 Packet
template<typename ExtType>
struct NewPayloadPacket {
    Packet* operator()(const typename ExtType::payload_t& payload, uint64_t sid, uint64_t sqn) {
        Packet* pkt = PacketPool::alloc();
        // 设置元数据...
        
        auto* ext = new ExtType();
        ext->data = payload;
        pkt->set_extension(ext);
        return pkt;
    }
};

// 用户定义的核心模块
SC_MODULE(RtlDmaEngine) {
    // ...
};

// 定义输出端口类型
using Outputs = std::tuple<ReadCmdExt, WriteCmdExt>;

using MyScInitiator = sc_core::ScMultiPortInitiator<RtlDmaEngine, Outputs>;

class ScInitiatorAdapter : public SimObject {
private:
    std::unique_ptr<MyScInitiator> initiator;

public:
    explicit ScInitiatorAdapter(const std::string& n, EventQueue* eq);
    ~ScInitiatorAdapter() override;

    void tick() override;
};

REGISTER_MODULE(ScInitiatorAdapter, "ScInitiatorAdapter")

#endif // SC_INITIATOR_ADAPTER_HH

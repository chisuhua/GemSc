// packet.hh
#ifndef PACKET_HH
#define PACKET_HH

#include "tlm.h"
#include <cstdint>
#include <unordered_map>

class PacketPool;

enum CmdType {
    CMD_INVALID = 0,
    CMD_READ    = 1,
    CMD_WRITE   = 2
};

enum PacketType {
    PKT_REQ,
    PKT_RESP,
    PKT_STREAM_DATA,
    PKT_CREDIT_RETURN
};

class Packet {
    friend class PacketPool; // 修正拼写错误: frind -> friend
public:
    tlm::tlm_generic_payload* payload;
    // 流控相关
    uint64_t stream_id = 0;
    uint64_t seq_num = 0;
    CmdType cmd; // 修正拼写错误: CmdT ype -> CmdType
    PacketType type;

    // 时间统计
    uint64_t src_cycle;
    uint64_t dst_cycle;           // 接收时间（用于延迟统计）

    Packet* original_req = nullptr;
    std::vector<Packet*> dependents; // 修正空格: < < -> <>

    // routing
    std::vector<std::string> route_path; // 修正空格: < < -> <>
    int hop_count = 0;
    uint8_t priority = 0;
    uint64_t flow_id = 0;

    int vc_id = 0;

    bool isRequest() const { return type == PKT_REQ; } // 修正枚举值: PKT_REQ_READ -> PKT_REQ
    bool isResponse() const { return type == PKT_RESP; } // 修正空格: i sResponse -> isResponse
    bool isStream() const { return type == PKT_STREAM_DATA; }
    bool isCredit() const { return type == PKT_CREDIT_RETURN; }

    uint64_t getDelayCycles() const { // 修正拼写和空格: ui nt64_t -> uint64_t, > > -> >=
        return (dst_cycle >= src_cycle) ? (dst_cycle - src_cycle) : 0;
    }

    uint64_t getEnd2EndCycles() const {
        return original_req ? (dst_cycle - original_req->src_cycle) : getDelayCycles();
    }

private:
    // 引用计数，确保 original_req 指向的对象不会被过早回收
    int ref_count = 0;

    // 增加一个 reset 方法来重置状态
    void reset() {
        if (payload && !isCredit()) {
            delete payload;
        }
        payload = nullptr;
        stream_id = 0;
        seq_num = 0;
        cmd = CMD_INVALID;
        type = PKT_REQ;
        src_cycle = 0;
        dst_cycle = 0;
        original_req = nullptr;
        dependents.clear();
        route_path.clear();
        hop_count = 0;
        priority = 0;
        flow_id = 0;
        vc_id = 0;
        ref_count = 0; // 重置引用计数
    }

    // 私有构造函数，由 PacketPool 管理
    Packet(tlm::tlm_generic_payload* p, uint64_t cycle, PacketType t)
        : payload(p), src_cycle(cycle), type(t), ref_count(0) {}

    ~Packet() {
        // 析构函数不再负责删除 payload 和 original_req
        // 这些操作由 PacketPool 在 release 时完成
    }

    // 友元类可以访问私有成员，包括增加和减少引用计数
    friend class PacketPool;
};

#endif // PACKET_HH

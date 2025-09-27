// packet/packet.cc
#include "packet.hh"
#include <iostream>

Packet::Packet() 
    : payload(nullptr), stream_id(0), seq_num(0)
    , addr(0), size(0), src_id(0), dest_id(0)
    , cmd(CMD_INVALID), type(PKT_REQ)
    , m_is_dynamic(false)
{
    payload = new tlm::tlm_generic_payload();
}

Packet::Packet(tlm::tlm_generic_payload* p)
    : payload(p), stream_id(0), seq_num(0)
    , addr(0), size(0), src_id(0), dest_id(0)
    , cmd(CMD_INVALID), type(PKT_REQ)
    , m_is_dynamic(false)
{}

Packet::~Packet() {
    // 注意：~tlm_generic_payload() 会 delete data_ptr
    if (payload && m_is_dynamic) {
        delete payload;
    }
    // 不 delete this，由 Pool 或用户管理
}

void Packet::reset() {
    // 保留 payload 指针，但重置其内容由 Pool 控制
    stream_id = 0;
    seq_num = 0;
    addr = 0;
    size = 0;
    src_id = 0;
    dest_id = 0;
    cmd = CMD_INVALID;
    type = PKT_REQ;
    // payload 不在此 reset，由 TLM 自身管理
}

void Packet::release() {
    PacketPool::get().release(this);
}

void Packet::print(sc_core::sc_trace_file* tf) const {
    std::cout << "Packet{"
              << "sid:" << stream_id 
              << ", sqn:" << seq_num
              << ", addr:0x" << std::hex << addr << std::dec
              << ", size:" << size
              << ", cmd:" << (cmd == CMD_READ ? "READ" : cmd == CMD_WRITE ? "WRITE" : "INVALID")
              << ", type:" << (type == PKT_REQ ? "REQ" : "RESP")
              << ", src:" << src_id << "->" << dest_id
              << "}";
    if (tf) {
        // 可添加波形追踪
    }
    std::cout << std::endl;
}

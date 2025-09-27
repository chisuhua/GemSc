// adapters/payload_to_packet.hh
#ifndef PAYLOAD_TO_PACKET_HH
#define PAYLOAD_TO_PACKET_HH

#include <systemc>
#include <unordered_map>
#include "packet.hh"
#include "mem_exts.hh"

/**
 * @brief 通用适配器：将 (POD, sid, sqn) 封装回新的 Packet*
 * 用于 RTL 模块输出响应
 * 我们假设 T 有一个 public 成员 data，类型为 data_t
 * 因此 typename T::data_t 是合法的
 *
 */
template<typename T>
struct PayloadToPacket : sc_core::sc_module {
    // 输入来自 RTL 模块
    sc_core::sc_in<bool> clk;
    sc_core::sc_in<typename T::data_t> payload_in;
    sc_core::sc_in<uint64_t> stream_id_in;
    sc_core::sc_in<uint64_t> seq_num_in;
    sc_core::sc_in<bool> valid_in;

    // 输出
    sc_core::sc_out<Packet*> pkt_out;
    sc_core::sc_out<bool> valid_out;

    // 请求缓存表：sid+sqn → 请求包（用于恢复 src/dest 等）
    std::unordered_map<std::pair<uint64_t, uint64_t>, Packet*, 
                       std::hash<uint64_t>> pending_reqs;

    void cache_request(Packet* req_pkt) {
        if (req_pkt) {
            auto key = std::make_pair(req_pkt->stream_id, req_pkt->seq_num);
            pending_reqs[key] = req_pkt;
        }
    }

    void clear_request(uint64_t sid, uint64_t sqn) {
        auto key = std::make_pair(sid, sqn);
        pending_reqs.erase(key);
    }

    void encapsulate() {
        if (!valid_in.read()) {
            valid_out.write(false);
            pkt_out.write(nullptr);
            return;
        }

        auto resp_data = payload_in.read();
        uint64_t sid = stream_id_in.read();
        uint64_t sqn = seq_num_in.read();

        auto key = std::make_pair(sid, sqn);
        auto it = pending_reqs.find(key);
        if (it == pending_reqs.end()) {
            valid_out.write(false);
            pkt_out.write(nullptr);
            return;
        }

        Packet* req_pkt = it->second;

        // 创建响应包
        Packet* resp_pkt = PacketPool::get().acquire();
        resp_pkt->addr = req_pkt->addr;
        resp_pkt->size = req_pkt->size;
        resp_pkt->src_id = req_pkt->dest_id;
        resp_pkt->dest_id = req_pkt->src_id;
        resp_pkt->stream_id = sid;
        resp_pkt->seq_num = sqn;
        resp_pkt->cmd = CMD_INVALID;
        resp_pkt->type = PKT_RESP;

        // 设置扩展
        auto* ext = new T();
        ext->data = resp_data;
        resp_pkt->set_extension(ext);

        // 输出
        pkt_out.write(resp_pkt);
        valid_out.write(true);

        // 清理缓存
        pending_reqs.erase(it);
    }

    SC_CTOR(PayloadToPacket) {
        SC_METHOD(encapsulate);
        sensitive << clk.pos();
    }
};

// 特化：提取 data_t 类型
template<>
struct PayloadToPacket<ReadRespExt> {
    using data_t = ReadResp;
};

template<>
struct PayloadToPacket<WriteRespExt> {
    using data_t = WriteResp;
};


#endif // PAYLOAD_TO_PACKET_HH

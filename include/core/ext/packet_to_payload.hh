// adapters/packet_to_payload.hh
#ifndef GEMSC_ADAPTERS_PACKET_TO_PAYLOAD_HH
#define GEMSC_ADAPTERS_PACKET_TO_PAYLOAD_HH

#include <systemc>
#include "packet/packet.hh"
#include "cmd_exts.hh"

/**
 * @brief 通用适配器：从 Packet* 中提取 T 扩展的数据 + sid/sqn
 * 输出给可综合模块使用
 *
 * 使用方式：
 *   PacketToPayload<ReadCmdExt> to_rtl("to_rtl");
 *   to_rtl.clk(clk);
 *   to_rtl.pkt_in(pkt_in);
 *   to_rtl.valid_in(valid_in);
 *   to_rtl.payload_out(cmd_out);
 *   to_rtl.stream_id_out(sid_out);
 *   to_rtl.seq_num_out(sqn_out);
 *   to_rtl.valid_out(valid_out);
 */
template<typename T>
struct PacketToPayload : sc_core::sc_module {
    // 输入
    sc_core::sc_in<bool> clk;
    sc_core::sc_in<Packet*> pkt_in;
    sc_core::sc_in<bool> valid_in;

    // 输出给 RTL 模块
    using payload_t = typename std::remove_reference<decltype(std::declval<T>().data)>::type;
    sc_core::sc_out<payload_t> payload_out;
    sc_core::sc_out<uint64_t> stream_id_out;
    sc_core::sc_out<uint64_t> seq_num_out;
    sc_core::sc_out<bool> valid_out;

    void extract() {
        if (!valid_in.read()) {
            valid_out.write(false);
            return;
        }

        Packet* pkt = pkt_in.read();
        if (!pkt || !pkt->payload) {
            valid_out.write(false);
            return;
        }

        // 获取扩展
        auto* ext = pkt->get_extension<T>();
        if (!ext) {
            valid_out.write(false);
            return;
        }

        // 提取数据
        payload_out.write(ext->data);
        stream_id_out.write(pkt->stream_id);
        seq_num_out.write(pkt->seq_num);
        valid_out.write(true);
    }

    SC_CTOR(PacketToPayload) {
        SC_METHOD(extract);
        sensitive << clk.pos();
    }
};

#endif // PACKET_TO_PAYLOAD_HH

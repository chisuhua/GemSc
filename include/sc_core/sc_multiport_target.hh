// include/sc_multiport_target.hh
#ifndef SC_MULTI_PORT_TARGET_HH
#define SC_MULTI_PORT_TARGET_HH

#include <systemc>
#include <tuple>
#include <utility>
#include "adapters/packet_to_payload.hh"
#include "adapters/payload_to_packet.hh"

namespace sc_core {

template<size_t... Is>
struct index_sequence {};

template<size_t N, size_t... Is>
struct make_index_sequence : make_index_sequence<N-1, N-1, Is...> {};

template<size_t... Is>
struct make_index_sequence<0, Is...> : index_sequence<Is...> {};

template<
    template<typename...> class CoreTemplate,
    typename InputTuple,
    typename OutputTuple
>
struct ScMultiPortTarget;

template<
    template<typename...> class CoreTemplate,
    typename... Inputs,
    typename... Outputs
>
struct ScMultiPortTarget<CoreTemplate, std::tuple<Inputs...>, std::tuple<Outputs...>>
    : sc_module 
{
    sc_in<bool> clk{"clk"};

    // 核心模块实例
    CoreTemplate<typename Inputs::payload_t..., typename Outputs::payload_t...> core{"core"};

    // 输入 valid/ready 信号数组 (来自 gemsc 网络的请求)
    std::array<sc_signal<bool>, sizeof...(Inputs)> valid_in;
    std::array<sc_signal<bool>, sizeof...(Inputs)> ready_out;

    // 输出 valid/ready 信号数组 (返回给 gemsc 网络的响应)
    std::array<sc_signal<bool>, sizeof...(Outputs)> valid_out;
    std::array<sc_signal<bool>, sizeof...(Outputs)> ready_in;

    // 数据信号 tuple
    std::tuple<sc_signal<typename Inputs::payload_t>...> data_in;
    std::tuple<sc_signal<uint64_t>...> stream_id_in;
    std::tuple<sc_signal<uint64_t>...> seq_num_in;

    std::tuple<sc_signal<typename Outputs::payload_t>...> data_out;
    std::tuple<sc_signal<uint64_t>...> stream_id_out;
    std::tuple<sc_signal<uint64_t>...> seq_num_out;

    // 适配器实例
    std::tuple<PacketToPayload<Inputs>...> to_payload_adapters;
    std::tuple<PayloadToPacket<Outputs>...> from_payload_adapters;

    SC_CTOR(ScMultiPortTarget) {
        connectComponents(make_index_sequence<sizeof...(Inputs)>{}, 
                          make_index_sequence<sizeof...(Outputs)>{});
    }

private:
    template<size_t... InIs, size_t... OutIs>
    void connectComponents(index_sequence<InIs...>, index_sequence<OutIs...>) {
        (connectInputPort(InIs), ...);
        (connectOutputPort(OutIs), ...);
        core.clk(clk);
    }

    // 连接单个输入端口 (请求 -> RTL)
    template<size_t I>
    void connectInputPort() {
        auto& to_adapter = std::get<I>(to_payload_adapters);
        auto& data_sig = std::get<I>(data_in);
        auto& sid_sig = std::get<I>(stream_id_in);
        auto& sqn_sig = std::get<I>(seq_num_in);

        to_adapter.name((std::string("to_req_adapter_") + std::to_string(I)).c_str());
        to_adapter.clk(clk);
        to_adapter.payload_out(data_sig);
        to_adapter.stream_id_out(sid_sig);
        to_adapter.seq_num_out(sqn_sig);
        to_adapter.valid_out(valid_in[I]);
        to_adapter.ready_out(ready_out[I]);

        // 将核心的输入连接到适配器的输出
        core.template get_input_valid<I>()(valid_in[I]);
        core.template get_input_data<I>()(data_sig);
        core.template get_input_sid<I>()(sid_sig);
        core.template get_input_sqn<I>()(sqn_sig);
        core.template get_input_ready<I>()(ready_out[I]);
    }

    // 连接单个输出端口 (RTL -> 响应)
    template<size_t I>
    void connectOutputPort() {
        auto& from_adapter = std::get<I>(from_payload_adapters);
        auto& data_sig = std::get<I>(data_out);
        auto& sid_sig = std::get<I>(stream_id_out);
        auto& sqn_sig = std::get<I>(seq_num_out);

        from_adapter.name((std::string("from_resp_adapter_") + std::to_string(I)).c_str());
        from_adapter.clk(clk);

        // 将核心的输出连接到适配器的输入
        core.template get_output_valid<I>()(valid_out[I]);
        core.template get_output_data<I>()(data_sig);
        core.template get_output_sid<I>()(sid_sig);
        core.template get_output_sqn<I>()(sqn_sig);
        core.template get_output_ready<I>()(ready_in[I]);

        // 连接适配器
        from_adapter.payload_in(data_sig);
        from_adapter.stream_id_in(sid_sig);
        from_adapter.seq_num_in(sqn_sig);
        from_adapter.valid_in(valid_out[I]);
        from_adapter.ready_out(ready_in[I]);
    }

public:
    // 提供给 ScTargetAdapter 的方法，用于缓存飞行中的请求
    template<size_t I>
    void cacheRequest(Packet* req_pkt) {
        std::get<I>(from_payload_adapters).cache_request(req_pkt);
    }
};

} // namespace sc_core

#endif // SC_MULTI_PORT_TARGET_HH

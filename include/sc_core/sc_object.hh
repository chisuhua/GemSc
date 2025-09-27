// utils/sc_object.hh
#ifndef GEMSC_UTILS_SC_OBJECT_HH
#define GEMSC_UTILS_SC_OBJECT_HH

#include <systemc>
#include <tuple>
#include <utility>
#include <type_traits>

namespace sc_core {

// 辅助：创建命名信号数组（用于 vector）
template<typename T>
struct NamedSignalCreator {
    sc_signal<T> operator()(const char* name) {
        return sc_signal<T>{name};
    }
};

// 辅助：创建命名信号 tuple
template<typename... Ts, size_t... Is>
auto make_named_tuple(const char* prefix, std::index_sequence<Is...>) {
    return std::make_tuple(
        sc_signal<Ts>{(std::string(prefix) + "_" + std::to_string(Is)).c_str()}...
    );
}

// 通用模块封装器
template<
    template<typename...> class CoreTemplate,
    typename InputTuple,
    typename OutputTuple
>
struct ScObject;

// 特化：展开输入输出 tuple
template<
    template<typename...> class CoreTemplate,
    typename... Inputs,
    typename... Outputs
>
struct ScObject<CoreTemplate, std::tuple<Inputs...>, std::tuple<Outputs...>>
    : sc_module 
{
    // 时钟
    sc_in<bool> clk{"clk"};

    // 输入 valid/ready（每个输入端口一组）
    sc_vector<sc_signal<bool>> valid_in{"valid_in", sizeof...(Inputs)};
    sc_vector<sc_signal<bool>> ready_out{"ready_out", sizeof...(Inputs)};
    sc_vector<sc_signal<bool>> core_ready{"core_ready", sizeof...(Inputs)};

    // 输出 valid/ready（每个输出端口一组）
    sc_vector<sc_signal<bool>> valid_out{"valid_out", sizeof...(Outputs)};
    sc_vector<sc_signal<bool>> ready_in{"ready_in", sizeof...(Outputs)};
    sc_vector<sc_signal<bool>> core_valid{"core_valid", sizeof...(Outputs)};

    // 数据信号：外部连接
    std::tuple<sc_signal<Inputs>...> data_in;
    std::tuple<sc_signal<Outputs>...> data_out;

    // 数据信号：连接到核心模块
    std::tuple<sc_signal<Inputs>...> data_in_core;
    std::tuple<sc_signal<Outputs>...> data_out_core;

    // 核心模块实例
    CoreTemplate<Inputs..., Outputs...> core{"core"};

    SC_CTOR(ScObject) 
        : data_in(make_named_tuple<Inputs...>("data_in", std::index_sequence_for<Inputs...>{}))
        , data_out(make_named_tuple<Outputs...>("data_out", std::index_sequence_for<Outputs...>{}))
        , data_in_core(make_named_tuple<Inputs...>("data_in_core", std::index_sequence_for<Inputs...>{}))
        , data_out_core(make_named_tuple<Outputs...>("data_out_core", std::index_sequence_for<Outputs...>{}))
    {
        // 绑定核心模块的端口（通过模板参数顺序）
        bind_core();

        // 流控逻辑
        SC_METHOD(arbitrate);
        sensitive << clk.pos();
    }

private:
    void bind_core() {
        // 使用 index_sequence 展开绑定
        bind_inputs(std::index_sequence_for<Inputs...>{});
        bind_outputs(std::index_sequence_for<Outputs...>{});
    }

    // 递归绑定输入
    template<size_t... Is>
    void bind_inputs(std::index_sequence<Is...>) {
        (bind_input_port(Is), ...); // C++17 fold expression
    }

    void bind_input_port(size_t i) {
        // 绑定 valid/ready
        core_ready[i](core_ready[i]);
        core_valid[i](core_valid[i]);

        // 绑定数据（通过 tuple get）
        bind_data_input(i, std::make_index_sequence<sizeof...(Inputs)>{});
    }

    template<size_t... Is>
    void bind_data_input(size_t i, std::index_sequence<Is...>) {
        // 匹配第 i 个输入类型
        if constexpr (i < sizeof...(Inputs)) {
            auto& in_sig = std::get<Is>(data_in);
            auto& in_core_sig = std::get<Is>(data_in_core);
            // 这里需要更复杂的类型匹配（简化版：按顺序绑定）
            // 实际中可用 type_index 或 tuple binding 工具
            in_core_sig(in_sig);
        }
    }

    // 绑定输出
    template<size_t... Is>
    void bind_outputs(std::index_sequence<Is...>) {
        (bind_output_port(Is), ...);
    }

    void bind_output_port(size_t i) {
        core_valid[i](core_valid[i]);
        ready_in[i](ready_in[i]);
        bind_data_output(i, std::make_index_sequence<sizeof...(Outputs)>{});
    }

    template<size_t... Is>
    void bind_data_output(size_t i, std::index_sequence<Is...>) {
        if constexpr (i < sizeof...(Outputs)) {
            auto& out_sig = std::get<Is>(data_out);
            auto& out_core_sig = std::get<Is>(data_out_core);
            out_sig(out_core_sig);
        }
    }

    // 流控仲裁逻辑（简化：轮询）
    void arbitrate() {
        // 将外部 valid/ready 映射到 core
        for (size_t i = 0; i < sizeof...(Inputs); ++i) {
            core_ready[i].write(ready_out[i].read());
        }
        for (size_t i = 0; i < sizeof...(Outputs); ++i) {
            core_valid[i].write(ready_in[i].read());
        }
    }
};

} // namespace sc_core
/*
// modules/accelerator_multi.hh
#ifndef GEMSC_MODULES_ACCELERATOR_MULTI_HH
#define GEMSC_MODULES_ACCELERATOR_MULTI_HH

#include <systemc>
#include "extensions/common.hh"

// 核心模板：支持多种输入和输出
template<typename... Inputs, typename... Outputs>
struct AcceleratorCore;

// 特化：支持 ReadCmd 输入，ReadResp 输出
template<>
struct AcceleratorCore<ReadCmd, WriteCmd, WriteData, ReadResp, WriteResp> 
    : sc_core::sc_module 
{
    // 输入端口（按顺序）
    sc_in<ReadCmd>    rcmd_in;
    sc_in<uint64_t>   r_sid;
    sc_in<uint64_t>   r_sqn;
    sc_in<bool>       r_valid;
    sc_out<bool>      r_ready;

    sc_in<WriteCmd>   wcmd_in;
    sc_in<uint64_t>   w_sid;
    sc_in<uint64_t>   w_sqn;
    sc_in<bool>       w_valid;
    sc_in<WriteData>  wdata_in;
    sc_in<bool>       wdata_valid;
    sc_out<bool>      w_ready;
    sc_out<bool>      wdata_ready;

    // 输出端口
    sc_out<ReadResp>  rresp_out;
    sc_out<uint64_t>  r_sid_out;
    sc_out<uint64_t>  r_sqn_out;
    sc_out<bool>      r_valid_out;
    sc_in<bool>       r_ready_in;

    sc_out<WriteResp> wresp_out;
    sc_out<uint64_t>  w_sid_out;
    sc_out<uint64_t>  w_sqn_out;
    sc_out<bool>      w_valid_out;
    sc_in<bool>       w_ready_in;

    // 内部状态机、内存等...
    void main() {
        // 实现多命令并发处理
        // ...
    }

    SC_CTOR(AcceleratorCore) {
        SC_METHOD(main);
        sensitive << clk.pos();
    }
};

// 使用 ScObject 封装
using AccelInputs = std::tuple<ReadCmd, WriteCmd, WriteData>;
using AccelOutputs = std::tuple<ReadResp, WriteResp>;

using AccelWrapper = sc_core::ScObject<
    AcceleratorCore,
    AccelInputs,
    AccelOutputs
>;

#endif // GEMSC_MODULES_ACCELERATOR_MULTI_HH
 
SC_MODULE(TestTopMemMulti) {
    sc_clock clk{"clk", 10, SC_NS};
    sc_signal<bool> rst_n;

    // 多命令适配器
    PacketToPayload<ReadCmdExt>   to_rtl_read{"to_rtl_read"};
    PacketToPayload<WriteCmdExt>  to_rtl_write{"to_rtl_write"};
    PacketToPayload<WriteDataExt> to_rtl_wdata{"to_rtl_wdata"};

    PayloadToPacket<ReadRespExt>  from_rtl_read{"from_rtl_read"};
    PayloadToPacket<WriteRespExt> from_rtl_write{"from_rtl_write"};

    AccelWrapper accel{"accel"};

    // 连接信号
    void connect() {
        // ReadCmd
        to_rtl_read.clk(clk);
        to_rtl_read.pkt_in(pkt_in);
        to_rtl_read.valid_in(valid_in);
        to_rtl_read.payload_out(accel.data_in.get<0>()); // ReadCmd
        to_rtl_read.stream_id_out(accel.data_in.get<1>()); // sid
        to_rtl_read.seq_num_out(accel.data_in.get<2>()); // sqn
        to_rtl_read.valid_out(accel.valid_in[0]);

        // 类似连接 WriteCmd, WriteData...

        // 响应
        from_rtl_read.payload_in(accel.data_out.get<0>());
        from_rtl_read.stream_id_in(accel.data_out.get<1>());
        from_rtl_read.seq_num_in(accel.data_out.get<2>());
        from_rtl_read.valid_in(accel.valid_out[0]);
        from_rtl_read.pkt_out(pkt_out);
        from_rtl_read.valid_out(valid_out);
    }
};
*/

#endif // GEMSC_UTILS_SC_OBJECT_HH

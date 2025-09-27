// include/modules/sc_tlm_router.hh
#ifndef SC_TLM_ROUTER_HH
#define SC_TLM_ROUTER_HH

#include <systemc>
#include "flit_sc.hh"

SC_MODULE(ScTlmRouter) {
public:
    // 输入端口 (从邻居接收)
    sc_in<bool> req_in[5]; // N, E, S, W, Local
    sc_in<Flit> flit_in[5];
    sc_out<bool> ack_in[5];

    // 输出端口 (向邻居发送)
    sc_out<bool> req_out[5];
    sc_out<Flit> flit_out[5];
    sc_in<bool> ack_out[5];

    // 时钟
    sc_in<bool> clk;

    // 构造函数
    SC_CTOR(ScTlmRouter);

private:
    // 内部缓冲区
    std::queue<Flit> input_buffer[5];
    std::queue<Flit> output_buffer[5];

    // 流程函数
    void routing_process();
    void arbitration_process();
    void switching_process();

    int compute_route(const Flit& flit);
};
#endif // SC_TLM_ROUTER_HH

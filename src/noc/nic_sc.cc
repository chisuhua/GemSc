// include/modules/terminal_node_sb.hh
#ifndef TERMINAL_NODE_SB_HH
#define TERMINAL_NODE_SB_HH

#include <systemc>
#include "flit_sc.hh"

SC_MODULE(TerminalNodeSb) {
public:
    sc_out<bool> req_out;
    sc_out<Flit> flit_out;
    sc_in<bool> ack_in;

    sc_in<bool> clk;

    SC_CTOR(TerminalNodeSb);

private:
    void generate_traffic();
    uint64_t packet_counter;
    std::mt19937 rng;
};

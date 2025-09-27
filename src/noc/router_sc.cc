// src/modules/router_sb.cc
#include "router_sb.hh"

ScTlmRouter::SC_CTOR(ScTlmRouter) {
    SC_METHOD(routing_process);
    sensitive << clk.pos();

    SC_METHOD(arbitration_process);
    sensitive << clk.pos();

    SC_METHOD(switching_process);
    sensitive << clk.pos();

    for (int i = 0; i < 5; ++i) {
        req_out[i].write(false); // 初始化请求信号
    }
}

void ScTlmRouter::routing_process() {
    for (int dir = 0; dir < 5; ++dir) {
        if (req_in[dir].read() && !ack_in[dir].read()) {
            Flit flit = flit_in[dir].read();
            int out_dir = compute_route(flit);
            output_buffer[out_dir].push(flit);
            ack_in[dir].write(true);
        } else {
            ack_in[dir].write(false);
        }
    }
}

void ScTlmRouter::arbitration_process() {
    // 简化：不实现复杂的仲裁
}

void ScTlmRouter::switching_process() {
    for (int out_dir = 0; out_dir < 5; ++out_dir) {
        if (!output_buffer[out_dir].empty() && !req_out[out_dir].read()) {
            Flit flit = output_buffer[out_dir].front();
            req_out[out_dir].write(true);
            flit_out[out_dir].write(flit);
            
            if (ack_out[out_dir].read()) {
                output_buffer[out_dir].pop();
                req_out[out_dir].write(false);
            }
        } else {
            req_out[out_dir].write(false);
        }
    }
}

int ScTlmRouter::compute_route(const Flit& flit) {
    uint32_t dest_addr = flit.header & 0xFFFF; // 假设低16位是地址
    uint32_t curr_x = name()[7] - '0'; // 从模块名 router_0_0 解析坐标
    uint32_t curr_y = name()[9] - '0';
    uint32_t dest_x = dest_addr % 4;
    uint32_t dest_y = dest_addr / 4;

    if (curr_x != dest_x) return dest_x > curr_x ? 1 : 3;
    if (curr_y != dest_y) return dest_y > curr_y ? 2 : 0;
    return 4; // Local
}


void RouterSb::switching_process() {
    for (int out_dir = 0; out_dir < 5; ++out_dir) {
        if (!output_buffer[out_dir].empty() && !req_out[out_dir].read()) {
            Flit flit = output_buffer[out_dir].front();
            
            // 请求发送
            req_out[out_dir].write(true);
            flit_out[out_dir].write(flit);
            
            // 等待下游确认
            if (ack_out[out_dir].read()) {
                output_buffer[out_dir].dequeue();
                req_out[out_dir].write(false);
            }
        } else {
            req_out[out_dir].write(false);
        }
    }
}

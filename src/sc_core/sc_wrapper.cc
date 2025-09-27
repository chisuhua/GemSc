#include "systemc_adapter.hh"

ScWrapper::ScWrapper(const std::string& n, EventQueue* eq)
    : SimObject(n, eq), sc_object(nullptr) {
    
    try {
        // Step 1: 创建 SystemC 模块
        sc_object = std::make_unique<ScObject>(("object_" + n).c_str());
        
        DPRINTF(ADAPTER, "[%s] ScWrapper initialized\n", name.c_str());
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to create SystemC module: " << e.what() << std::endl;
    }
}

// src/modules/systemc_adapter.cc
bool ScWrapper::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    if (!sc_object) { delete pkt; return false; }

    // 此时 ack_out 的状态已在上一周期由 tick() 更新
    if (sc_object->ready_out.read()) {
        sc_object->valid_in.write(true);
        sc_object->data_in.write(pkt);
        return true; // 成功注入，允许从 InputVC 移除
    } else {
        delete pkt;
        return false; // 反压：ScObject 忙
    }
}

void ScWrapper::tick() override {
    if (!sc_object) return;

    // Step 1: 推进 SystemC 内核一个完整周期
    sc_start(sc_time(1, SC_NS)); 

    // Step 2: 处理来自 ScObject 的输出请求
    if (sc_object->valid_out.read() && !sc_object->ready_in.read()) {
        Packet* pkt = sc_object->data_out.read();
        auto out_port = getPortManager().getDownstreamPorts()[0];
        if (out_port && out_port->sendReq(pkt)) {
            // sendReq 成功，将在下一 delta cycle 设置 ready_in=true
            sc_core::sc_event dummy;
            dummy.notify(sc_core::SC_ZERO_TIME);
        }
        // 如果 sendReq 失败，不设置 ready_in，形成反压
    }
}

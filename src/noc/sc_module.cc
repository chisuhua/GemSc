// src/modules/systemc_adapter.cc
//
// src/modules/systemc_adapter.cc
bool SystemcAdapter::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    // Step 1: 提取并验证 FlitExtension (与 RouterTb 完全相同)
    FlitExtension* flit_ext = get_flit_ext(pkt->payload);
    if (!flit_ext) {
        DPRINTF(NOC, "[%s] Missing FlitExtension! Dropping packet.\n", name.c_str());
        delete pkt;
        return false;
    }
    
    DPRINTF(NOC, "[%s] Received %s flit for packet %" PRIu64 " from %s\n", 
            name.c_str(), 
            flit_type_to_string(flit_ext->type).c_str(),
            flit_ext->packet_id, src_label.c_str());

    // Step 2: 将 Packet 和 FlitExtension 存入共享缓冲区
    // 这个缓冲区中的元素代表了等待被 SystemC 内核处理的 Flits
    {
        sc_core::sc_guard guard(buffer_mutex);
        InputFlit input_flit;
        input_flit.packet = std::unique_ptr<Packet>(pkt); // 接管所有权
        input_flit.flit_ext = *flit_ext; // 复制扩展信息
        input_flit.src_direction = src_id; // 记录来源方向
        
        to_sc_buffer.push(std::move(input_flit));
    }

    // Step 3: 通知 SystemC 内核有新数据
    data_ready_event.notify(sc_core::SC_ZERO_TIME);
    return true;
}

// src/modules/systemc_adapter.cc
void SystemcAdapter::tick() override {
    if (!sc_router) return;

    // Step 1: 推进 SystemC 内核一个时钟周期
    sc_start(sc_time(CLOCK_PERIOD, SC_NS));

    // Step 2: 处理来自 SystemC 模块的输出
    process_flits_from_sc();
}

void SystemcAdapter::process_flits_from_sc() {
    // 这个函数模拟了 ScTlmRouter 的输出行为
    // 实际中可能通过回调或信号监听来实现
    
    while (true) {
        Flit output_flit;
        int out_dir;
        
        // 尝试从 ScTlmRouter 获取一个已处理好的 Flit
        // 伪代码：if (sc_router->pop_output_flit(output_flit, out_dir))
        if (/* 从某个队列或信号获取到一个 Flit */) {
            
            // Step 1: 创建一个新的 Packet 来承载这个 Flit
            auto* trans = new tlm::tlm_generic_payload();
            // ... 设置 payload 数据 ...
            
            // ========== 关键：创建新的 FlitExtension ==========
            // 注意：这里我们**不是**直接复制原始的 extension
            // 而是根据当前 Flit 的性质和 ScTlmRouter 的行为来创建
            FlitExtension flit_ext;
            
            // 例如，如果这是 Head Flit 的响应，我们创建一个 HEAD_TAIL Flit
            if (output_flit.type == Flit::HEAD && is_response_to_request(output_flit)) {
                flit_ext.type = FlitType::HEAD_TAIL;
                // packet_id 应该与原始请求的 packet_id 相同
                flit_ext.packet_id = find_original_packet_id(output_flit.header);
                flit_ext.vc_id = output_flit.vc_id; // 或根据算法重新分配
                
                set_flit_ext(trans, flit_ext);
                
                Packet* resp_pkt = new Packet(trans, getCurrentCycle(), PKT_RESP);
                // resp_pkt->original_req = ... ; // 如果能找到原始请求
                //
                auto& pm = getPortManager();
                if (!pm.getDownstreamPorts().empty()) {
                    pm.getDownstreamPorts()[0]->sendReq(resp_pkt);
                    DPRINTF(NOC, "[%s] Sent response flit to direction %d\n", name.c_str(), out_dir);
                } else {
                    delete resp_pkt;
                }
            }
            // 处理其他情况...
        } else {
            break; // 没有更多输出
        }
    }
}

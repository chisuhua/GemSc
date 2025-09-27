// src/modules/terminal_node.cc
void TerminalNode::generateAndSendRequest() {
    // ... 创建原始的大 payload ...
    std::vector<uint8_t> data(32, 0xAB); // 32字节的有效载荷
    auto* trans = new tlm::tlm_generic_payload();
    trans->set_command(tlm::TLM_READ_COMMAND);
    trans->set_address(dest_addr);
    trans->set_data_length(data.size());
    // ... 设置其他字段 ...

    // 计算需要多少个 flits (假设每个 flit 最多承载 8 字节)
    size_t flit_count = (data.size() + 7) / 8; // 向上取整
    uint64_t current_offset = 0;

    for (size_t i = 0; i < flit_count; ++i) {
        // 为每个 flit 创建一个新的 transaction 和 Packet
        auto* flit_trans = new tlm::tlm_generic_payload();
        
        // 复制必要的元数据
        flit_trans->set_command(trans->get_command());
        flit_trans->set_address(trans->get_address());
        flit_trans->set_streaming_width(trans->get_streaming_width());

        // 设置当前 flit 的数据
        size_t this_flit_size = std::min<size_t>(8, data.size() - current_offset);
        uint8_t* flit_data = new uint8_t[this_flit_size];
        memcpy(flit_data, data.data() + current_offset, this_flit_size);
        flit_trans->set_data_ptr(flit_data);
        flit_trans->set_data_length(this_flit_size);

        FlitExtension flit_ext;
        if (flit_count == 1) {
            flit_ext.type = FlitType::HEAD_TAIL;
        } else if (i == 0) {
            flit_ext.type = FlitType::HEAD;
        } else if (i == flit_count - 1) {
            flit_ext.type = FlitType::TAIL;
        } else {
            flit_ext.type = FlitType::BODY;
        }
        flit_ext.packet_id = packet_counter;
        flit_ext.vc_id = 0; // 可以根据算法选择
        
        set_flit_ext(trans, flit_ext); // 将扩展附加到 payload
 
        // 创建 GemSC Packet 并发送
        Packet* flit_pkt = new Packet(flit_trans, getCurrentCycle(), PKT_REQ_READ);
        flit_pkt->seq_num = packet_counter;
        flit_pkt->vc_id = flit_ext.vc_id;
        flit_pkt->priority = 0; // 或从配置读取

        if (to_network_port->sendReq(flit_pkt)) {
            DPRINTF(NOC, "[%s] Injected flit %zu/%zu (type=%d)\n", 
                    name.c_str(), i+1, flit_count, flit_ext.type);
        } else {
            delete flit_pkt; // 网络拥塞
        }

        current_offset += 8;
    }

    packet_counter++;
    delete trans; // 删除原始的 transaction
}

// src/modules/terminal_node.cc
bool TerminalNode::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    FlitTypeExtension* flit_ext = get_flit_type(pkt->payload);
    if (!flit_ext) {
        delete pkt;
        return false;
    }

    DPRINTF(NOC, "[%s] Received response flit: type=%d, packet_id=%" PRIu64 "\n", 
            name.c_str(), flit_ext->type, flit_ext->packet_id);

    switch (flit_ext->type) {
        case FlitTypeExtension::HEAD:
        case FlitTypeExtension::BODY: {
            // 对于响应，通常只有 HEAD/TAIL 或 HEAD_TAIL
            // 这里简化处理，假设所有响应都是完整到达
            [[fallthrough]];
        }
        case FlitTypeExtension::TAIL:
        case FlitTypeExtension::HEAD_TAIL: {
            uint64_t latency = getCurrentCycle() - pkt->src_cycle;
            total_latency += latency;
            received_packets++;
            DPRINTF(NOC, "[%s] Complete response received (Latency: %lu)\n", 
                    name.c_str(), latency);
            break;
        }
    }
    delete pkt;
    return true;
}

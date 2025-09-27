// src/sc_target_adapter.cc
bool ScTargetAdapter::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    if (!target || !pkt) {
        delete pkt;
        return false;
    }

    if (src_id >= (int)std::tuple_size_v<Inputs>) {
        delete pkt;
        return false;
    }

    // Step 1: 缓存请求，供后续响应使用
    target->cacheRequest<src_id>(pkt);

    // Step2: 将 Packet 注入到目标模块
    auto& to_adapter = std::get<src_id>(target->to_payload_adapters);
    to_adapter.pkt_in.write(pkt);
    to_adapter.valid_in.write(true);

    return true;
}

void ScTargetAdapter::tick() override {
    if (!target) return;
    sc_start(sc_time(1, SC_NS));

    // 处理所有输出
    for (size_t i = 0; i < std::tuple_size_v<Outputs>; ++i) {
        auto& from_adapter = std::get<i>(target->from_payload_adapters);
        if (from_adapter.valid_out.read()) {
            Packet* resp_pkt = from_adapter.pkt_out.read();
            auto out_port = getPortManager().getDownstreamPorts()[i];
            if (out_port && out_port->sendReq(resp_pkt)) {
                // 成功发送响应
            }
        }
    }
}

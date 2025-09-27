// src/modules/router.cc
bool Router::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
    // 获取 Flit 类型扩展
    FlitTypeExtension* flit_ext = get_flit_type(pkt->payload);
    if (!flit_ext) {
        DPRINTF(NOC, "[%s] Missing FlitTypeExtension!\n", name.c_str());
        delete pkt;
        return false;
    }

    DPRINTF(NOC, "[%s] Received flit: type=%d, packet_id=%" PRIu64 "\n", 
            name.c_str(), flit_ext->type, flit_ext->packet_id);

    switch (flit_ext->type) {
        case FlitTypeExtension::HEAD: {
            // Step 1: 路由计算
            int out_dir = routing_algo->route(pkt, router_id);
            if (out_dir < 0 || out_dir >= NUM_DIRECTIONS) {
                DPRINTF(NOC, "[%s] Routing failed\n", name.c_str());
                delete pkt;
                return false;
            }
            
            // Step 2: VC 分配 (VCA) - 为这个 packet 分配一个输出 VC
            int out_vc = allocateVC(out_dir, flit_ext->vc_id);
            if (out_vc < 0) {
                DPRINTF(NOC, "[%s] VC allocation failed\n", name.c_str());
                delete pkt;
                return false;
            }
            
            // 更新扩展中的 VC ID
            flit_ext->vc_id = out_vc;
            
            // Step 3: 将 Head flit 放入输出 VC 的缓冲区
            output_ports[out_dir].output_vcs[out_vc].enqueue(pkt);
            break;
        }
        
        case FlitTypeExtension::BODY:
        case FlitTypeExtension::TAIL: {
            // 对于 Body 和 Tail，查找之前为该 packet_id 分配的路由和 VC
            auto route_info = findRouteForPacket(flit_ext->packet_id);
            if (!route_info.valid) {
                DPRINTF(NOC, "[%s] No route found for body/tail flit\n", name.c_str());
                delete pkt;
                return false;
            }
            
            // 直接放入对应的输出 VC 缓冲区
            output_ports[route_info.out_dir].output_vcs[route_info.out_vc].enqueue(pkt);
            break;
        }
        
        case FlitTypeExtension::HEAD_TAIL: {
            // 单 flit 包，直接路由并尝试发送
            int out_dir = routing_algo->route(pkt, router_id);
            if (out_dir < 0 || out_dir >= NUM_DIRECTIONS) {
                delete pkt;
                return false;
            }
            
            if (output_ports[out_dir].canSend()) {
                output_ports[out_dir].sendReq(pkt);
            } else {
                // 需要缓冲
                int out_vc = allocateVC(out_dir, flit_ext->vc_id);
                if (out_vc >= 0) {
                    flit_ext->vc_id = out_vc;
                    output_ports[out_dir].output_vcs[out_vc].enqueue(pkt);
                } else {
                    delete pkt;
                }
            }
            break;
        }
    }
    return true;
}

void Router::tick() override {
    tryForwardFlits();
}

void Router::tryForwardFlits() {
    for (auto& port : output_ports) {
        for (auto& vc : port.output_vcs) {
            if (!vc.empty()) {
                Packet* pkt = vc.front();
                FlitTypeExtension* flit_ext = get_flit_type(pkt->payload);
                
                // 检查下游是否可以接收
                if (port.canSend()) {
                    if (port.sendReq(pkt)) {
                        vc.pop(); // 成功发送，从缓冲区移除
                        DPRINTF(NOC, "[%s] Forwarded flit (type=%d)\n", 
                                name.c_str(), flit_ext->type);
                        
                        // 如果是 TAIL flit，可以释放 VC 资源
                        if (flit_ext->type == FlitTypeExtension::TAIL ||
                            flit_ext->type == FlitTypeExtension::HEAD_TAIL) {
                            releaseVC(port.id, flit_ext->vc_id);
                        }
                    }
                }
                // 如果不能发送，flit 保留在 VC 缓冲区，等待下一周期
            }
        }
    }
}

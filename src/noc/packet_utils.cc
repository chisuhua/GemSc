// src/utils/packet_utils.cc (新建此文件)

#include "utils/packet_utils.hh"
#include "include/flit.hh"
#include "include/packet.hh"

// 将一个 Packet 分割成一系列 Flits
std::vector<Flit> packetToFlits(const Packet& pkt) {
    std::vector<Flit> flits;
    
    // Step 1: 创建 Head Flit
    Flit head_flit;
    head_flit.packet_id = pkt.seq_num; // 使用 seq_num 作为 packet_id
    head_flit.dest_addr = pkt.payload->get_address();
    head_flit.src_addr = pkt.payload->get_address(); // 需要从 payload 或其他地方获取源地址
    head_flit.vc_id = pkt.vc_id;
    head_flit.priority = pkt.priority;
    head_flit.type = Flit::HEAD;
    
    // 复制有效载荷数据
    size_t remaining_data = pkt.payload->get_data_length();
    size_t offset = 0;
    
    if (remaining_data > 0) {
        // 复制第一个 flit 的数据
        head_flit.data_length = std::min(remaining_data, Flit::DATA_SIZE);
        memcpy(head_flit.data.data(), 
               static_cast<const uint8_t*>(pkt.payload->get_data_ptr()), 
               head_flit.data_length);
        offset += head_flit.data_length;
        remaining_data -= head_flit.data_length;
        
        // 如果整个包能放入一个 flit，则它是 HEAD_TAIL 类型
        if (remaining_data == 0) {
            head_flit.type = Flit::HEAD_TAIL;
            flits.push_back(head_flit);
            return flits;
        }
    }
    
    // 否则，这是一个多 flit 的包，head_flit 只是头部
    flits.push_back(head_flit);
    
    // Step 2: 创建 Body Flits
    while (remaining_data > Flit::DATA_SIZE) {
        Flit body_flit(head_flit, Flit::BODY);
        body_flit.data_length = Flit::DATA_SIZE;
        memcpy(body_flit.data.data(), 
               static_cast<const uint8_t*>(pkt.payload->get_data_ptr()) + offset, 
               Flit::DATA_SIZE);
        flits.push_back(body_flit);
        
        offset += Flit::DATA_SIZE;
        remaining_data -= Flit::DATA_SIZE;
    }
    
    // Step 3: 创建 Tail Flit
    if (remaining_data > 0) {
        Flit tail_flit(head_flit, Flit::TAIL);
        tail_flit.data_length = remaining_data;
        memcpy(tail_flit.data.data(), 
               static_cast<const uint8_t*>(pkt.payload->get_data_ptr()) + offset, 
               remaining_data);
        flits.push_back(tail_flit);
    }
    
    return flits;
}

// Flit 构造函数实现
Flit::Flit(const Packet& pkt) {
    packet_id = pkt.seq_num;
    dest_addr = pkt.payload->get_address();
    // ... 其他元数据 ...
    
    data_length = std::min(pkt.payload->get_data_length(), DATA_SIZE);
    if (data_length > 0) {
        memcpy(data.data(), pkt.payload->get_data_ptr(), data_length);
    }
    
    // 判断类型
    if (pkt.payload->get_data_length() <= DATA_SIZE) {
        type = HEAD_TAIL;
    } else {
        type = HEAD;
    }
}

Flit::Flit(const Flit& head_flit, const Type new_type) 
    : packet_id(head_flit.packet_id),
      dest_addr(head_flit.dest_addr),
      src_addr(head_flit.src_addr),
      vc_id(head_flit.vc_id),
      priority(head_flit.priority),
      type(new_type),
      data_length(0) {
    // 保留其他元数据，data 和 data_length 在后续填充
}

// src/utils/packet_utils.cc (续)

// 将一个 Flit (通常是 Tail Flit) 重新组装成一个 Packet
// 注意：这通常只用于生成响应包。对于请求包，我们使用的是反向过程。
Packet* flitToPacket(const Flit& flit, EventQueue* eq) {
    // 为响应创建一个新的 tlm_generic_payload
    auto* trans = new tlm::tlm_generic_payload();
    trans->set_command(tlm::TLM_RESPONSE_COMMAND);
    trans->set_address(flit.src_addr); // 响应发回源地址
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    trans->set_streaming_width(flit.data_length);
    
    // 设置数据
    if (flit.data_length > 0) {
        uint8_t* data_ptr = new uint8_t[flit.data_length];
        memcpy(data_ptr, flit.data.data(), flit.data_length);
        trans->set_data_ptr(data_ptr);
        trans->set_data_length(flit.data_length);
    }

    // 创建 Packet
    Packet* pkt = new Packet(trans, eq->getCurrentCycle(), PKT_RESP);
    pkt->seq_num = flit.packet_id;
    pkt->vc_id = flit.vc_id;
    pkt->priority = flit.priority;

    // 这里需要一种机制来关联原始请求，例如通过 packet_id 查找
    // pkt->original_req = findOriginalRequest(flit.packet_id);
    // 如果找不到 original_req，end-to-end 延迟统计会不准确

    return pkt;
}

// 辅助函数：根据 packet_id 查找原始请求 (需要全局管理)
// Packet* findOriginalRequest(uint64_t packet_id) {
//     // 需要在某个全局表中查找
//     // ...
// }

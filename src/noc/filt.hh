// include/flit.hh
#ifndef FLIT_HH
#define FLIT_HH

#include <cstdint>
#include <array>

// 微片 (Flit) 是 NoC 中传输的最小单位
struct Flit {
    // =============== 元数据 (Metadata) ===============
    uint64_t packet_id;      // 所属数据包的ID
    uint32_t dest_addr;      // 目标地址
    uint32_t src_addr;       // 源地址
    int vc_id;               // 虚拟通道ID
    uint8_t priority;        // 优先级
    
    // Flit类型标记
    enum Type { HEAD, BODY, TAIL, HEAD_TAIL } type;
    
    // =============== 有效载荷 (Payload) ===============
    static constexpr size_t DATA_SIZE = 8; // 假设每个 flit 8 字节
    std::array<uint8_t, DATA_SIZE> data;
    size_t data_length; // 实际使用的数据长度 (0-8)
    
    // =============== 构造函数 ===============
    Flit() : packet_id(0), dest_addr(0), src_addr(0), vc_id(0), 
             priority(0), type(HEAD), data_length(0) {}

    // 从 Packet 构造一个 Head/Tail Flit (用于小数据包)
    explicit Flit(const Packet& pkt);

    // 为同一个包创建新的 Body 或 Tail Flit
    Flit(const Flit& head_flit, const Type new_type);
};

#endif // FLIT_HH

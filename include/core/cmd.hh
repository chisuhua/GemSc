// cmd.hh
#ifndef GEMSC_EXTENSIONS_COMMON_HH
#define GEMSC_EXTENSIONS_COMMON_HH

#include <cstdint>
#include <ostream>  // 👈 新增

struct ReadCmd {
    uint64_t addr;
    uint32_t size;
    uint8_t  flags;

    ReadCmd() : addr(0), size(0), flags(0) {}
    ReadCmd(uint64_t a, uint32_t s, uint8_t f = 0) : addr(a), size(s), flags(f) {}
};

// 👇 为 ReadCmd 添加打印支持
inline std::ostream& operator<<(std::ostream& os, const ReadCmd& cmd) {
    os << "ReadCmd{addr=0x" << std::hex << cmd.addr
       << ", size=" << std::dec << cmd.size
       << ", flags=0x" << std::hex << (int)cmd.flags << "}";
    return os;
}

// ----------------------------------------

struct WriteCmd {
    uint64_t addr;
    uint32_t size;
    uint8_t  flags;

    WriteCmd() : addr(0), size(0), flags(0) {}
    WriteCmd(uint64_t a, uint32_t s, uint8_t f = 0) : addr(a), size(s), flags(f) {}
};

inline std::ostream& operator<<(std::ostream& os, const WriteCmd& cmd) {
    os << "WriteCmd{addr=0x" << std::hex << cmd.addr
       << ", size=" << std::dec << cmd.size
       << ", flags=0x" << std::hex << (int)cmd.flags << "}";
    return os;
}

// ----------------------------------------

struct WriteData {
    uint8_t data[64];
    uint8_t strb[8];
    uint8_t valid_bytes;

    WriteData() : valid_bytes(0) {
        for (int i = 0; i < 64; ++i) data[i] = 0;
        for (int i = 0; i < 8;  ++i) strb[i] = 0;
    }
};

inline std::ostream& operator<<(std::ostream& os, const WriteData& wd) {
    os << "WriteData{valid_bytes=" << (int)wd.valid_bytes << ", data=[";
    for (int i = 0; i < wd.valid_bytes && i < 64; ++i) {
        if (i > 0) os << " ";
        os << "0x" << std::hex << (int)wd.data[i];
    }
    os << "], strb=[";
    for (int i = 0; i < 8; ++i) {
        if (i > 0) os << " ";
        os << (int)wd.strb[i];
    }
    os << "]}";
    return os;
}

// ----------------------------------------

struct ReadResp {
    uint8_t data[64];
    uint8_t valid_bytes;
    bool    error;

    ReadResp() : valid_bytes(0), error(false) {
        for (int i = 0; i < 64; ++i) data[i] = 0;
    }
};

inline std::ostream& operator<<(std::ostream& os, const ReadResp& rr) {
    os << "ReadResp{valid_bytes=" << (int)rr.valid_bytes
       << ", error=" << (rr.error ? "true" : "false")
       << ", data=[";
    for (int i = 0; i < rr.valid_bytes && i < 64; ++i) {
        if (i > 0) os << " ";
        os << "0x" << std::hex << (int)rr.data[i];
    }
    os << "]}";
    return os;
}

// ----------------------------------------

struct WriteResp {
    bool success;
    uint8_t status_code;

    WriteResp() : success(true), status_code(0) {}
};

inline std::ostream& operator<<(std::ostream& os, const WriteResp& wr) {
    os << "WriteResp{success=" << (wr.success ? "true" : "false")
       << ", status_code=" << (int)wr.status_code << "}";
    return os;
}

// ----------------------------------------

struct StreamUniqID {
    uint64_t stream_id;
    uint64_t seq_num;

    StreamUniqID() : stream_id(0), seq_num(0) {}
    StreamUniqID(uint64_t sid, uint64_t sqn) : stream_id(sid), seq_num(sqn) {}
};

inline std::ostream& operator<<(std::ostream& os, const StreamUniqID& s) {
    os << "StreamUniqID{stream_id=0x" << std::hex << s.stream_id
       << ", seq_num=" << std::dec << s.seq_num << "}";
    return os;
}

// 👇 为简单类型 int32_t 也提供打印（虽然默认有，但统一风格）
inline std::ostream& operator<<(std::ostream& os, int32_t val) {
    os << val;
    return os;
}

#endif // GEMSC_EXTENSIONS_COMMON_HH

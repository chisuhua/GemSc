// include/port_stats.hh
#ifndef PORT_STATS_HH
#define PORT_STATS_HH

#include <cstdint>
#include <string>

struct PortStats {
    uint64_t req_count = 0;      // 请求包数量
    uint64_t resp_count = 0;     // 响应包数量
    uint64_t byte_count = 0;     // 总字节数
    uint64_t total_delay = 0;    // 累计延迟（cycles）
    uint64_t min_delay = UINT64_MAX;
    uint64_t max_delay = 0;
    // 信用流控专用
    uint64_t credit_sent = 0;     // 发出的信用包数
    uint64_t credit_received = 0; // 接收的信用包数
    uint64_t credit_value = 0;    // 实际传递的信用额度


    void reset() {
        req_count = resp_count = byte_count = 0;
        total_delay = 0;
        min_delay = UINT64_MAX;
        max_delay = 0;
        credit_sent = credit_received = credit_value = 0;
    }

    void merge(const PortStats& other) {
        req_count += other.req_count;
        resp_count += other.resp_count;
        byte_count += other.byte_count;
        total_delay += other.total_delay;
        credit_sent += other.credit_sent;
        credit_received += other.credit_received;
        credit_value += other.credit_value;
        if (other.min_delay < min_delay) min_delay = other.min_delay;
        if (other.max_delay > max_delay) max_delay = other.max_delay;
    }

    std::string toString() const;
};

inline std::string PortStats::toString() const {
    char buf[256];
    double avg_delay = (req_count ? (double)total_delay / req_count : 0.0);
    double avg_credit = (credit_received ? (double)credit_value / credit_received : 0.0);

    snprintf(buf, sizeof(buf),
             "req=%lu resp=%lu bytes=%s delay(avg=%.1f min=%lu max=%lu) "
             "credits(sent=%lu recv=%lu value=%lu avg=%.1f)",
             req_count,
             resp_count,
             byte_count >= (1<<20) ? (std::to_string(byte_count >> 20) + "MB").c_str() :
             byte_count >= (1<<10) ? (std::to_string(byte_count >> 10) + "KB").c_str() :
                                   (std::to_string(byte_count) + "B").c_str(),
             avg_delay,
             min_delay == UINT64_MAX ? 0 : min_delay,
             max_delay,
             credit_sent, credit_received, credit_value, avg_credit);
    return std::string(buf);
}

#endif // PORT_STATS_HH

// include/modules/systemc_adapter.hh
#ifndef SYSTEMC_ADAPTER_HH
#define SYSTEMC_ADAPTER_HH

#include "../sim_object.hh"
#include "systemc_module_interface.hh"
#include <memory>
#include <mutex>

template<typename T>
class SystemcAdapter : public SimObject, public SystemcModuleInterface {
private:
    std::unique_ptr<T> sc_module; // 具体的 SystemC 模块实例
    mutable std::mutex data_mutex; // 保护共享数据

    // 输入缓冲区：存储从 gemsc 收到的、等待处理的 Flits
    std::queue<std::pair<Packet*, std::string>> input_flit_queue;

    // 输出缓冲区：存储 SystemC 模块生成的、等待发送的 Flits
    std::array<std::unique_ptr<Packet>, 5> output_flit_buffer; // 假设有5个方向

public:
    explicit SystemcAdapter(const std::string& n, EventQueue* eq);
    ~SystemcAdapter() override = default;

    // SimObject 接口
    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override;
    void tick() override;

    // SystemcModuleInterface 接口 (供 SystemcAdapter 自身调用)
    void processInputFlit(const Packet& pkt, const std::string& src_label) override;
    bool hasOutputFlit(int port_id) const override;
    Packet* getOutputFlit(int port_id) override;
    void tick() override;
};

// 显式实例化，避免链接错误
extern template class SystemcAdapter<class ScTlmRouter>;
extern template class SystemcAdapter<class ScTerminalNode>;

#endif // SYSTEMC_ADAPTER_HH

// include/modules/systemc_module_interface.hh
#ifndef SYSTEMC_MODULE_INTERFACE_HH
#define SYSTEMC_MODULE_INTERFACE_HH

#include "packet.hh"
#include "flit_extension.hh" // 假设已定义

class SystemcModuleInterface {
public:
    virtual ~SystemcModuleInterface() = default;
    
    // 处理从 gemsc 网络传入的一个 Flit (Packet)
    virtual void processInputFlit(const Packet& pkt, const std::string& src_label) = 0;
    
    // 获取当前周期是否有待发送的输出 Flit
    virtual bool hasOutputFlit(int port_id) const = 0;
    
    // 获取指定端口的输出 Flit
    virtual Packet* getOutputFlit(int port_id) = 0;
    
    // 推进内部状态一个周期
    virtual void tick() = 0;
};

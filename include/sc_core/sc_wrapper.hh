#include <systemc>


template<
class ScWrapper : public SimObject {
private:
    std::unique_ptr<ScObject> sc_object;
public:
    explicit ScWrapper(const std::string& n, EventQueue* eq);
    ~ScWrapper() override;

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override;
    void tick() override;
};



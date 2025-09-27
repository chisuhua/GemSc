// include/extensions/flit_extension.hh
#ifndef FLIT_EXTENSION_HH
#define FLIT_EXTENSION_HH

#include "tlm.h"

enum class FlitType {
    HEAD, BODY, TAIL, HEAD_TAIL
};

struct FlitExtension : public tlm::tlm_extension<FlitExtension> {
    FlitType type;
    uint64_t packet_id;
    int vc_id;

    explicit FlitExtension(FlitType t = FlitType::HEAD) 
        : type(t), packet_id(0), vc_id(0) {}

    tlm_extension* clone() const override {
        return new FlitExtension(*this);
    }

    void copy_from(tlm_extension const& e) override {
        auto& ext = static_cast<const FlitExtension&>(e);
        type = ext.type;
        packet_id = ext.packet_id;
        vc_id = ext.vc_id;
    }
};

inline FlitExtension* get_flit_ext(tlm::tlm_generic_payload* p) {
    FlitExtension* ext = nullptr;
    p->get_extension(ext);
    return ext;
}

inline void set_flit_ext(tlm::tlm_generic_payload* p, const FlitExtension& src) {
    FlitExtension* ext = new FlitExtension(src);
    p->set_extension(ext);
}

#endif // FLIT_EXTENSION_HH

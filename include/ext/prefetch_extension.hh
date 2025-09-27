// include/extensions/prefetch_extension.hh
#ifndef PREFETCH_EXTENSION_HH
#define PREFETCH_EXTENSION_HH

#include "tlm.h"

struct PrefetchExtension : public tlm::tlm_extension<PrefetchExtension> {
    bool is_prefetch = false;
    uint64_t source_pc = 0;
    uint64_t distance = 0;
    uint64_t level = 0;  // L1/L2/L3

    tlm_extension* clone() const override {
        return new PrefetchExtension(*this);
    }

    void copy_from(tlm_extension const &e) override {
        auto& ext = static_cast<const PrefetchExtension&>(e);
        is_prefetch = ext.is_prefetch;
        source_pc = ext.source_pc;
        distance = ext.distance;
        level = ext.level;
    }
};

inline PrefetchExtension* get_prefetch(tlm_generic_payload* p) {
    PrefetchExtension* ext = nullptr;
    p->get_extension(ext);
    return ext;
}

#endif // PREFETCH_EXTENSION_HH

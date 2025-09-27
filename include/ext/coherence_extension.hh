// include/extensions/coherence_extension.hh
#ifndef COHERENCE_EXTENSION_HH
#define COHERENCE_EXTENSION_HH

#include "tlm.h"

enum CacheState { INVALID, SHARED, EXCLUSIVE, MODIFIED };

struct CoherenceExtension : public tlm::tlm_extension<CoherenceExtension> {
    CacheState prev_state = INVALID;
    CacheState next_state = INVALID;
    bool is_exclusive = false;
    uint64_t sharers_mask = 0;
    bool needs_snoop = true;

    tlm_extension* clone() const override {
        return new CoherenceExtension(*this);
    }

    void copy_from(tlm_extension const &e) override {
        auto& ext = static_cast<const CoherenceExtension&>(e);
        prev_state = ext.prev_state;
        next_state = ext.next_state;
        is_exclusive = ext.is_exclusive;
        sharers_mask = ext.sharers_mask;
        needs_snoop = ext.needs_snoop;
    }
};

inline CoherenceExtension* get_coherence(tlm_generic_payload* p) {
    CoherenceExtension* ext = nullptr;
    p->get_extension(ext);
    return ext;
}

inline void set_coherence(tlm_generic_payload* p, const CoherenceExtension& src) {
    CoherenceExtension* ext = new CoherenceExtension(src);
    p->set_extension(ext);
}

#endif // COHERENCE_EXTENSION_HH

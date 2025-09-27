// include/extensions/performance_extension.hh
#ifndef PERFORMANCE_EXTENSION_HH
#define PERFORMANCE_EXTENSION_HH

#include "tlm.h"

struct PerformanceExtension : public tlm::tlm_extension<PerformanceExtension> {
    uint64_t issue_cycle = 0;
    uint64_t grant_cycle = 0;
    uint64_t complete_cycle = 0;
    bool is_critical_word_first = false;

    tlm_extension* clone() const override {
        return new PerformanceExtension(*this);
    }

    void copy_from(tlm_extension const &e) override {
        auto& ext = static_cast<const PerformanceExtension&>(e);
        issue_cycle = ext.issue_cycle;
        grant_cycle = ext.grant_cycle;
        complete_cycle = ext.complete_cycle;
        is_critical_word_first = ext.is_critical_word_first;
    }
};

inline PerformanceExtension* get_performance(tlm_generic_payload* p) {
    PerformanceExtension* ext = nullptr;
    p->get_extension(ext);
    return ext;
}

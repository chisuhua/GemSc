#ifndef SIM_CORE_HH
#define SIM_CORE_HH

#include <cstdint>
#include <cinttypes> 

//
#ifdef DEBUG_PRINT
#define DPRINTF(name, fmt, ...) \
    do { \
        printf("[%s] ", #name); \
        printf(fmt, ##__VA_ARGS__); \
    } while(0)
#else
#define DPRINTF(name, fmt, ...) do {} while(0)
#endif

#endif // SIM_CORE_HH

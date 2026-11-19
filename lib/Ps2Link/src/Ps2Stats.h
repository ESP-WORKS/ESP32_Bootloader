#pragma once

#include <stdint.h>

struct Ps2Stats {
    uint32_t rxBytes = 0;
    uint32_t txBytes = 0;
    uint32_t txFail = 0;
    uint32_t parityErr = 0;
    uint32_t frameErr = 0;
    uint32_t timeoutErr = 0;
    uint32_t overflowErr = 0;
};

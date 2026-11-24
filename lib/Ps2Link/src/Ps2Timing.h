#pragma once

#include <stdint.h>

// Timing of the bit-bang PS/2 link.
// 40 us half-period => ~12.5 kHz, inside the 10–16.7 kHz PS/2 spec.
// Both boards share these constants so host and device stay in lockstep.
namespace ps2 {

constexpr uint32_t kHalfUs = 40;
constexpr uint32_t kSetupUs = 10;
constexpr uint32_t kInhibitUs = 150;
// FabGL segura CLK longo; idle precisa de janela maior antes de reenviar.
constexpr uint32_t kIdleStableUs = 200;
constexpr uint32_t kByteTimeoutUs = 20000;
constexpr uint32_t kIdleTimeoutUs = 100000;
constexpr uint32_t kInterByteUs = 500;
constexpr uint32_t kStuckFrameUs = 2500;

inline uint8_t oddParity(uint8_t data) {
    uint8_t p = 1;
    for (int i = 0; i < 8; i++) {
        p ^= (data >> i) & 1;
    }
    return p;
}

}  // namespace ps2

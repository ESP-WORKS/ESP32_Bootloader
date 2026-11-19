#pragma once

#include <Arduino.h>
#include "Ps2Timing.h"

namespace ps2 {

// Open-drain emulation: never drive HIGH, only pull LOW or release to pull-up.
// Required for PS/2 and for the BSS138 bidirectional shifter.
inline void release(int pin) { pinMode(pin, INPUT_PULLUP); }

inline void pullLow(int pin) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
}

inline void releaseBus(int clkPin, int dataPin) {
    release(clkPin);
    release(dataPin);
}

inline bool waitLevel(int pin, int level, uint32_t timeoutUs) {
    const uint32_t start = micros();
    while (digitalRead(pin) != level) {
        if ((micros() - start) > timeoutUs) {
            return false;
        }
    }
    return true;
}

// Both lines HIGH and stable. Used before starting a transmission.
inline bool waitIdle(int clkPin, int dataPin, uint32_t timeoutUs,
                     uint32_t stableUs = kIdleStableUs) {
    const uint32_t start = micros();
    uint32_t highSince = micros();
    while ((micros() - start) < timeoutUs) {
        if (digitalRead(clkPin) && digitalRead(dataPin)) {
            if ((micros() - highSince) >= stableUs) {
                return true;
            }
        } else {
            highSince = micros();
        }
    }
    return false;
}

}  // namespace ps2

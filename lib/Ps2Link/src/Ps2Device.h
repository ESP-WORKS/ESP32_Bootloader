#pragma once

#include <Arduino.h>
#include "ByteQueue.h"
#include "Ps2Stats.h"

// PS/2 device (the T-Display, standing in for a keyboard on the mouse port).
// Always generates CLK. Sends unsolicited bytes; clocks in host-to-device
// frames when the host holds DATA low (Request-To-Send).
class Ps2Device {
public:
    Ps2Device(int clkPin, int dataPin);

    void begin();  // idempotent — safe to call again after ps2_init loops

    bool write(uint8_t b);
    bool write(const uint8_t* data, size_t n);
    bool write(const char* text);

    int available();
    int read();  // -1 if empty

    const Ps2Stats& stats() const { return stats_; }

private:
    bool sendByte(uint8_t data);
    bool receiveFromHost();
    bool handleHostAttention();
    void pulseClock();
    void taskLoop();
    static void taskEntry(void* arg);

    int clkPin_;
    int dataPin_;
    bool started_ = false;
    ByteQueue rx_;
    ByteQueue tx_;
    Ps2Stats stats_;
};

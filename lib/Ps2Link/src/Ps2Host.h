#pragma once

#include <Arduino.h>
#include "ByteQueue.h"
#include "Ps2Stats.h"

// PS/2 host (the VGA32 / "computer" side of the mouse port).
// Receives with a CLK falling-edge ISR. Sends by inhibiting the bus and
// letting the device generate the clock — same sequence a real keyboard uses.
class Ps2Host {
public:
    Ps2Host(int clkPin, int dataPin);

    void begin();

    // Enqueue a byte to be sent host→device. Returns false if the TX queue is full.
    bool write(uint8_t b);
    bool write(const uint8_t* data, size_t n);
    bool write(const char* text);  // without adding a newline

    int available();
    int read();  // -1 if empty

    uint16_t txQueued() const { return tx_.count(); }
    void flushRx();

    const Ps2Stats& stats() const { return stats_; }

private:
    static void IRAM_ATTR clockIsrThunk();
    void IRAM_ATTR onClockFall();
    void attachIsr();
    void detachIsr();
    bool sendByte(uint8_t data);
    void taskLoop();
    static void taskEntry(void* arg);

    int clkPin_;
    int dataPin_;

    volatile uint16_t shift_ = 0;
    volatile uint8_t bitCount_ = 0;
    volatile uint32_t lastEdgeUs_ = 0;

    bool started_ = false;
    ByteQueue rx_;
    ByteQueue tx_;
    Ps2Stats stats_;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    static Ps2Host* instance_;
};

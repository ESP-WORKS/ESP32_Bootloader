#include "Ps2Device.h"

#include <string.h>

#include "Ps2Gpio.h"

using ps2::kByteTimeoutUs;
using ps2::kHalfUs;
using ps2::kIdleTimeoutUs;
using ps2::kInterByteUs;
using ps2::kSetupUs;
using ps2::oddParity;
using ps2::pullLow;
using ps2::release;
using ps2::releaseBus;
using ps2::waitIdle;
using ps2::waitLevel;

Ps2Device::Ps2Device(int clkPin, int dataPin) : clkPin_(clkPin), dataPin_(dataPin) {}

void Ps2Device::begin() {
    releaseBus(clkPin_, dataPin_);
    if (started_) {
        return;
    }
    started_ = true;
    delay(20);
    xTaskCreatePinnedToCore(&Ps2Device::taskEntry, "ps2dev", 4096, this, 2, nullptr,
                            0);
}

bool Ps2Device::write(uint8_t b) { return tx_.push(b); }

bool Ps2Device::write(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!tx_.push(data[i])) {
            return false;
        }
    }
    return true;
}

bool Ps2Device::write(const char* text) {
    if (!text) {
        return false;
    }
    return write(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

int Ps2Device::available() { return static_cast<int>(rx_.count()); }

int Ps2Device::read() {
    uint8_t b = 0;
    return rx_.pop(&b) ? static_cast<int>(b) : -1;
}

void Ps2Device::pulseClock() {
    pullLow(clkPin_);
    delayMicroseconds(kHalfUs);
    release(clkPin_);
    delayMicroseconds(kHalfUs);
}

bool Ps2Device::sendByte(uint8_t data) {
    if (!waitIdle(clkPin_, dataPin_, kIdleTimeoutUs)) {
        stats_.timeoutErr++;
        releaseBus(clkPin_, dataPin_);
        return false;
    }

    const uint8_t parity = oddParity(data);
    // 11-bit frame: start(0), data LSB first, odd parity, stop(1).
    uint16_t frame = 0;
    frame |= 0;  // start
    frame |= static_cast<uint16_t>(data) << 1;
    frame |= static_cast<uint16_t>(parity) << 9;
    frame |= static_cast<uint16_t>(1) << 10;

    for (int i = 0; i < 11; i++) {
        if (frame & 1) {
            release(dataPin_);
        } else {
            pullLow(dataPin_);
        }
        frame >>= 1;
        delayMicroseconds(kSetupUs);
        pullLow(clkPin_);  // host samples DATA on this falling edge
        delayMicroseconds(kHalfUs);
        release(clkPin_);
        delayMicroseconds(kHalfUs);
        // Host inhibit: CLK stays low after we released it.
        if (digitalRead(clkPin_) == LOW) {
            releaseBus(clkPin_, dataPin_);
            stats_.timeoutErr++;
            return false;
        }
    }

    release(dataPin_);
    delayMicroseconds(kInterByteUs);
    return true;
}

bool Ps2Device::receiveFromHost() {
    // DATA is already the start bit (0), CLK is high after the host RTS.
    uint16_t frame = 0;
    for (int i = 0; i < 11; i++) {
        delayMicroseconds(kHalfUs);  // host places the next bit while CLK is high
        pullLow(clkPin_);
        delayMicroseconds(kSetupUs);
        const int bit = digitalRead(dataPin_);
        delayMicroseconds(kHalfUs - kSetupUs);
        release(clkPin_);
        frame |= static_cast<uint16_t>(bit) << i;
    }

    const uint8_t start = frame & 0x01;
    const uint8_t data = static_cast<uint8_t>((frame >> 1) & 0xFF);
    const uint8_t parity = static_cast<uint8_t>((frame >> 9) & 0x01);
    const uint8_t stop = static_cast<uint8_t>((frame >> 10) & 0x01);

    // ACK: pull DATA low and generate one extra clock.
    pullLow(dataPin_);
    delayMicroseconds(kHalfUs);
    pulseClock();
    release(dataPin_);

    if (start != 0 || stop != 1) {
        stats_.frameErr++;
        return false;
    }
    if (parity != oddParity(data)) {
        stats_.parityErr++;
        return false;
    }
    if (!rx_.push(data)) {
        stats_.overflowErr++;
        return false;
    }
    stats_.rxBytes++;
    return true;
}

bool Ps2Device::handleHostAttention() {
    // Host inhibit: CLK low. RTS: DATA low (with CLK high after inhibit).
    const uint32_t start = micros();
    while (digitalRead(clkPin_) == LOW && digitalRead(dataPin_) == HIGH) {
        if ((micros() - start) > kByteTimeoutUs) {
            return false;  // long inhibit, nothing to read
        }
    }

    if (!waitLevel(clkPin_, HIGH, kByteTimeoutUs)) {
        stats_.timeoutErr++;
        releaseBus(clkPin_, dataPin_);
        return false;
    }

    if (digitalRead(dataPin_) == LOW) {
        return receiveFromHost();
    }
    return false;
}

void Ps2Device::taskLoop() {
    for (;;) {
        if (digitalRead(clkPin_) == LOW || digitalRead(dataPin_) == LOW) {
            handleHostAttention();
            continue;
        }

        uint8_t b = 0;
        if (tx_.pop(&b)) {
            if (sendByte(b)) {
                stats_.txBytes++;
            } else {
                stats_.txFail++;
            }
            continue;
        }

        vTaskDelay(1);
    }
}

void Ps2Device::taskEntry(void* arg) { static_cast<Ps2Device*>(arg)->taskLoop(); }

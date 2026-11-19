#include "Ps2Host.h"

#include <string.h>

#include "Ps2Gpio.h"

using ps2::kByteTimeoutUs;
using ps2::kIdleTimeoutUs;
using ps2::kInhibitUs;
using ps2::kInterByteUs;
using ps2::kStuckFrameUs;
using ps2::oddParity;
using ps2::pullLow;
using ps2::release;
using ps2::releaseBus;
using ps2::waitIdle;
using ps2::waitLevel;

Ps2Host* Ps2Host::instance_ = nullptr;

Ps2Host::Ps2Host(int clkPin, int dataPin) : clkPin_(clkPin), dataPin_(dataPin) {}

void Ps2Host::begin() {
    instance_ = this;
    releaseBus(clkPin_, dataPin_);
    if (started_) {
        attachIsr();
        return;
    }
    started_ = true;
    delay(20);
    attachIsr();
    xTaskCreatePinnedToCore(&Ps2Host::taskEntry, "ps2host", 4096, this, 2, nullptr,
                            0);
}

void Ps2Host::attachIsr() {
    attachInterrupt(digitalPinToInterrupt(clkPin_), &Ps2Host::clockIsrThunk, FALLING);
}

void Ps2Host::detachIsr() { detachInterrupt(digitalPinToInterrupt(clkPin_)); }

void IRAM_ATTR Ps2Host::clockIsrThunk() {
    if (instance_) {
        instance_->onClockFall();
    }
}

void IRAM_ATTR Ps2Host::onClockFall() {
    const uint8_t bit = static_cast<uint8_t>(digitalRead(dataPin_));
    const uint32_t now = micros();

    portENTER_CRITICAL_ISR(&mux_);
    if (bitCount_ > 0 && (now - lastEdgeUs_) > kStuckFrameUs) {
        bitCount_ = 0;
        shift_ = 0;
        stats_.timeoutErr++;
    }
    lastEdgeUs_ = now;
    shift_ |= static_cast<uint16_t>(bit) << bitCount_;
    bitCount_++;

    if (bitCount_ >= 11) {
        const uint16_t frame = shift_;
        bitCount_ = 0;
        shift_ = 0;

        const uint8_t start = frame & 0x01;
        const uint8_t data = static_cast<uint8_t>((frame >> 1) & 0xFF);
        const uint8_t parity = static_cast<uint8_t>((frame >> 9) & 0x01);
        const uint8_t stop = static_cast<uint8_t>((frame >> 10) & 0x01);

        if (start != 0 || stop != 1) {
            stats_.frameErr++;
        } else if (parity != oddParity(data)) {
            stats_.parityErr++;
        } else if (!rx_.push(data)) {
            stats_.overflowErr++;
        } else {
            stats_.rxBytes++;
        }
    }
    portEXIT_CRITICAL_ISR(&mux_);
}

bool Ps2Host::write(uint8_t b) { return tx_.push(b); }

bool Ps2Host::write(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!tx_.push(data[i])) {
            return false;
        }
    }
    return true;
}

bool Ps2Host::write(const char* text) {
    if (!text) {
        return false;
    }
    return write(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

void Ps2Host::flushRx() {
    uint8_t b = 0;
    portENTER_CRITICAL(&mux_);
    while (rx_.pop(&b)) {
    }
    bitCount_ = 0;
    shift_ = 0;
    portEXIT_CRITICAL(&mux_);
}

int Ps2Host::available() {
    portENTER_CRITICAL(&mux_);
    const int n = static_cast<int>(rx_.count());
    portEXIT_CRITICAL(&mux_);
    return n;
}

int Ps2Host::read() {
    uint8_t b = 0;
    portENTER_CRITICAL(&mux_);
    const bool ok = rx_.pop(&b);
    portEXIT_CRITICAL(&mux_);
    return ok ? static_cast<int>(b) : -1;
}

bool Ps2Host::sendByte(uint8_t data) {
    detachIsr();

    auto abortSend = [this]() {
        releaseBus(clkPin_, dataPin_);
        attachIsr();
        stats_.timeoutErr++;
        return false;
    };

    if (!waitIdle(clkPin_, dataPin_, kIdleTimeoutUs)) {
        return abortSend();
    }

    // Inhibit the device, then Request-To-Send.
    pullLow(clkPin_);
    delayMicroseconds(kInhibitUs);
    pullLow(dataPin_);
    delayMicroseconds(10);
    release(clkPin_);

    // Device clocks the start bit (already 0 on DATA).
    if (!waitLevel(clkPin_, LOW, kByteTimeoutUs)) {
        return abortSend();
    }
    if (!waitLevel(clkPin_, HIGH, kByteTimeoutUs)) {
        return abortSend();
    }

    const uint8_t parity = oddParity(data);
    // 8 data bits (LSB first), odd parity, stop (1).
    uint16_t rest = static_cast<uint16_t>(data) | (static_cast<uint16_t>(parity) << 8) |
                    (static_cast<uint16_t>(1) << 9);

    for (int i = 0; i < 10; i++) {
        if (rest & 1) {
            release(dataPin_);
        } else {
            pullLow(dataPin_);
        }
        rest >>= 1;
        if (!waitLevel(clkPin_, LOW, kByteTimeoutUs)) {
            return abortSend();
        }
        if (!waitLevel(clkPin_, HIGH, kByteTimeoutUs)) {
            return abortSend();
        }
    }

    // Stop bit already released DATA. Device ACKs by pulling DATA low and clocking.
    release(dataPin_);
    if (!waitLevel(dataPin_, LOW, kByteTimeoutUs)) {
        return abortSend();
    }
    if (!waitLevel(clkPin_, LOW, kByteTimeoutUs)) {
        return abortSend();
    }
    waitIdle(clkPin_, dataPin_, kByteTimeoutUs);

    attachIsr();
    delayMicroseconds(kInterByteUs);
    return true;
}

void Ps2Host::taskLoop() {
    for (;;) {
        portENTER_CRITICAL(&mux_);
        if (bitCount_ > 0 && (micros() - lastEdgeUs_) > kStuckFrameUs) {
            bitCount_ = 0;
            shift_ = 0;
            stats_.timeoutErr++;
        }
        portEXIT_CRITICAL(&mux_);

        uint8_t b = 0;
        if (tx_.pop(&b)) {
            if (sendByte(b)) {
                stats_.txBytes++;
            } else {
                stats_.txFail++;
            }
        } else {
            vTaskDelay(1);
        }
    }
}

void Ps2Host::taskEntry(void* arg) { static_cast<Ps2Host*>(arg)->taskLoop(); }

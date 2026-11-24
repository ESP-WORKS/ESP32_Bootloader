#pragma once

#include <Arduino.h>
#include <stdint.h>

// Single-producer / single-consumer ring. ISR-safe when the caller wraps
// push/pop with the same portMUX (host RX path).
class ByteQueue {
public:
    static constexpr uint16_t kSize = 256;

    bool push(uint8_t b) {
        const uint16_t next = static_cast<uint16_t>((head_ + 1) % kSize);
        if (next == tail_) {
            return false;
        }
        buf_[head_] = b;
        head_ = next;
        return true;
    }

    bool peek(uint8_t* b) const {
        if (head_ == tail_) {
            return false;
        }
        *b = buf_[tail_];
        return true;
    }

    bool pop(uint8_t* b) {
        if (head_ == tail_) {
            return false;
        }
        *b = buf_[tail_];
        tail_ = static_cast<uint16_t>((tail_ + 1) % kSize);
        return true;
    }

    bool empty() const { return head_ == tail_; }

    uint16_t count() const {
        return static_cast<uint16_t>((head_ + kSize - tail_) % kSize);
    }

private:
    uint8_t buf_[kSize] = {};
    volatile uint16_t head_ = 0;
    volatile uint16_t tail_ = 0;
};

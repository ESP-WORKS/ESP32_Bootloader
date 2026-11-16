#pragma once
// ===========================================================================
// ps2_tx.h - Host->device config transmission on port 2 (GPIO26 CLK, GPIO27 DAT),
//            used to send the gamepad keymap to the T-Display.
//
// NOT real bidirectional PS/2. This is a simple host-clocked, one-way protocol:
// the Multiloader is the only side that drives the lines during a send, so there
// is no device ACK, no parity, no start/stop bit, and nothing to wait on. This
// avoids the "device never took the clock" aborts of the real PS/2 handshake.
//
// Per byte: 8 bits MSB first.
//   - put the bit on DAT
//   - wait TX_BIT_SETUP_US for DAT to settle
//   - pull CLK LOW for TX_CLK_HALF_US  (receiver samples DAT on this falling edge)
//   - release CLK HIGH for TX_CLK_HALF_US
// Between bytes: TX_BYTE_GAP_US pause (the receiver resyncs on a gap > ~500us).
//
// Lines are open-drain: INPUT_PULLUP = released HIGH, OUTPUT+LOW = driven LOW.
// Never drive a line HIGH actively. Both lines idle HIGH.
//
// The caller (sendKeymapForBoot) detaches the port-2 RX ISR and stops the VGA
// before calling this, so the bit timing isn't disturbed.
// ===========================================================================
#include <Arduino.h>
#include "keymap.h"

// These come from main.cpp (port-2 pins).
extern const int ps2DatPin2;   // GPIO27
extern const int ps2ClkPin2;   // GPIO26

// Config packet constants
#define PS2TX_MAGIC0   0xAA
#define PS2TX_MAGIC1   0x55
#define PS2TX_CMD_LOADMAP 0x01

// Timing (microseconds). Slower than strictly needed on purpose: wider pulses
// are less prone to the occasional single-bit glitch seen on some boots.
#define TX_BIT_SETUP_US  40   // DAT settle time before pulling CLK low
#define TX_CLK_HALF_US  100   // each clock half-period (LOW, then HIGH)
#define TX_BYTE_GAP_US  800   // gap between bytes (receiver resyncs on it)

// ---- low-level open-drain line helpers ----
static inline void ps2tx_clk_low()    { pinMode(ps2ClkPin2, OUTPUT);       digitalWrite(ps2ClkPin2, LOW); }
static inline void ps2tx_clk_release(){ pinMode(ps2ClkPin2, INPUT_PULLUP); }  // released = HIGH
static inline void ps2tx_dat_low()    { pinMode(ps2DatPin2, OUTPUT);       digitalWrite(ps2DatPin2, LOW); }
static inline void ps2tx_dat_high()   { pinMode(ps2DatPin2, INPUT_PULLUP); }  // released = HIGH
static inline int  ps2tx_clk_read()   { return digitalRead(ps2ClkPin2); }
static inline int  ps2tx_dat_read()   { return digitalRead(ps2DatPin2); }

// Send one byte, 8 bits MSB first. Host clocks every bit; no ACK, never fails.
// Returns true always (kept bool so the caller code is unchanged).
bool ps2_send_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; bit--) {
        // 1) place the data bit (open drain: 1 = release HIGH, 0 = drive LOW)
        if (value & (1 << bit)) ps2tx_dat_high(); else ps2tx_dat_low();
        delayMicroseconds(TX_BIT_SETUP_US);

        // 2) falling edge of CLK — receiver samples DAT here
        ps2tx_clk_low();
        delayMicroseconds(TX_CLK_HALF_US);

        // 3) rising edge of CLK
        ps2tx_clk_release();
        delayMicroseconds(TX_CLK_HALF_US);
    }
    // leave DAT released (HIGH) between bytes
    ps2tx_dat_high();
    return true;
}

// Serialize and send the config packet (name + keymap):
//   0xAA 0x55 0x01  NAME_LEN  name[NAME_LEN]  MAP_LEN  [id ext code]*MAP_LEN  CHECKSUM
//   CHECKSUM = XOR of every byte from the first magic to the last map triple.
// Only buttons with a mapped, known key name are included. The caller must have
// stopped the port-2 ISR first. Returns true if the whole packet went out.
bool ps2_send_keymap(const Keymap& km) {
    uint8_t pkt[8 + 32 + GP_BUTTON_COUNT * 3];   // header + name(<=31) + triples
    int n = 0;
    pkt[n++] = PS2TX_MAGIC0;
    pkt[n++] = PS2TX_MAGIC1;
    pkt[n++] = PS2TX_CMD_LOADMAP;

    // Name (max 31 bytes, ASCII, no terminator on the wire).
    uint8_t nameLen = (uint8_t)strnlen(km.name, 31);
    pkt[n++] = nameLen;
    for (int i = 0; i < nameLen; i++) pkt[n++] = (uint8_t)km.name[i];

    // Map length placeholder, then the button triples.
    int mapLenIdx = n++;
    uint8_t count = 0;
    for (int i = 0; i < GP_BUTTON_COUNT; i++) {
        uint8_t ext, code;
        if (!keyNameToScancode(km.keys[i], ext, code)) continue;  // empty/unknown -> skip
        pkt[n++] = GP_BUTTONS[i].id;
        pkt[n++] = ext;
        pkt[n++] = code;
        count++;
    }
    pkt[mapLenIdx] = count;

    // checksum = XOR of all bytes so far
    uint8_t chk = 0;
    for (int i = 0; i < n; i++) chk ^= pkt[i];
    pkt[n++] = chk;

    Serial.printf("[PS2TX] sending config: name='%s' (%d), %d buttons, %d bytes\n",
                  km.name, nameLen, count, n);

    // Make sure both lines start idle HIGH, with a settle gap.
    ps2tx_clk_release();
    ps2tx_dat_high();
    delayMicroseconds(TX_BYTE_GAP_US);

    // The link is one-way (the host is asleep by boot time, so the T-Display can't
    // ask for a resend). To survive the occasional single-bit glitch, send the whole
    // packet 3 times; the T-Display accepts the first copy with a good checksum and
    // ignores the rest (it dedups for 5s).
    const int TX_REPEATS = 3;
    for (int retry = 0; retry < TX_REPEATS; retry++) {
        Serial.printf("[PS2TX] tentativa %d/%d\n", retry + 1, TX_REPEATS);
        for (int i = 0; i < n; i++) {
            ps2_send_byte(pkt[i]);
            delayMicroseconds(TX_BYTE_GAP_US);   // gap lets the receiver resync
        }
        if (retry < TX_REPEATS - 1) delay(30);   // pause between full copies
    }

    Serial.println("[PS2TX] config sent OK (3x)");
    return true;
}
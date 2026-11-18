#pragma once
// ===========================================================================
// ps2_tx.h — Host -> T-Display on port 2 (GPIO26 CLK, GPIO27 DAT).
//
// Real PS/2 host-to-device: the T-Display generates every clock. The host
// only pulls DAT (and a short CLK inhibit). Host-generated clocks do not
// reach GPIO17 through the level shifter; device-generated clocks do
// (the bootloader already receives key bytes such as 0x5A on this port).
//
// Frame, LSB first: start 0, 8 data, odd parity, stop 1, then device ACK.
// The host sets each bit while CLK is low. The device samples at the end
// of that low period.
// ===========================================================================
#include <Arduino.h>
#include <driver/gpio.h>
#include "keymap.h"

extern const int ps2DatPin2;   // GPIO27
extern const int ps2ClkPin2;   // GPIO26

#define PS2TX_MAGIC0      0xAA
#define PS2TX_MAGIC1      0x55
#define PS2TX_CMD_LOADMAP 0x01

// Device segura CLK low por 5ms e amostra aos 4ms. pinMode() cabe nessa janela.
// Um glitch nao conta: o low tem que durar TX_HOLD_US.
#define TX_HOLD_US        500
#define TX_FIRST_WAIT_US  100000
#define TX_BIT_WAIT_US    15000
#define TX_BYTE_GAP_MS    15

static inline void ps2tx_clk_low()     { pinMode(ps2ClkPin2, OUTPUT); digitalWrite(ps2ClkPin2, LOW); }
static inline void ps2tx_clk_release() { pinMode(ps2ClkPin2, INPUT_PULLUP); }
static inline void ps2tx_dat_set(int bit) {
    if (bit) pinMode(ps2DatPin2, INPUT_PULLUP);
    else { pinMode(ps2DatPin2, OUTPUT); digitalWrite(ps2DatPin2, LOW); }
}

// Depois de ver CLK alto, exige que ele permaneca low por holdUs.
// Glitch de ruido nao passa. O inhibit proprio tambem nao: ele acaba
// antes de a linha voltar a alto.
static bool ps2tx_wait_held_low(uint32_t holdUs, uint32_t timeoutUs) {
    uint32_t t0 = micros();
    bool sawHigh = false;
    bool inLow = false;
    uint32_t lowAt = 0;
    while ((uint32_t)(micros() - t0) < timeoutUs) {
        if (digitalRead(ps2ClkPin2)) {
            sawHigh = true;
            inLow = false;
        } else if (sawHigh) {
            if (!inLow) { inLow = true; lowAt = micros(); }
            if ((uint32_t)(micros() - lowAt) >= holdUs) return true;
        }
    }
    return false;
}

static bool ps2tx_wait_dat_low(uint32_t holdUs, uint32_t timeoutUs) {
    uint32_t t0 = micros();
    bool inLow = false;
    uint32_t lowAt = 0;
    while ((uint32_t)(micros() - t0) < timeoutUs) {
        if (digitalRead(ps2DatPin2)) {
            inLow = false;
        } else {
            if (!inLow) { inLow = true; lowAt = micros(); }
            if ((uint32_t)(micros() - lowAt) >= holdUs) return true;
        }
    }
    return false;
}

static void ps2tx_bus_init() {
    gpio_reset_pin((gpio_num_t)ps2ClkPin2);
    gpio_reset_pin((gpio_num_t)ps2DatPin2);
    ps2tx_clk_release();
    ps2tx_dat_set(1);
}

// One byte. Returns false if the T-Display never takes the clock.
bool ps2_send_byte(uint8_t value) {
    int ones = 0;
    for (int i = 0; i < 8; i++) if (value & (1 << i)) ones++;
    int parity = (ones & 1) ? 0 : 1;   // odd

    ps2tx_dat_set(0);
    ps2tx_clk_low();
    delayMicroseconds(300);
    ps2tx_clk_release();

    // Start: DAT ja esta em 0. Os demais bits sao escritos quando o
    // CLK low do device ja segurou 500us — sobram ~3.5ms antes da amostra.
    if (!ps2tx_wait_held_low(TX_HOLD_US, TX_FIRST_WAIT_US)) return false;
    for (int i = 0; i < 8; i++) {
        if (!ps2tx_wait_held_low(TX_HOLD_US, TX_BIT_WAIT_US)) return false;
        ps2tx_dat_set((value >> i) & 1);
    }
    if (!ps2tx_wait_held_low(TX_HOLD_US, TX_BIT_WAIT_US)) return false;
    ps2tx_dat_set(parity);
    if (!ps2tx_wait_held_low(TX_HOLD_US, TX_BIT_WAIT_US)) return false;
    ps2tx_dat_set(1);

    // ACK do device: ele puxa DAT e segura. Sem isso o byte nao conta.
    ps2tx_dat_set(1);
    ps2tx_clk_release();
    return ps2tx_wait_dat_low(400, 20000);
}

bool ps2_send_keymap(const Keymap& km) {
    uint8_t pkt[8 + 32 + GP_BUTTON_COUNT * 3];
    int n = 0;
    pkt[n++] = PS2TX_MAGIC0;
    pkt[n++] = PS2TX_MAGIC1;
    pkt[n++] = PS2TX_CMD_LOADMAP;

    uint8_t nameLen = (uint8_t)strnlen(km.name, 31);
    pkt[n++] = nameLen;
    for (int i = 0; i < nameLen; i++) pkt[n++] = (uint8_t)km.name[i];

    int mapLenIdx = n++;
    uint8_t count = 0;
    for (int i = 0; i < GP_BUTTON_COUNT; i++) {
        uint8_t ext, code;
        if (!keyNameToScancode(km.keys[i], ext, code)) continue;
        pkt[n++] = GP_BUTTONS[i].id;
        pkt[n++] = ext;
        pkt[n++] = code;
        count++;
    }
    pkt[mapLenIdx] = count;

    uint8_t chk = 0;
    for (int i = 0; i < n; i++) chk ^= pkt[i];
    pkt[n++] = chk;

    Serial.printf("[PS2TX] sending config: name='%s' (%d), %d buttons, %d bytes\n",
                  km.name, nameLen, count, n);

    ps2tx_bus_init();

    const int TX_REPEATS = 3;
    int okBytes = 0, failBytes = 0;
    for (int retry = 0; retry < TX_REPEATS; retry++) {
        Serial.printf("[PS2TX] tentativa %d/%d\n", retry + 1, TX_REPEATS);
        for (int i = 0; i < n; i++) {
            if (!ps2_send_byte(pkt[i])) {
                Serial.printf("[PS2TX] byte %d sem ACK — T-Display nao segurou DAT\n", i);
                failBytes++;
                break;
            }
            okBytes++;
            delay(TX_BYTE_GAP_MS);
        }
        if (retry < TX_REPEATS - 1) delay(80);
    }

    ps2tx_clk_release();
    ps2tx_dat_set(1);
    Serial.printf("[PS2TX] fim: %d bytes com ACK, %d falhas\n", okBytes, failBytes);
    return failBytes == 0;
}

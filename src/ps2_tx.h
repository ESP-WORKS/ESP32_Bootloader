#pragma once
// ===========================================================================
// ps2_tx.h — Host -> T-Display no DIN do teclado (GPIO33 CLK, GPIO32 DAT).
//
// Uses Ps2Link (same protocol as ps2-proto-vga). The T-Display generates
// every clock. Packet layout is unchanged:
//   0xAA 0x55 0x01  NAME_LEN  name[]  MAP_LEN  [id ext code]*  CHECKSUM
// ===========================================================================
#include <Arduino.h>
#include "keymap.h"

#define PS2TX_MAGIC0      0xAA
#define PS2TX_MAGIC1      0x55
#define PS2TX_CMD_PING      0x00
#define PS2TX_CMD_LOADMAP   0x01
#define PS2TX_CMD_LEARN_ON  0x02
#define PS2TX_CMD_LEARN_OFF 0x03
#define PS2TX_CMD_REMAP     0x04
#define PS2TX_CMD_SCAN_ON   0x05
#define PS2TX_CMD_SCAN_OFF  0x06
#define PS2TX_CMD_LIST      0x07
#define PS2TX_CMD_PAIR      0x08
#define PS2TX_CMD_UNPAIR    0x09
#define PS2TX_CMD_STATUS    0x0A
#define PS2TX_PING_R0     0xA5
#define PS2TX_PING_R1     0x5A
#define PS2TX_PING_R2     0xA5
#define PS2TX_LEARN_R0    0xA5
#define PS2TX_LEARN_R1    0x5A
#define PS2TX_ANA_R1      0x5B
#define PS2TX_LIST_R0     0xA5
#define PS2TX_LIST_R1     0x5C
#define PS2TX_ACK_R0      0xA5
#define PS2TX_ACK_R1      0x5E
#define PS2TX_STAT_R0     0xA5
#define PS2TX_STAT_R1     0x5F
#define PS2TX_GP_MAX      6
#define PS2TX_GP_NAME_MAX 20

struct GpStatus {
    uint8_t flags;
    char    name[PS2TX_GP_NAME_MAX + 1];
};

bool ps2host_send_bytes(const uint8_t* data, int n);

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

    const int TX_REPEATS = 3;
    bool ok = false;
    for (int retry = 0; retry < TX_REPEATS; retry++) {
        Serial.printf("[PS2TX] tentativa %d/%d\n", retry + 1, TX_REPEATS);
        if (ps2host_send_bytes(pkt, n)) {
            ok = true;
            break;
        }
        Serial.println("[PS2TX] falha — repetindo");
        delay(80);
    }
    Serial.printf("[PS2TX] fim: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

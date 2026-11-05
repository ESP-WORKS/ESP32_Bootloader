#pragma once
// ===========================================================================
// wifisetup.h - First-run WiFi setup when /wificonfig.rc is missing.
// Scans networks, lets the user pick one (Q/A + Enter), type the password on
// the PS/2 keyboard, connects, and on success writes /wificonfig.rc.
// ===========================================================================
#include "bootloader.h"
#include <WiFi.h>

#define WIFI_MAX_LIST   7
#define WIFI_PASS_MAX   64

// PS/2 set-2 make code -> ASCII (unshifted / shifted). Only the keys needed for
// a WiFi password: letters, digits, common symbols, space. 0 = ignore.
struct Ps2Map { uint8_t code; char lo; char hi; };
static const Ps2Map PS2_ASCII[] = {
    {0x1C,'a','A'},{0x32,'b','B'},{0x21,'c','C'},{0x23,'d','D'},{0x24,'e','E'},
    {0x2B,'f','F'},{0x34,'g','G'},{0x33,'h','H'},{0x43,'i','I'},{0x3B,'j','J'},
    {0x42,'k','K'},{0x4B,'l','L'},{0x3A,'m','M'},{0x31,'n','N'},{0x44,'o','O'},
    {0x4D,'p','P'},{0x15,'q','Q'},{0x2D,'r','R'},{0x1B,'s','S'},{0x2C,'t','T'},
    {0x3C,'u','U'},{0x2A,'v','V'},{0x1D,'w','W'},{0x22,'x','X'},{0x35,'y','Y'},
    {0x1A,'z','Z'},
    {0x45,'0',')'},{0x16,'1','!'},{0x1E,'2','@'},{0x26,'3','#'},{0x25,'4','$'},
    {0x2E,'5','%'},{0x36,'6','^'},{0x3D,'7','&'},{0x3E,'8','*'},{0x46,'9','('},
    {0x29,' ',' '},
    {0x4E,'-','_'},{0x55,'=','+'},{0x54,'[','{'},{0x5B,']','}'},{0x4C,';',':'},
    {0x52,'\'','"'},{0x41,',','<'},{0x49,'.','>'},{0x4A,'/','?'},{0x5D,'\\','|'},
    {0x0E,'`','~'},
};
static const int PS2_ASCII_COUNT = sizeof(PS2_ASCII) / sizeof(PS2_ASCII[0]);

// Returns the ASCII char for a make code given shift state, or 0 if not printable.
static char ps2CodeToAscii(uint8_t code, bool shift) {
    for (int i = 0; i < PS2_ASCII_COUNT; i++)
        if (PS2_ASCII[i].code == code)
            return shift ? PS2_ASCII[i].hi : PS2_ASCII[i].lo;
    return 0;
}

// Draw the network list with the cursor.
static void wifiDrawList(String* ssids, int n, int sel) {
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("SELECT WIFI NETWORK", C_YELLOW);
    statusLine("Q/A = move   ENTER = select", C_CYAN);
    statusY += 6;

    for (int i = 0; i < n; i++) {
        bool is = (i == sel);
        char line[48];
        snprintf(line, sizeof(line), "%s%s", is ? "> " : "  ", ssids[i].c_str());
        drawString(14, statusY, line, is ? C_BLACK : C_GREEN, is ? C_GREEN : C_BLACK);
        if (is) fillRect(8, statusY, 4, 12, C_GREEN);
        statusY += 12;
    }
}

// Raw one-byte read from the PS/2 ring (the ring globals are extern'd in
// bootloader.h). Blocks until a byte is available. We need raw bytes here so we
// can track Shift make/break, which ps2_get_key() hides.
static uint8_t wifiRawByte() {
    while (true) {
        portENTER_CRITICAL(&ps2Mux);
        bool has = (ps2_head != ps2_tail);
        uint8_t b = 0;
        if (has) { b = ps2_buffer[ps2_tail]; ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE; }
        portEXIT_CRITICAL(&ps2Mux);
        if (has) return b;
        delay(1);
    }
}

// Password entry via PS/2. Fills out[]. Enter finishes, Esc cancels (false),
// Backspace deletes. Shift (0x12 / 0x59) tracked via raw make/break stream.
static bool wifiReadPassword(const char* ssid, char* out, size_t outSz) {
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("WIFI PASSWORD", C_YELLOW);
    char net[48];
    snprintf(net, sizeof(net), "Network: %s", ssid);
    statusLine(net, C_CYAN);
    statusLine("Type password, ENTER=OK  ESC=cancel", C_WHITE);

    int inputY = statusY + 8;
    size_t len = 0;
    out[0] = '\0';
    bool shift = false;

    ps2_flush();
    while (true) {
        uint8_t b = wifiRawByte();

        if (b == 0xF0) {               // break: next byte is the released key
            uint8_t rel = wifiRawByte();
            if (rel == 0x12 || rel == 0x59) shift = false;   // shift released
            continue;
        }
        if (b == 0xE0) { wifiRawByte(); continue; }          // extended: ignore pair

        if (b == 0x12 || b == 0x59) { shift = true; continue; } // shift pressed
        if (b == KEY_ENTER) { out[len] = '\0'; return true; }
        if (b == KEY_ESC)   { return false; }
        if (b == 0x66) {               // Backspace
            if (len > 0) { len--; out[len] = '\0'; }
        } else {
            char c = ps2CodeToAscii(b, shift);
            if (c && len < outSz - 1) { out[len++] = c; out[len] = '\0'; }
        }

        fillRect(10, inputY, HRES - 20, 12, C_BLACK);
        drawString(10, inputY, out, C_GREEN, C_BLACK);
    }
}

// Full first-run flow. Returns true if a config was written (WiFi configured).
bool wifiSetupIfNeeded() {
    if (SD.exists(WIFICONFIG_FILE)) return true;   // already configured

    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("WIFI SETUP", C_YELLOW);
    statusLine("Scanning networks...", C_CYAN);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int found = WiFi.scanNetworks();
    if (found <= 0) {
        statusLine("No networks found", C_RED);
        statusLine("Press any key to continue", C_YELLOW);
        ps2_flush(); while (ps2_get_key() == 0);
        WiFi.scanDelete();
        return false;
    }

    int n = found < WIFI_MAX_LIST ? found : WIFI_MAX_LIST;
    String ssids[WIFI_MAX_LIST];
    for (int i = 0; i < n; i++) ssids[i] = WiFi.SSID(i);
    WiFi.scanDelete();

    int sel = 0;
    wifiDrawList(ssids, n, sel);
    ps2_flush();

    // Pick a network with Q/A + Enter.
    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && sel > 0) {
            sel--; wifiDrawList(ssids, n, sel);
        } else if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) && sel < n - 1) {
            sel++; wifiDrawList(ssids, n, sel);
        } else if (key == KEY_ENTER) {
            break;
        } else if (key == KEY_ESC) {
            return false;
        }
    }

    // Ask the password.
    char pass[WIFI_PASS_MAX];
    if (!wifiReadPassword(ssids[sel].c_str(), pass, sizeof(pass))) return false;

    // Try to connect.
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("WIFI SETUP", C_YELLOW);
    char msg[64];
    snprintf(msg, sizeof(msg), "Connecting to %s...", ssids[sel].c_str());
    statusLine(msg, C_CYAN);

    WiFi.begin(ssids[sel].c_str(), pass);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) { delay(500); tries++; }

    if (WiFi.status() != WL_CONNECTED) {
        statusLine("Connection FAILED", C_RED);
        statusLine("Wrong password? Press any key", C_YELLOW);
        WiFi.disconnect(true);
        ps2_flush(); while (ps2_get_key() == 0);
        return false;
    }

    // Save /wificonfig.rc (line1=SSID, line2=password).
    File f = SD.open(WIFICONFIG_FILE, FILE_WRITE);
    if (f) {
        f.printf("%s\n%s\n", ssids[sel].c_str(), pass);
        f.close();
    }
    statusLine("Connected! Config saved.", C_GREEN);
    statusLine("Press any key to continue", C_YELLOW);
    WiFi.disconnect(true);
    ps2_flush(); while (ps2_get_key() == 0);
    return true;
}
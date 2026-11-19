#pragma once
// ===========================================================================
// keymap.h - Part 1: gamepad->PS/2 keymap infrastructure for the Multiloader.
//   - Keymap struct (17 gamepad buttons -> key name)
//   - NAME_TO_SCANCODE table (key name -> {extended flag, PS/2 Set 2 scancode})
//   - loadKeymap(): parse /<emulator>/keymap.ini from the SD card
// Part 2 (PS/2 host->device TX) and Part 3 (boot integration) build on this.
//
// The scancode table MUST stay identical to the one embedded in the T-Display
// firmware, or the keys won't match.
// ===========================================================================
#include <Arduino.h>
#include <SD.h>

// --- Gamepad button IDs (must match the T-Display side) ---
enum GpButtonId : uint8_t {
    GP_A = 0x01, GP_B = 0x02, GP_X = 0x03, GP_Y = 0x04,
    GP_UP = 0x05, GP_DOWN = 0x06, GP_LEFT = 0x07, GP_RIGHT = 0x08,
    GP_L1 = 0x09, GP_R1 = 0x0A, GP_L2 = 0x0B, GP_R2 = 0x0C,
    GP_L3 = 0x0D, GP_R3 = 0x0E, GP_START = 0x0F, GP_SELECT = 0x10,
    GP_HOME = 0x11,
};
#define GP_BUTTON_COUNT 17

// Order of buttons in the Keymap.keys[] array (index -> button id + ini name).
struct GpButtonDef { uint8_t id; const char* iniName; };
static const GpButtonDef GP_BUTTONS[GP_BUTTON_COUNT] = {
    { GP_A,     "A"     }, { GP_B,      "B"      }, { GP_X,    "X"    }, { GP_Y,     "Y"     },
    { GP_UP,    "UP"    }, { GP_DOWN,   "DOWN"   }, { GP_LEFT, "LEFT" }, { GP_RIGHT, "RIGHT" },
    { GP_L1,    "L1"    }, { GP_R1,     "R1"     }, { GP_L2,   "L2"   }, { GP_R2,    "R2"    },
    { GP_L3,    "L3"    }, { GP_R3,     "R3"     }, { GP_START,"START"}, { GP_SELECT,"SELECT"},
    { GP_HOME,  "HOME"  },
};

static bool gpButtonPresent(int i) {
    uint8_t id = GP_BUTTONS[i].id;
    return id != GP_L3 && id != GP_R3;
}

// --- Key name -> PS/2 Set 2 scancode (+ extended flag) ---
struct ScancodeEntry { const char* name; uint8_t ext; uint8_t code; };

// PS/2 Set 2 make codes. ext=1 means the make code is prefixed with 0xE0 on the wire.
static const ScancodeEntry NAME_TO_SCANCODE[] = {
    // Letters
    {"A",0,0x1C},{"B",0,0x32},{"C",0,0x21},{"D",0,0x23},{"E",0,0x24},{"F",0,0x2B},
    {"G",0,0x34},{"H",0,0x33},{"I",0,0x43},{"J",0,0x3B},{"K",0,0x42},{"L",0,0x4B},
    {"M",0,0x3A},{"N",0,0x31},{"O",0,0x44},{"P",0,0x4D},{"Q",0,0x15},{"R",0,0x2D},
    {"S",0,0x1B},{"T",0,0x2C},{"U",0,0x3C},{"V",0,0x2A},{"W",0,0x1D},{"X",0,0x22},
    {"Y",0,0x35},{"Z",0,0x1A},
    // Digits (top row)
    {"0",0,0x45},{"1",0,0x16},{"2",0,0x1E},{"3",0,0x26},{"4",0,0x25},
    {"5",0,0x2E},{"6",0,0x36},{"7",0,0x3D},{"8",0,0x3E},{"9",0,0x46},
    // Function keys
    {"F1",0,0x05},{"F2",0,0x06},{"F3",0,0x04},{"F4",0,0x0C},{"F5",0,0x03},{"F6",0,0x0B},
    {"F7",0,0x83},{"F8",0,0x0A},{"F9",0,0x01},{"F10",0,0x09},{"F11",0,0x78},{"F12",0,0x07},
    // Control / whitespace
    {"ENTER",0,0x5A},{"RETURN",0,0x5A},{"ESC",0,0x76},{"ESCAPE",0,0x76},
    {"SPACE",0,0x29},{"TAB",0,0x0D},{"BACKSPACE",0,0x66},{"BKSP",0,0x66},
    {"SHIFT",0,0x12},{"LSHIFT",0,0x12},{"RSHIFT",0,0x59},
    {"CTRL",0,0x14},{"LCTRL",0,0x14},
    {"ALT",0,0x11},{"LALT",0,0x11},
    {"CAPS",0,0x58},{"CAPSLOCK",0,0x58},
    // Symbols
    {"MINUS",0,0x4E},{"-",0,0x4E},{"EQUAL",0,0x55},{"=",0,0x55},
    {"LBRACKET",0,0x54},{"[",0,0x54},{"RBRACKET",0,0x5B},{"]",0,0x5B},
    {"SEMICOLON",0,0x4C},{";",0,0x4C},{"APOSTROPHE",0,0x52},{"'",0,0x52},
    {"COMMA",0,0x41},{",",0,0x41},{"PERIOD",0,0x49},{".",0,0x49},
    {"SLASH",0,0x4A},{"/",0,0x4A},{"BACKSLASH",0,0x5D},{"\\",0,0x5D},
    {"GRAVE",0,0x0E},{"`",0,0x0E},
    // Extended keys (prefixed 0xE0)
    {"UP",1,0x75},{"DOWN",1,0x72},{"LEFT",1,0x6B},{"RIGHT",1,0x74},
    {"INSERT",1,0x70},{"DELETE",1,0x71},{"DEL",1,0x71},{"HOME",1,0x6C},{"END",1,0x69},
    {"PGUP",1,0x7D},{"PAGEUP",1,0x7D},{"PGDN",1,0x7A},{"PAGEDOWN",1,0x7A},
    {"RCTRL",1,0x14},{"RALT",1,0x11},
};
static const int NAME_TO_SCANCODE_COUNT =
    sizeof(NAME_TO_SCANCODE) / sizeof(NAME_TO_SCANCODE[0]);

// Resolve a key name to its scancode. Returns true and fills ext/code on success,
// false if the name is empty/unknown.
static bool keyNameToScancode(const char* name, uint8_t& ext, uint8_t& code) {
    if (!name || name[0] == '\0') return false;
    for (int i = 0; i < NAME_TO_SCANCODE_COUNT; i++) {
        if (strcasecmp(name, NAME_TO_SCANCODE[i].name) == 0) {
            ext  = NAME_TO_SCANCODE[i].ext;
            code = NAME_TO_SCANCODE[i].code;
            return true;
        }
    }
    return false;
}

#define BOOTLKEY_FILE "/bootlkey.rc"

// --- Keymap struct: one key name per gamepad button (empty = unused) ---
struct Keymap {
    char name[32];                    // emulator name from the ini (optional)
    char keys[GP_BUTTON_COUNT][16];   // key name per button, indexed like GP_BUTTONS[]
    bool valid;                       // true if the file parsed OK

    void clear() {
        name[0] = '\0';
        for (int i = 0; i < GP_BUTTON_COUNT; i++) keys[i][0] = '\0';
        valid = false;
    }

    // Default embutido (e template de /bootlkey.rc).
    //   A=ENTER  B=SPACE  X=M  Y=N
    //   UP/DOWN/LEFT/RIGHT = setas
    //   L1=0  R1=1  L2=F12  R2=F11
    //   START=F5  SELECT=ESC  HOME=ESC
    //   L3/R3 omitted — these pads have no stick-click
    void fillBuiltinDefaults() {
        clear();
        strcpy(keys[ 0], "ENTER");
        strcpy(keys[ 1], "SPACE");
        strcpy(keys[ 2], "M");
        strcpy(keys[ 3], "N");
        strcpy(keys[ 4], "UP");
        strcpy(keys[ 5], "DOWN");
        strcpy(keys[ 6], "LEFT");
        strcpy(keys[ 7], "RIGHT");
        strcpy(keys[ 8], "0");
        strcpy(keys[ 9], "1");
        strcpy(keys[10], "F12");
        strcpy(keys[11], "F11");
        strcpy(keys[14], "F5");
        strcpy(keys[15], "ESC");
        strcpy(keys[16], "ESC");   // HOME
        valid = true;
    }

    void loadDefaults();
};

static Keymap gBootlKey;

inline void Keymap::loadDefaults() {
    if (gBootlKey.valid) {
        for (int i = 0; i < GP_BUTTON_COUNT; i++)
            memcpy(keys[i], gBootlKey.keys[i], sizeof(keys[0]));
        valid = true;
        return;
    }
    fillBuiltinDefaults();
}

// Find the GP_BUTTONS[] index for an ini key name ("A", "UP", ...). -1 if none.
static int gpButtonIndexByName(const char* iniName) {
    for (int i = 0; i < GP_BUTTON_COUNT; i++)
        if (strcasecmp(iniName, GP_BUTTONS[i].iniName) == 0) return i;
    return -1;
}

// Parse one "key=value" line into the keymap. Unknown keys are ignored.
static void keymapParseLine(const char* line, Keymap& km) {
    const char* eq = strchr(line, '=');
    if (!eq) return;

    char key[24];
    size_t klen = eq - line;
    if (klen >= sizeof(key)) klen = sizeof(key) - 1;
    memcpy(key, line, klen);
    key[klen] = '\0';
    // trim trailing spaces on the key
    while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t')) key[--klen] = '\0';

    const char* val = eq + 1;
    while (*val == ' ' || *val == '\t') val++;        // skip leading spaces
    char value[16];
    strncpy(value, val, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    // trim trailing \r \n and spaces
    size_t vlen = strlen(value);
    while (vlen > 0 && (value[vlen-1] == '\r' || value[vlen-1] == '\n' ||
                        value[vlen-1] == ' '  || value[vlen-1] == '\t')) value[--vlen] = '\0';

    if (strcasecmp(key, "name") == 0) {
        strncpy(km.name, value, sizeof(km.name) - 1);
        km.name[sizeof(km.name) - 1] = '\0';
        return;
    }

    int idx = gpButtonIndexByName(key);
    if (idx >= 0) {
        strncpy(km.keys[idx], value, sizeof(km.keys[0]) - 1);
        km.keys[idx][sizeof(km.keys[0]) - 1] = '\0';
    }
}

// Load /<folderPath>/keymap.ini into km. folderPath is like "/ZX 81".
// Returns true if the file existed and parsed; false otherwise (caller then
// keeps the T-Display's built-in default — no packet is sent).
static bool loadKeymap(const char* folderPath, Keymap& km) {
    km.clear();

    char path[160];
    snprintf(path, sizeof(path), "%s/keymap.ini", folderPath);
    if (!SD.exists(path)) {
        Serial.printf("[KEYMAP] %s not found\n", path);
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[KEYMAP] cannot open %s\n", path); return false; }

    char line[64];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;   // comment
        keymapParseLine(line, km);
    }
    f.close();

    // Fallback: if the ini had no (or empty) name=, use the emulator folder name.
    if (km.name[0] == '\0') {
        const char* base = folderPath;
        while (*base == '/') base++;                 // strip leading slash(es)
        const char* slash = strrchr(base, '/');      // in case of nested path
        if (slash) base = slash + 1;
        strncpy(km.name, base, sizeof(km.name) - 1);
        km.name[sizeof(km.name) - 1] = '\0';
    }

    km.valid = true;
    Serial.printf("[KEYMAP] loaded %s (name='%s')\n", path, km.name);
    return true;
}

static void writeBootlKeyFile(const Keymap& km) {
    File wf = SD.open(BOOTLKEY_FILE, FILE_WRITE);
    if (!wf) {
        Serial.println("[BOOTLKEY] cannot create /bootlkey.rc");
        return;
    }
    for (int i = 0; i < GP_BUTTON_COUNT; i++) {
        if (gpButtonPresent(i) && km.keys[i][0])
            wf.printf("%s=%s\n", GP_BUTTONS[i].iniName, km.keys[i]);
    }
    wf.close();
    Serial.println("[BOOTLKEY] wrote /bootlkey.rc");
}

static void ensureBootlKeyRc() {
    gBootlKey.fillBuiltinDefaults();
    if (!SD.exists(BOOTLKEY_FILE)) {
        writeBootlKeyFile(gBootlKey);
        return;
    }
    File f = SD.open(BOOTLKEY_FILE, FILE_READ);
    if (!f) {
        Serial.println("[BOOTLKEY] cannot open /bootlkey.rc");
        return;
    }
    char line[64];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        keymapParseLine(line, gBootlKey);
    }
    f.close();
    gBootlKey.valid = true;
    Serial.println("[BOOTLKEY] loaded /bootlkey.rc");
}
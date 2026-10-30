#pragma once
// ===========================================================================
// bootloader.h - Central hub: system includes, config, globals, module includes
// Include this ONCE at the top of main.cpp.
// ===========================================================================

#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <fabgl.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_efuse.h"
#include "logo.h"

// SD pinout — same scheme as ESPectrum FileUtils / hardpins.h
// CLK/MOSI/CS are shared; only MISO differs by board/chip package
#define SD_CLK   14
#define SD_MOSI  12
#define SD_CS    13
#define SD_MISO_LILYGO  2   // TTGO VGA32 / LILYGO / ESP32PICOD4 / D0WDQ6
#define SD_MISO_OLIMEX  35  // Olimex SBC-FABGL / WROVER (ESP32D0WDQ5)

#define FIRMWARE_FILE  "/firmware.bin"
#define VERSION_FILE   "/version.txt"

#define PS2_DAT 32
#define PS2_CLK 33

#define KEY_UP      0x75
#define KEY_Q       0x15
#define KEY_DOWN    0x72
#define KEY_A_NAV   0x1C
#define KEY_ENTER   0x5A
#define KEY_F1      0x05
#define KEY_1       0x16
#define KEY_N_KEY   0x31
#define KEY_A_KEY   0x1C
#define KEY_O_KEY   0x44
#define KEY_R_KEY   0x2D
#define KEY_D_KEY   0x23
#define KEY_ESC     0x76
#define KEY_SPACE   0x29
#define KEY_F2      0x06
#define KEY_2       0x1E
#define KEY_E_KEY   0x24
#define KEY_P_KEY   0x4D

// Extended keys (prefixed 0xE0 on the wire) are returned by ps2_get_key() with bit 0x80 set.
// Arrow up/down also come extended on most keyboards, so accept both forms.
#define KEY_UP_EXT   (0x75 | 0x80)  // up arrow (extended)
#define KEY_DOWN_EXT (0x72 | 0x80)  // down arrow (extended)
#define KEY_LEFT    (0x6B | 0x80)   // left arrow  -> page up
#define KEY_RIGHT   (0x74 | 0x80)   // right arrow -> page down
#define KEY_PGDN    (0x7A | 0x80)   // page down
#define KEY_PGUP    (0x7D | 0x80)   // page up

#define SPEAKER_PIN 25
#define VERSION "ver 0.8.1b"

// Optional: blank VGA during flash writes. Default 0 = keep VGA + rare progress.
// Set -DOTA_SUSPEND_VGA_DURING_FLASH=1 if flash stage still sparkles.
#ifndef OTA_SUSPEND_VGA_DURING_FLASH
#define OTA_SUSPEND_VGA_DURING_FLASH 0
#endif

#define HRES  320
#define VRES  240

#define MENU_LINE_H   12
#define MAX_VISIBLE   13
#define MENU_Y_START  68
#define MAX_ENTRIES   35

// Colors
#define C_BLACK   Color::Black
#define C_RED     Color::Red
#define C_GREEN   Color::BrightGreen
#define C_YELLOW  Color::Yellow
#define C_BLUE    Color::Blue
#define C_MAGENTA Color::Magenta
#define C_CYAN    Color::Cyan
#define C_WHITE   Color::White

// PS2 ring buffer size
#define PS2_BUFFER_SIZE 64

// --------------------------------------------------------------------------
// Global state (defined in main.cpp)
// --------------------------------------------------------------------------
extern int statusY_original;
extern int statusY;
extern const uint32_t AUTOBOOT_MS;

extern fabgl::VGAController DisplayController;
extern fabgl::Canvas          cv;

extern volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
extern volatile int ps2_head, ps2_tail;
extern volatile int ps2_bit;
extern volatile uint8_t ps2_data;
extern portMUX_TYPE ps2Mux;

extern SPIClass spiSD;
extern Preferences prefs;
extern int sdMisoActive;

// Keyboard pins (runtime-configurable via bootl.rc / magic button) — defined in main.cpp
extern int ps2DatPin;
extern int ps2ClkPin;
extern int kbdConfigIdx;
extern int magicBtnPin;

struct MenuEntry {
    char name[64];
    char version[64];
    char path[128];
};
extern MenuEntry menuEntries[MAX_ENTRIES];
extern int menuCount;

// --------------------------------------------------------------------------
// System functions (defined in main.cpp)
// --------------------------------------------------------------------------
void ps2_isr();
void ps2_flush();
bool ps2_available();
void ps2_init();
uint8_t ps2_get_key();
void ps2_shutdown();
bool tryMountSD(int miso);
bool initSDAuto();
bool needsUpdate(const char* pathOnSD, const char* versionOnSD);
void saveVersion(const char* path, const char* version);
void shutdownForReboot();
void bootEmulatorDirect();
void bootEmulator();
bool doOTA(const char* binPath, const char* folderPath, const char* versionName);
void scanFolders();

// --------------------------------------------------------------------------
// UI + updater modules (order matters: render first, updater uses render)
// --------------------------------------------------------------------------
#include "rc.h"
#include "render.h"
#include "updater.h"
#include "extract.h"
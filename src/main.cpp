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
#define KEY_ESC     0x76
#define KEY_SPACE   0x29
#define KEY_F2      0x06
#define KEY_2       0x1E

#define SPEAKER_PIN 25
#define VERSION "ver 0.6.0a"

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

// statusY tracks the next status line; reset to menu area (not header)
int statusY_original = MENU_Y_START;
int statusY = MENU_Y_START;

const uint32_t AUTOBOOT_MS = 10000;

// ---------------------------------------------------------------------------
// FabGL globals
// ---------------------------------------------------------------------------
fabgl::VGA16Controller DisplayController;
fabgl::Canvas          cv(&DisplayController);

// ---------------------------------------------------------------------------
// PS2 por interrupção (ring buffer protegido ISR ↔ main)
// ---------------------------------------------------------------------------
#define PS2_BUFFER_SIZE 64
volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
volatile int ps2_head = 0, ps2_tail = 0;
volatile int ps2_bit = 0;
volatile uint8_t ps2_data = 0;
portMUX_TYPE ps2Mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR ps2_isr() {
    int dat = digitalRead(PS2_DAT);
    ps2_bit++;
    if (ps2_bit >= 2 && ps2_bit <= 9) {
        ps2_data >>= 1;
        if (dat) ps2_data |= 0x80;
    } else if (ps2_bit == 11) {
        portENTER_CRITICAL_ISR(&ps2Mux);
        int next = (ps2_head + 1) % PS2_BUFFER_SIZE;
        if (next != ps2_tail) {  // drop byte if ring is full
            ps2_buffer[ps2_head] = ps2_data;
            ps2_head = next;
        }
        portEXIT_CRITICAL_ISR(&ps2Mux);
        ps2_bit = 0;
        ps2_data = 0;
    } else if (ps2_bit > 11) {
        // Frame desync — resync on next falling edges
        ps2_bit = 0;
        ps2_data = 0;
    }
}

void ps2_flush() {
    portENTER_CRITICAL(&ps2Mux);
    ps2_head = ps2_tail = 0;
    ps2_bit = 0;
    ps2_data = 0;
    portEXIT_CRITICAL(&ps2Mux);
}

bool ps2_available() {
    portENTER_CRITICAL(&ps2Mux);
    bool has = (ps2_head != ps2_tail);
    portEXIT_CRITICAL(&ps2Mux);
    return has;
}

static uint8_t ps2_pop() {
    uint8_t code = 0;
    portENTER_CRITICAL(&ps2Mux);
    if (ps2_head != ps2_tail) {
        code = ps2_buffer[ps2_tail];
        ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;
    }
    portEXIT_CRITICAL(&ps2Mux);
    return code;
}

void ps2_init() {
    pinMode(PS2_DAT, INPUT_PULLUP);
    pinMode(PS2_CLK, INPUT_PULLUP);
    ps2_flush();
    attachInterrupt(digitalPinToInterrupt(PS2_CLK), ps2_isr, FALLING);
}

// Returns make scancode; 0 = break / ignore. Does not wipe the ring after each key.
uint8_t ps2_get_key() {
    while (true) {
        while (!ps2_available()) delay(1);
        uint8_t code = ps2_pop();
        if (code == 0) continue;
        if (code == 0xF0) {
            while (!ps2_available()) delay(1);
            ps2_pop();  // discard released key
            return 0;
        }
        if (code == 0xE0) {
            while (!ps2_available()) delay(1);
            code = ps2_pop();
            if (code == 0xF0) {
                while (!ps2_available()) delay(1);
                ps2_pop();
                return 0;
            }
            return code;
        }
        return code;
    }
}

void ps2_shutdown() {
    detachInterrupt(digitalPinToInterrupt(PS2_CLK));
    gpio_reset_pin(GPIO_NUM_32);
    gpio_reset_pin(GPIO_NUM_33);
    WRITE_PERI_REG(0x3ff48094, 0);
}

// ---------------------------------------------------------------------------
// Cores FabGL
// ---------------------------------------------------------------------------
#define C_BLACK   Color::Black
#define C_RED     Color::Red
#define C_GREEN   Color::BrightGreen
#define C_YELLOW  Color::Yellow
#define C_BLUE    Color::Blue
#define C_MAGENTA Color::Magenta
#define C_CYAN    Color::Cyan
#define C_WHITE   Color::White

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void drawString(int x, int y, const char* str, Color ink, Color paper) {
    cv.selectFont(&fabgl::FONT_6x12);
    cv.setPenColor(ink);
    cv.setBrushColor(paper);
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));
    cv.drawText( x, y, str);
}

void fillRect(int x, int y, int w, int h, Color color) {
    cv.setBrushColor(color);
    cv.fillRectangle(x, y, x + w - 1, y + h - 1);
}

void drawLine(int x1, int y1, int x2, Color color) {
    cv.setPenColor(color);
    cv.drawLine(x1, y1, x2, y1);
}

// Logo: logo_data is 1 byte/px packed --RRGGBB; FabGL Bitmap wants RGBA2222 = AABBGGRR
static uint8_t logo_rgba[LOGO_W * LOGO_H];
static fabgl::Bitmap* logoBitmap = nullptr;

void ensureLogoBitmap() {
    if (logoBitmap) return;
    for (int i = 0; i < LOGO_W * LOGO_H; i++) {
        uint8_t c = logo_data[i];
        uint8_t r = (c >> 4) & 3;
        uint8_t g = (c >> 2) & 3;
        uint8_t b = c & 3;
        logo_rgba[i] = 0xC0 | (b << 4) | (g << 2) | r;  // AA=3 (opaque)
    }
    logoBitmap = new fabgl::Bitmap(LOGO_W, LOGO_H, logo_rgba,
                                   fabgl::PixelFormat::RGBA2222, false);
}

void drawLogo(int x, int y) {
    ensureLogoBitmap();
    cv.drawBitmap(x, y, logoBitmap);
}

// ---------------------------------------------------------------------------
// Speaker
// ---------------------------------------------------------------------------
void speakerClick() {
    for (int i = 0; i < 50; i++) {
        dacWrite(SPEAKER_PIN, 40);
        delayMicroseconds(500);
        dacWrite(SPEAKER_PIN, 0);
        delayMicroseconds(500);
    }
    dacWrite(SPEAKER_PIN, 20);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
SPIClass spiSD(HSPI);
Preferences prefs;

// Active MISO after auto-detect (for status/logging)
int sdMisoActive = SD_MISO_LILYGO;

// Try one SD pin set (Arduino SD + HSPI). Cleans previous bus on failure.
bool tryMountSD(int miso) {
    SD.end();
    spiSD.end();
    delay(20);
    spiSD.begin(SD_CLK, miso, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spiSD)) {
        SD.end();
        spiSD.end();
        return false;
    }
    sdMisoActive = miso;
    return true;
}

// Auto-detect SD GPIOs from chip package (ported from ESPectrum FileUtils::initFileSystem)
// Package: 0=D0WDQ6, 1=D0WDQ5/WROVER, 5=PICOD4/TTGO — only MISO changes (2 vs 35)
bool initSDAuto() {
    uint32_t ver_pkg = esp_efuse_get_pkg_ver() & 0x7;
    int firstMiso;
    int secondMiso;

    switch (ver_pkg) {
        case 1:  // ESP32D0WDQ5 (WROVER) — Olimex-style MISO
            firstMiso = SD_MISO_OLIMEX;
            secondMiso = SD_MISO_LILYGO;
            Serial.printf("SD: pkg=%u (D0WDQ5/WROVER) try MISO=GPIO%d\n", ver_pkg, firstMiso);
            break;
        case 5:  // ESP32PICOD4 (TTGO VGA32)
            firstMiso = SD_MISO_LILYGO;
            secondMiso = SD_MISO_OLIMEX;
            Serial.printf("SD: pkg=%u (PICOD4/TTGO) try MISO=GPIO%d\n", ver_pkg, firstMiso);
            break;
        case 0:  // ESP32D0WDQ6
            firstMiso = SD_MISO_LILYGO;
            secondMiso = SD_MISO_OLIMEX;
            Serial.printf("SD: pkg=%u (D0WDQ6) try MISO=GPIO%d\n", ver_pkg, firstMiso);
            break;
        default:
            firstMiso = SD_MISO_LILYGO;
            secondMiso = SD_MISO_OLIMEX;
            Serial.printf("SD: pkg=%u (unknown) try MISO=GPIO%d\n", ver_pkg, firstMiso);
            break;
    }

    if (tryMountSD(firstMiso)) {
        Serial.printf("SD: mounted OK MISO=GPIO%d\n", sdMisoActive);
        return true;
    }

    Serial.printf("SD: mount failed, fallback MISO=GPIO%d\n", secondMiso);
    if (tryMountSD(secondMiso)) {
        Serial.printf("SD: mounted OK MISO=GPIO%d\n", sdMisoActive);
        return true;
    }

    Serial.println("SD: mount failed on both MISO pin sets");
    return false;
}



void statusLine(const char* label, const char* value, Color color) {
    char buf[64];
    if (label[0] == '\0')
        snprintf(buf, sizeof(buf), "%s", value);
    else
        snprintf(buf, sizeof(buf), "%s %s", label, value);

    drawString(10, statusY, buf, color, C_BLACK);
    Serial.printf("[%s] %s\n", label, value);
    statusY += 12;
}

void statusLine(const char* value, Color color) {
    statusLine("", value, color);
}

void drawProgress(int percent, size_t written, size_t total, const char* action = "Flashing") {
    char buf[50];
    snprintf(buf, sizeof(buf), "%s %3d%%  (%dKB/%dKB)  ",
             action, percent, (int)(written/1024), (int)(total/1024));
    drawString(20, statusY, buf, C_YELLOW, C_BLACK);
    int barW = (280 * percent) / 100;
    fillRect(20,        statusY + 13, barW,       6, C_GREEN);
    fillRect(20 + barW, statusY + 13, 280 - barW, 6, C_BLACK);
}


// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------
void drawHeader() {
    fillRect(0, 0, HRES, VRES, C_BLACK);
    drawLogo((HRES - LOGO_W) / 2, 4);
    drawString(130, 20, VERSION, C_WHITE, C_BLACK);
    drawString(38, 32, "by FG1998 - www.alternativebits.com/esp32", C_CYAN, C_BLACK);
    drawString(30, 44, "Q/UP - A/DOWN - ENTER", C_WHITE,   C_BLACK);    
    drawString(160, 44, "F1/1=Menu F2/2=Update", C_MAGENTA, C_BLACK);
    drawLine(8, 56, HRES - 9, C_BLUE);

}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
bool needsUpdate(const char* versionOnSD) {
    prefs.begin("sdloader", true);
    String stored = prefs.getString("version", "");
    prefs.end();
    Serial.printf("NVS: '%s'  SD: '%s'\n", stored.c_str(), versionOnSD);
    return (stored != String(versionOnSD));
}

void saveVersion(const char* version) {
    prefs.begin("sdloader", false);
    size_t written = prefs.putString("version", version);
    if (written == 0) { prefs.clear(); prefs.putString("version", version); }
    prefs.end();
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
// Stop VGA DMA/I2S and release shared peripherals before ESP.restart()
void shutdownForReboot() {
    Serial.println("Shutdown: PS2 / VGA / SD...");
    ps2_shutdown();
    DisplayController.end();
    delay(50);
    gpio_reset_pin(GPIO_NUM_25);
    SD.end();
    spiSD.end();
    spi_bus_free(HSPI_HOST);
    delay(100);
}

void bootEmulatorDirect() {
    Serial.println("Iniciando emulador (direto)...");
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    }
    delay(200);
    shutdownForReboot();
    ESP.restart();
}

void bootEmulator() {
    Serial.println("Iniciando emulador...");
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) esp_ota_set_boot_partition(ota0);
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
        Serial.println("otadata apagado");
    }
    delay(200);
    shutdownForReboot();
    ESP.restart();
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
// Update progress UI at most every 10% to avoid Canvas vs SD/flash bus contention
static void otaProgressMaybe(int& lastPct, size_t done, size_t total, const char* action) {
    if (total == 0) return;
    int pct = (int)((done * 100) / total);
    if (done < total && pct / 10 == lastPct / 10) return;
    lastPct = pct;
    drawProgress(pct, done, total, action);
}

bool doOTA(const char* binPath, const char* versionName) {
    File bin = SD.open(binPath);
    if (!bin) { statusLine("firmware.bin", "OPEN ERROR", C_RED); return false; }

    size_t fileSize = bin.size();
    if (fileSize == 0) {
        bin.close();
        statusLine("firmware.bin", "EMPTY", C_RED);
        return false;
    }

    char sizeStr[40];
    snprintf(sizeStr, sizeof(sizeStr), "FOUND %dKB", (int)(fileSize / 1024));
    statusLine("firmware.bin", sizeStr, C_GREEN);

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) {
        statusLine("OTA", "ota_0 NOT FOUND!", C_RED);
        bin.close();
        return false;
    }
    if (fileSize > ota0->size) {
        statusLine("OTA", "FILE TOO LARGE!", C_RED);
        bin.close();
        return false;
    }

    // Phase A: prefer full SD → PSRAM (separates SD burst from flash writes)
    uint8_t* fileBuf = nullptr;
    bool fromPsram = false;
    if (psramFound()) {
        fileBuf = (uint8_t*)ps_malloc(fileSize);
        if (fileBuf) {
            statusLine("OTA", "Loading SD -> PSRAM...", C_YELLOW);
            size_t loaded = 0;
            int lastPct = -1;
            const size_t RD = 64 * 1024;
            otaProgressMaybe(lastPct, 0, fileSize, "Reading SD");
            while (loaded < fileSize) {
                size_t want = min(RD, fileSize - loaded);
                size_t rd = bin.read(fileBuf + loaded, want);
                if (rd == 0) break;
                loaded += rd;
                otaProgressMaybe(lastPct, loaded, fileSize, "Reading SD");
            }
            bin.close();
            if (loaded != fileSize) {
                free(fileBuf);
                statusLine("OTA", "SD READ ERROR", C_RED);
                return false;
            }
            fromPsram = true;
            statusY += 20;  // leave room below the progress bar
            statusLine("OTA", "Flashing from PSRAM...", C_CYAN);
        }
    }
    if (!fromPsram) {
        statusLine("OTA", "Streaming from SD...", C_YELLOW);
    }

#if OTA_SUSPEND_VGA_DURING_FLASH
    // Stop VGA DMA during flash writes — screen goes black; progress on Serial only
    statusLine("OTA", "Blanking VGA for flash...", C_YELLOW);
    delay(80);
    DisplayController.end();
    const bool vgaSuspended = true;
#else
    const bool vgaSuspended = false;
#endif

    auto otaFailUi = [&](const char* msg) {
#if OTA_SUSPEND_VGA_DURING_FLASH
        if (vgaSuspended) {
            DisplayController.begin();
            DisplayController.setResolution(QVGA_320x240_60Hz);
            drawHeader();
            statusY = MENU_Y_START;
        }
#endif
        statusLine("OTA", msg, C_RED);
    };

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_begin err:%d", err);
        if (fileBuf) free(fileBuf);
        else bin.close();
        otaFailUi(msg);
        return false;
    }

    size_t written = 0;
    int lastPct = -1;
    if (!vgaSuspended) otaProgressMaybe(lastPct, 0, fileSize, "Flashing");

    if (fromPsram) {
        const size_t CHUNK = 64 * 1024;
        while (written < fileSize) {
            size_t n = min(CHUNK, fileSize - written);
            err = esp_ota_write(ota_handle, fileBuf + written, n);
            if (err != ESP_OK) {
                char msg[50];
                snprintf(msg, sizeof(msg), "ota_write err:%d", err);
                free(fileBuf);
                esp_ota_abort(ota_handle);
                otaFailUi(msg);
                return false;
            }
            written += n;
            if (vgaSuspended) {
                int pct = (int)((written * 100) / fileSize);
                if (pct / 10 != lastPct / 10 || written == fileSize) {
                    lastPct = pct;
                    Serial.printf("OTA flash %d%% (%u/%u)\n", pct, (unsigned)written, (unsigned)fileSize);
                }
            } else {
                otaProgressMaybe(lastPct, written, fileSize, "Flashing");
            }
        }
        free(fileBuf);
        fileBuf = nullptr;
    } else {
        const size_t CHUNK = 16 * 1024;
        uint8_t* buf = (uint8_t*)malloc(CHUNK);
        const size_t chunkSize = buf ? CHUNK : 4096;
        uint8_t stackBuf[4096];
        uint8_t* ptr = buf ? buf : stackBuf;

        while (written < fileSize) {
            size_t want = min(chunkSize, fileSize - written);
            size_t rd = bin.read(ptr, want);
            if (rd == 0) break;
            err = esp_ota_write(ota_handle, ptr, rd);
            if (err != ESP_OK) {
                char msg[50];
                snprintf(msg, sizeof(msg), "ota_write err:%d", err);
                if (buf) free(buf);
                bin.close();
                esp_ota_abort(ota_handle);
                otaFailUi(msg);
                return false;
            }
            written += rd;
            if (vgaSuspended) {
                int pct = (int)((written * 100) / fileSize);
                if (pct / 10 != lastPct / 10 || written == fileSize) {
                    lastPct = pct;
                    Serial.printf("OTA flash %d%% (%u/%u)\n", pct, (unsigned)written, (unsigned)fileSize);
                }
            } else {
                otaProgressMaybe(lastPct, written, fileSize, "Flashing");
            }
        }
        if (buf) free(buf);
        bin.close();
    }

    if (written != fileSize) {
        esp_ota_abort(ota_handle);
        otaFailUi("INCOMPLETE WRITE");
        return false;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_end err:%d", err);
        otaFailUi(msg);
        return false;
    }

#if OTA_SUSPEND_VGA_DURING_FLASH
    if (vgaSuspended) {
        DisplayController.begin();
        DisplayController.setResolution(QVGA_320x240_60Hz);
        drawHeader();
        statusY = MENU_Y_START;
        statusLine("OTA", "VGA restored", C_CYAN);
    }
#endif

    saveVersion(versionName);
    statusY += 20;
    statusLine("Status", "FLASHED OK!", C_GREEN);
    return true;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
struct MenuEntry {
    char name[64];
    char version[64];
    char path[128];
};

MenuEntry menuEntries[MAX_ENTRIES];
int menuCount = 0;

void showMaintenanceMenu() {
fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START + 12, C_BLACK);
drawString(10, MENU_Y_START + 14,      "*** MENU ***",      C_YELLOW, C_BLACK);
drawString(10, MENU_Y_START + 28, "N - Clear Bootloader NVS", C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 42, "A - Clear ALL NVS",        C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 56, "O - Clear Otadata",        C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 70, "R - Reset ESP32",          C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 84, "ESC/SPACE - Cancel",       C_CYAN,   C_BLACK);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        const char* msg  = nullptr;
        const char* done = nullptr;

        if      (key == KEY_N_KEY) { msg = "Clear Bootloader NVS?"; done = "Bootloader NVS cleared!"; }
        else if (key == KEY_A_KEY) { msg = "Clear ALL NVS?";         done = "ALL NVS cleared!"; }
        else if (key == KEY_O_KEY) { msg = "Clear Otadata?";         done = "Otadata cleared!"; }
        else if (key == KEY_R_KEY) { msg = "Reset ESP32?";           done = "Resetting..."; }
        else if (key == KEY_ESC || key == KEY_SPACE) { break; }

        if (!msg) continue;

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      msg,                         C_YELLOW, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Y to confirm, N to cancel", C_WHITE,  C_BLACK);

        uint8_t conf = 0;
        while (conf == 0) conf = ps2_get_key();

        if (conf != 0x35) {
            // N — volta pro menu
            fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
            drawString(10, MENU_Y_START,      "*** MAINTENANCE ***",      C_YELLOW, C_BLACK);
            drawString(10, MENU_Y_START + 14, "N - Clear Bootloader NVS", C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 24, "A - Clear ALL NVS",        C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 34, "O - Clear Otadata",        C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 44, "R - Reset ESP32",          C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 58, "ESC/SPACE - Cancel",       C_CYAN,   C_BLACK);
            continue;
        }

        if      (key == KEY_N_KEY) { prefs.begin("sdloader", false); prefs.clear(); prefs.end(); }
        else if (key == KEY_A_KEY) { nvs_flash_erase(); nvs_flash_init(); }
        else if (key == KEY_O_KEY) {
            const esp_partition_t* otadata = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
            if (otadata) esp_partition_erase_range(otadata, 0, otadata->size);
        }
        else if (key == KEY_R_KEY) {
            shutdownForReboot();
            ESP.restart();
        }

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      done,                        C_GREEN, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Press any key to reboot",   C_WHITE, C_BLACK);
        while (ps2_get_key() == 0);
        shutdownForReboot();
        ESP.restart();
    }
}

void scanFolders() {
    menuCount = 0;
    File root = SD.open("/");
    if (!root) return;
    while (menuCount < MAX_ENTRIES) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) { entry.close(); continue; }
        char folderPath[128];
        snprintf(folderPath, sizeof(folderPath), "/%s", entry.name());
        char binPath[140], verPath[140];
        snprintf(binPath, sizeof(binPath), "%s/firmware.bin", folderPath);
        snprintf(verPath, sizeof(verPath), "%s/version.txt", folderPath);
        if (SD.exists(binPath) && SD.exists(verPath)) {
            strncpy(menuEntries[menuCount].name, entry.name(), sizeof(menuEntries[0].name)-1);
            strncpy(menuEntries[menuCount].path, folderPath, sizeof(menuEntries[0].path)-1);
            File vf = SD.open(verPath);
            if (vf) {
                String v = vf.readStringUntil('\n'); v.trim();
                strncpy(menuEntries[menuCount].version, v.c_str(), sizeof(menuEntries[0].version)-1);
                vf.close();
            }
            Serial.printf("Menu: %s (%s)\n", menuEntries[menuCount].name, menuEntries[menuCount].version);
            menuCount++;
        }
        entry.close();
    }
    qsort(menuEntries, menuCount, sizeof(MenuEntry), [](const void* a, const void* b) {
        return strcasecmp(((MenuEntry*)a)->name, ((MenuEntry*)b)->name);
    });
    root.close();
}

static const int MENU_BAR_W = 150;
static const int MENU_BAR_X = (HRES - MENU_BAR_W) / 2;
static const int MENU_COUNTER_CLEAR_W = 8 * 6;  // room for "99/99"

void drawMenuCounter(int selected) {
    fillRect(HRES - MENU_COUNTER_CLEAR_W - 4, 32, MENU_COUNTER_CLEAR_W, MENU_LINE_H, C_BLACK);
    char sc[16];
    snprintf(sc, sizeof(sc), "%d/%d", selected + 1, menuCount);
    drawString(HRES - (int)strlen(sc) * 6 - 4, 32, sc, C_CYAN, C_BLACK);
}

void drawMenuRow(int idx, int selected, int scrollOffset, const String& currentVersion) {
    int i = idx - scrollOffset;
    if (i < 0 || i >= MAX_VISIBLE || idx < 0 || idx >= menuCount) return;

    bool isSel = (idx == selected);
    Color ink   = isSel ? C_BLACK : C_GREEN;
    Color paper = isSel ? C_GREEN : C_BLACK;
    bool isInstalled = (String(menuEntries[idx].version) == currentVersion);

    char line[42];
    snprintf(line, sizeof(line), "%s%s", isInstalled ? "*" : "", menuEntries[idx].name);

    int y = MENU_Y_START + i * MENU_LINE_H;
    fillRect(MENU_BAR_X, y, MENU_BAR_W, MENU_LINE_H, paper);

    int textW = (int)strlen(line) * 6;
    int textX = MENU_BAR_X + (MENU_BAR_W - textW) / 2;
    cv.selectFont(&fabgl::FONT_6x12);
    drawString(textX, y, line, ink, paper);
}

void drawMenu(int selected, int scrollOffset, const String& currentVersion) {
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawMenuCounter(selected);

    int visible = min(menuCount, MAX_VISIBLE);
    for (int i = 0; i < visible; i++) {
        int idx = i + scrollOffset;
        if (idx >= menuCount) break;
        drawMenuRow(idx, selected, scrollOffset, currentVersion);
    }
}

// Incremental update: only old/new rows when scroll unchanged; full redraw on scroll
void updateMenuSelection(int oldSelected, int newSelected,
                         int oldScroll, int newScroll,
                         const String& currentVersion) {
    if (oldScroll != newScroll) {
        drawMenu(newSelected, newScroll, currentVersion);
        return;
    }
    if (oldSelected != newSelected) {
        drawMenuRow(oldSelected, newSelected, newScroll, currentVersion);
        drawMenuRow(newSelected, newSelected, newScroll, currentVersion);
    }
    drawMenuCounter(newSelected);
}




#include "updater.h"

int runMenu() {

    cv.setPenColor(C_WHITE);
    cv.setBrushColor(C_BLACK);
    cv.selectFont(&fabgl::FONT_6x12);

    if (menuCount == 0) return -1;

    prefs.begin("sdloader", true);
    String currentVersion = prefs.getString("version", "");
    prefs.end();

    int selected = 0;
    for (int i = 0; i < menuCount; i++) {
        if (String(menuEntries[i].version) == currentVersion) { selected = i; break; }
    }

    int scrollOffset = 0;
    if (selected >= MAX_VISIBLE) scrollOffset = selected - MAX_VISIBLE + 1;

    ps2_init();
    delay(500);
    ps2_flush();  // drop noise from attach

    drawMenu(selected, scrollOffset, currentVersion);

    bool hasInstalled = (currentVersion.length() > 0);
    uint32_t autobootStart = millis();
    int lastSecsLeft = -1;
    // FONT_6x12: worst case "10s" = 3 chars; clear a 4-char slot to avoid ghosts
    const int countdownClearW = 4 * 6;
    const int countdownY = MENU_Y_START - 20;
    const int countdownClearX = HRES - countdownClearW - 4;

    while (true) {

        if (hasInstalled) {
            uint32_t elapsed = millis() - autobootStart;
            if (elapsed >= AUTOBOOT_MS) return selected;
            int secsLeft = (AUTOBOOT_MS - elapsed) / 1000 + 1;
            // Only redraw when the second changes — avoids flooding FabGL queue
            if (secsLeft != lastSecsLeft) {
                lastSecsLeft = secsLeft;
                char countdown[16];
                snprintf(countdown, sizeof(countdown), "%ds", secsLeft);
                int textW = (int)strlen(countdown) * 6;
                int textX = HRES - textW - 4;
                fillRect(countdownClearX, countdownY, countdownClearW, MENU_LINE_H, C_BLACK);
                drawString(textX, countdownY, countdown, C_YELLOW, C_BLACK);
            }
        }

        if (!ps2_available()) {
            delay(1);
            continue;
        }

        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        // Do not wipe the ring here — that raced the ISR and dropped make codes

        if (hasInstalled) {
            hasInstalled = false;
            fillRect(countdownClearX, countdownY, countdownClearW, MENU_LINE_H, C_BLACK);
        }

        speakerClick();
        Serial.printf("Key: 0x%02X\n", key);

        if (key == KEY_F1 || key == KEY_1) {
            showMaintenanceMenu();
            ps2_flush();
            drawMenu(selected, scrollOffset, currentVersion);
            continue;
        }
        if (key == KEY_F2 || key == KEY_2) {
            runUpdater();
            ps2_flush();
            statusLine("Status", "Press any key to return", C_YELLOW);
            while (ps2_get_key() == 0);
            drawMenu(selected, scrollOffset, currentVersion);
            statusY = statusY_original;
            continue;
        }
        if ((key == KEY_UP || key == KEY_Q) && selected > 0) {
            int oldSelected = selected;
            int oldScroll = scrollOffset;
            selected--;
            if (selected < scrollOffset) scrollOffset--;
            updateMenuSelection(oldSelected, selected, oldScroll, scrollOffset, currentVersion);
        } else if ((key == KEY_DOWN || key == KEY_A_NAV) && selected < menuCount - 1) {
            int oldSelected = selected;
            int oldScroll = scrollOffset;
            selected++;
            if (selected >= scrollOffset + MAX_VISIBLE) scrollOffset++;
            updateMenuSelection(oldSelected, selected, oldScroll, scrollOffset, currentVersion);
        } else if (key == KEY_ENTER) {
            return selected;
        }
    }
}






// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {

    disableCore0WDT();
    delay(100);
    disableCore1WDT();
    // 512: room for UI primitives without the RAM cost of FabGL default (often 1024+)
    // Logo is now 1x drawBitmap; menu navigation is incremental — 128 was too tight before
    fabgl::BitmappedDisplayController::queueSize = 512;

    
    Serial.begin(115200);
    delay(200);

    Serial.println("Iniciando VGA...");
    DisplayController.begin();
    DisplayController.setResolution(QVGA_320x240_60Hz);
   
    cv.selectFont(&fabgl::FONT_7x14);
    Serial.println("VGA OK");

    drawHeader();
   
    delay(300);

    Serial.printf("Heap: %d  PSRAM: %s\n", ESP.getFreeHeap(), psramFound() ? "SIM" : "NAO");

    if (!initSDAuto()) {
        statusLine("SD Card not found", C_RED);
        statusLine("Press any key to retry", C_YELLOW);
        ps2_init();
        while (ps2_get_key() == 0);
        shutdownForReboot();
        ESP.restart();
        return;
    }

    char sdMsg[40];
    snprintf(sdMsg, sizeof(sdMsg), "FOUND (MISO=GPIO%d)", sdMisoActive);
    statusLine("SD Card", sdMsg, C_GREEN);



    // --- Modo 1: firmware.bin + version.txt na raiz ---
    if (SD.exists(FIRMWARE_FILE) && SD.exists(VERSION_FILE)) {
        char versionName[64] = "firmware_sem_versao";
        File vf = SD.open(VERSION_FILE);
        if (vf) {
            String v = vf.readStringUntil('\n'); v.trim();
            strncpy(versionName, v.c_str(), sizeof(versionName)-1);
            vf.close();
        }
        statusLine("version.txt", versionName, C_CYAN);
        if (!needsUpdate(versionName)) {
            statusLine("Status", "Firmware OK - Starting...", C_GREEN);
            delay(1000); SD.end(); bootEmulatorDirect(); return;
        }
        statusLine("Status", "New firmware found!", C_YELLOW);
        bool ok = doOTA(FIRMWARE_FILE, versionName);
        SD.end();
        if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
        else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }
        return;
    }

    // --- Modo 2: menu ---
    statusLine("Scanning folders...", C_YELLOW);
    scanFolders();

    if (menuCount == 0) {
        statusLine("Status", "No emulators found!", C_RED);
        
        ps2_init();
        delay(500);
        ps2_flush();

        while (true) {
            if (!ps2_available()) {
                delay(1);
                continue;
            }
            uint8_t key = ps2_get_key();
            if (key == 0) continue;
            if (key == KEY_F1 || key == KEY_1) {
                showMaintenanceMenu();
                ps2_flush();
                fillRect(0, 0, HRES, VRES, C_BLACK);
                drawHeader();
            } else if (key == KEY_F2 || key == KEY_2) {
                runUpdater();
                ps2_flush();
            }
        }
    }

    int selected = runMenu();
    if (selected < 0) {
        delay(3000);
        shutdownForReboot();
        ESP.restart();
        return;
    }

    fillRect(0, MENU_Y_START - 23, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("Selected", menuEntries[selected].name, C_GREEN);

    char binPath[140];
    snprintf(binPath, sizeof(binPath), "%s/firmware.bin", menuEntries[selected].path);
    const char* versionName = menuEntries[selected].version;

    if (!needsUpdate(versionName)) {
        statusLine("Status", "Firmware OK - Starting...", C_GREEN);
        delay(1000); SD.end(); bootEmulatorDirect(); return;
    }

    statusLine("Status", "New firmware found!", C_YELLOW);
    bool ok = doOTA(binPath, versionName);
    SD.end();

    if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
    else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }



    
    
}

void loop() {

}
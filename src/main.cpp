// ===========================================================================
// main.cpp - ESP32 SD Bootloader / Multiloader
// All modules pulled in through a single include below.
// ===========================================================================
#include "bootloader.h"

// --------------------------------------------------------------------------
// Global state definitions (declared extern in bootloader.h)
// --------------------------------------------------------------------------
int statusY_original = MENU_Y_START;
int statusY = MENU_Y_START;
const uint32_t AUTOBOOT_MS = 10000;

fabgl::VGA16Controller DisplayController;
fabgl::Canvas          cv(&DisplayController);

volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
volatile int ps2_head = 0, ps2_tail = 0;
volatile int ps2_bit = 0;
volatile uint8_t ps2_data = 0;
portMUX_TYPE ps2Mux = portMUX_INITIALIZER_UNLOCKED;

SPIClass spiSD(HSPI);
Preferences prefs;
int sdMisoActive = SD_MISO_LILYGO;

MenuEntry menuEntries[MAX_ENTRIES];
int menuCount = 0;

// ===========================================================================
// PS2 implementation
// ===========================================================================
void IRAM_ATTR ps2_isr() {
    // Resync by timing: PS/2 bits arrive ~60-120us apart. A gap longer than
    // ~250us means a clock pulse was lost or a new frame is starting, so we
    // restart the bit counter. Without this, one missed clock desyncs the
    // frame forever and every scancode comes out garbled (e.g. 0xC0 / 0xEA).
    static uint32_t lastEdgeUs = 0;
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastEdgeUs) > 250) {
        ps2_bit = 0;
        ps2_data = 0;
    }
    lastEdgeUs = nowUs;

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

// Waits up to timeoutMs for the next byte in a multi-byte sequence.
// Returns true if a byte became available (does NOT pop it), false on timeout.
static bool ps2_wait_next(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!ps2_available()) {
        if (millis() - start >= timeoutMs) return false;  // second byte never arrived
        delay(1);
    }
    return true;
}

// Returns make scancode; 0 = break / ignore. Does not wipe the ring after each key.
// Multi-byte tails (0xF0 break, 0xE0 extended) use a bounded wait so a lost or
// delayed second byte can never lock the menu forever.
uint8_t ps2_get_key() {
    const uint32_t TAIL_TIMEOUT_MS = 20;  // enough for a valid pair, short enough to never hang
    while (true) {
        while (!ps2_available()) delay(1);   // idle wait for a first byte (blocking is fine)
        uint8_t code = ps2_pop();
        if (code == 0) continue;
        if (code == 0xF0) {
            if (!ps2_wait_next(TAIL_TIMEOUT_MS)) return 0;  // orphan break: drop, don't hang
            ps2_pop();  // discard released key
            return 0;
        }
        if (code == 0xE0) {
            if (!ps2_wait_next(TAIL_TIMEOUT_MS)) return 0;  // orphan extended prefix: drop
            code = ps2_pop();
            if (code == 0xF0) {
                if (!ps2_wait_next(TAIL_TIMEOUT_MS)) return 0;
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

bool needsUpdate(const char* pathOnSD, const char* versionOnSD) {
    prefs.begin("sdloader", true);
    String storedPath = prefs.getString("path", "");
    String storedVer  = prefs.getString("version", "");
    prefs.end();
    Serial.printf("NVS path:'%s' ver:'%s'  SD path:'%s' ver:'%s'\n",
                  storedPath.c_str(), storedVer.c_str(), pathOnSD, versionOnSD);
    // Precisa gravar se mudou de emulador (path) OU se a versao do mesmo subiu
    return (storedPath != String(pathOnSD)) || (storedVer != String(versionOnSD));
}

void saveVersion(const char* path, const char* version) {
    prefs.begin("sdloader", false);
    prefs.putString("path", path);
    size_t written = prefs.putString("version", version);
    if (written == 0) { prefs.clear(); prefs.putString("path", path); prefs.putString("version", version); }
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

bool doOTA(const char* binPath, const char* folderPath, const char* versionName) {
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

    saveVersion(folderPath, versionName);
    statusY += 20;
    statusLine("Status", "FLASHED OK!", C_GREEN);
    return true;
}

// ---------------------------------------------------------------------------
// Menu

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
        if (!needsUpdate("/", versionName)) {
            statusLine("Status", "Firmware OK - Starting...", C_GREEN);
            delay(1000); SD.end(); bootEmulatorDirect(); return;
        }
        statusLine("Status", "New firmware found!", C_YELLOW);
        bool ok = doOTA(FIRMWARE_FILE, "/", versionName);
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
    const char* folderPath  = menuEntries[selected].path;

    if (!needsUpdate(folderPath, versionName)) {
        statusLine("Status", "Firmware OK - Starting...", C_GREEN);
        delay(1000); SD.end(); bootEmulatorDirect(); return;
    }

    statusLine("Status", "New firmware found!", C_YELLOW);
    bool ok = doOTA(binPath, folderPath, versionName);
    SD.end();

    if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
    else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }



    
    
}

void loop() {

}
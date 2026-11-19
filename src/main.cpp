// ===========================================================================
// main.cpp - ESP32 SD Bootloader / Multiloader
// All modules pulled in through a single include below.
// ===========================================================================
#include "bootloader.h"
#include "keymap.h"
#include "ps2_tx.h"
#include <Ps2Host.h>

// --------------------------------------------------------------------------
// Global state definitions (declared extern in bootloader.h)
// --------------------------------------------------------------------------
int statusY_original = MENU_Y_START;
int statusY = MENU_Y_START;
const uint32_t AUTOBOOT_MS = 10000;

//fabgl::VGA16Controller DisplayController;
fabgl::VGAController DisplayController;
fabgl::Canvas          cv(&DisplayController);

// --- Porta 1 (teclado principal — pinos vem do bootl.rc) ---
volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
volatile int ps2_head = 0, ps2_tail = 0;
volatile int ps2_bit = 0;
volatile uint8_t ps2_data = 0;
portMUX_TYPE ps2Mux = portMUX_INITIALIZER_UNLOCKED;

// --- Porta 2 (mouse PS/2 da VGA32: GPIO26 CLK / GPIO27 DAT) ---
volatile uint8_t ps2_buffer2[PS2_BUFFER_SIZE];
volatile int ps2_head2 = 0, ps2_tail2 = 0;
volatile int ps2_bit2 = 0;
volatile uint8_t ps2_data2 = 0;
portMUX_TYPE ps2Mux2 = portMUX_INITIALIZER_UNLOCKED;
const int ps2DatPin2 = 27;
const int ps2ClkPin2 = 26;

// T-Display esta no DIN do teclado: DAT=GPIO32 CLK=GPIO33.
const int ps2TdispDat = 32;
const int ps2TdispClk = 33;
static Ps2Host ps2host(ps2TdispClk, ps2TdispDat);

// Pinos PS/2 e config de teclado — definidos em runtime por loadRC() após SD montar.
// Valores iniciais C1 (padrão) para caso o SD falhe antes do loadRC().
int ps2DatPin    = 32;
int ps2ClkPin    = 33;
int kbdConfigIdx = 0;
int magicBtnPin  = 36;  // default; sobrescrito pelo loadRC() via magicb= no bootl.rc
int autoStartTime = 10;  // segundos da janela de autoboot (default); lido de autostarttime= no bootl.rc
String blInstalledVersion = "";  // lido do bootl.rc (version=); default BL_VERSION se ausente
int blAccepted = 0;              // lido do bootl.rc (accepted=); 1 = disclaimer aceito

SPIClass spiSD(HSPI);
Preferences prefs;
int sdMisoActive = SD_MISO_LILYGO;

MenuEntry menuEntries[MAX_ENTRIES];
int menuCount = 0;

// ===========================================================================
// PS2 implementation — dual port
// ===========================================================================
// Resync explanation (vale pras duas ISRs):
//   PS/2 bits chegam ~60-120us apart. Um gap > 250us significa clock perdido
//   ou novo frame comecando — restart do bit counter. Sem isso, um clock
//   perdido dessincroniza pra sempre e todo scancode sai corrompido.

void IRAM_ATTR ps2_isr() {
    static uint32_t lastEdgeUs = 0;
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastEdgeUs) > 250) {
        ps2_bit = 0;
        ps2_data = 0;
    }
    lastEdgeUs = nowUs;

    int dat = digitalRead(ps2DatPin);
    ps2_bit++;
    if (ps2_bit >= 2 && ps2_bit <= 9) {
        ps2_data >>= 1;
        if (dat) ps2_data |= 0x80;
    } else if (ps2_bit == 11) {
        portENTER_CRITICAL_ISR(&ps2Mux);
        int next = (ps2_head + 1) % PS2_BUFFER_SIZE;
        if (next != ps2_tail) {
            ps2_buffer[ps2_head] = ps2_data;
            ps2_head = next;
        }
        portEXIT_CRITICAL_ISR(&ps2Mux);
        ps2_bit = 0;
        ps2_data = 0;
    } else if (ps2_bit > 11) {
        ps2_bit = 0;
        ps2_data = 0;
    }
}

void IRAM_ATTR ps2_isr2() {
    static uint32_t lastEdgeUs = 0;
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastEdgeUs) > 250) {
        ps2_bit2 = 0;
        ps2_data2 = 0;
    }
    lastEdgeUs = nowUs;

    int dat = digitalRead(ps2DatPin2);
    ps2_bit2++;
    if (ps2_bit2 >= 2 && ps2_bit2 <= 9) {
        ps2_data2 >>= 1;
        if (dat) ps2_data2 |= 0x80;
    } else if (ps2_bit2 == 11) {
        portENTER_CRITICAL_ISR(&ps2Mux2);
        int next = (ps2_head2 + 1) % PS2_BUFFER_SIZE;
        if (next != ps2_tail2) {
            ps2_buffer2[ps2_head2] = ps2_data2;
            ps2_head2 = next;
        }
        portEXIT_CRITICAL_ISR(&ps2Mux2);
        ps2_bit2 = 0;
        ps2_data2 = 0;
    } else if (ps2_bit2 > 11) {
        ps2_bit2 = 0;
        ps2_data2 = 0;
    }
}

// ---- Helpers por porta ----
static bool p1_available() {
    portENTER_CRITICAL(&ps2Mux);
    bool has = (ps2_head != ps2_tail);
    portEXIT_CRITICAL(&ps2Mux);
    return has;
}
static bool p2_available() {
    return ps2host.available() > 0;
}
static uint8_t p1_pop() {
    uint8_t code = 0;
    portENTER_CRITICAL(&ps2Mux);
    if (ps2_head != ps2_tail) {
        code = ps2_buffer[ps2_tail];
        ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;
    }
    portEXIT_CRITICAL(&ps2Mux);
    return code;
}
static uint8_t p2_pop() {
    int b = ps2host.read();
    return b < 0 ? 0 : (uint8_t)b;
}

void ps2_flush() {
    portENTER_CRITICAL(&ps2Mux);
    ps2_head = ps2_tail = 0;
    ps2_bit = 0;
    ps2_data = 0;
    portEXIT_CRITICAL(&ps2Mux);
    ps2host.flushRx();
}

bool ps2_available() {
    return p1_available() || p2_available();
}

void ps2_init() {
    // Nao anexar a ISR do teclado no mesmo CLK do T-Display.
    if (ps2ClkPin != ps2TdispClk) {
        pinMode(ps2DatPin, INPUT_PULLUP);
        pinMode(ps2ClkPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(ps2ClkPin), ps2_isr, FALLING);
    }
    ps2_flush();
    ps2host.begin();
}

bool ps2host_send_bytes(const uint8_t* data, int n) {
    const uint32_t tx0 = ps2host.stats().txBytes;
    const uint32_t fail0 = ps2host.stats().txFail;
    for (int i = 0; i < n; i++) {
        uint32_t t = millis();
        while (!ps2host.write(data[i])) {
            if (millis() - t > 300) {
                Serial.printf("[PS2TX] fila cheia no byte %d\n", i);
                return false;
            }
            delay(1);
        }
    }
    uint32_t t = millis();
    while (true) {
        const uint32_t done =
            (ps2host.stats().txBytes - tx0) + (ps2host.stats().txFail - fail0);
        if (done >= (uint32_t)n) break;
        if (millis() - t > 3000) {
            Serial.println("[PS2TX] timeout esperando TX");
            return false;
        }
        delay(1);
    }
    const uint32_t sent = ps2host.stats().txBytes - tx0;
    const uint32_t fails = ps2host.stats().txFail - fail0;
    Serial.printf("[PS2TX] enviados=%u falhas=%u\n", sent, fails);
    return fails == 0 && sent >= (uint32_t)n;
}

// Espera o proximo byte DA MESMA PORTA (crucial — nao pode cruzar com o
// fluxo da outra porta, senao sequencias E0 / F0 ficam corrompidas).
static bool wait_next_port(int port, uint32_t timeoutMs) {
    uint32_t start = millis();
    while (true) {
        if (port == 1 ? p1_available() : p2_available()) return true;
        if (millis() - start >= timeoutMs) return false;
        delay(1);
    }
}

// Processa 1 scancode completo a partir da porta indicada. Retorna:
//   make scancode (bit 0x80 setado para teclas estendidas tipo setas)
//   ou 0 em break / sequencia incompleta / byte espurio.
static uint8_t read_key_from(int port) {
    const uint32_t TAIL_TIMEOUT_MS = 20;
    uint8_t code = (port == 1) ? p1_pop() : p2_pop();
    if (code == 0) return 0;

    if (code == 0xF0) {
        if (!wait_next_port(port, TAIL_TIMEOUT_MS)) return 0;
        (port == 1) ? p1_pop() : p2_pop();
        return 0;
    }
    if (code == 0xE0) {
        if (!wait_next_port(port, TAIL_TIMEOUT_MS)) return 0;
        code = (port == 1) ? p1_pop() : p2_pop();
        if (code == 0xF0) {
            if (!wait_next_port(port, TAIL_TIMEOUT_MS)) return 0;
            (port == 1) ? p1_pop() : p2_pop();
            return 0;
        }
        return code | 0x80;
    }
    return code;
}

// Drena qualquer uma das duas portas. Uma vez iniciada a leitura de uma
// porta, read_key_from() conclui a sequencia nela antes de olhar a outra —
// isso impede que E0/F0 de uma porta sejam "preenchidos" pela outra.
uint8_t ps2_get_key() {
    while (true) {
        while (!p1_available() && !p2_available()) delay(1);
        uint8_t k = p1_available() ? read_key_from(1) : read_key_from(2);
        if (k != 0) return k;
        // k == 0 => era break ou lixo; volta pro loop
    }
}

void ps2_shutdown() {
    detachInterrupt(digitalPinToInterrupt(ps2ClkPin));
    detachInterrupt(digitalPinToInterrupt(ps2ClkPin2));
    gpio_reset_pin((gpio_num_t)ps2DatPin);
    gpio_reset_pin((gpio_num_t)ps2ClkPin);
    gpio_reset_pin((gpio_num_t)ps2DatPin2);
    gpio_reset_pin((gpio_num_t)ps2ClkPin2);
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
// Reads /<folderPath>/keymap.ini and sends it to the T-Display on port 2 before
// booting the emulator. Must run while the SD is still mounted. If the file is
// missing or the send fails, the T-Display keeps its built-in default map.
//
// VGA is stopped first so display DMA does not jitter the PS/2 clocks.
// The screen going black here is fine — we're about to boot the emulator anyway.
void sendKeymapForBoot(const char* folderPath) {
    Serial.printf("[KEYMAP] sendKeymapForBoot('%s')\n", folderPath);

    Keymap km;
    if (!loadKeymap(folderPath, km)) {
        // No keymap.ini: send the default mapping + emulator name.
        km.loadDefaults();
        const char* base = folderPath;
        while (*base == '/') base++;                 // strip leading slash(es)
        const char* slash = strrchr(base, '/');      // in case of a nested path
        if (slash) base = slash + 1;
        strncpy(km.name, base, sizeof(km.name) - 1);
        km.name[sizeof(km.name) - 1] = '\0';
        Serial.printf("[KEYMAP] no ini -> sending defaults + name ('%s')\n", km.name);
    }

    // Log what was parsed, before touching any hardware.
    Serial.println("[KEYMAP] parsed buttons:");
    for (int i = 0; i < GP_BUTTON_COUNT; i++) {
        if (km.keys[i][0]) Serial.printf("   %s = %s\n", GP_BUTTONS[i].iniName, km.keys[i]);
    }
    Serial.flush();

    // Stop the VGA to remove display-DMA jitter during the send.
    Serial.println("[KEYMAP] stopping VGA");
    Serial.flush();
    DisplayController.end();
    delay(20);

    Serial.println("[KEYMAP] sending packet...");
    Serial.flush();
    bool sent = ps2_send_keymap(km);   // best-effort; has its own timeouts
    Serial.printf("[KEYMAP] send result: %s\n", sent ? "OK" : "FAILED");
    Serial.flush();

    // No need to restore the ISR/VGA — the caller boots the emulator next.
}

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

    // Stream straight from SD into ota_0, one chunk at a time. No full PSRAM copy:
    // we read a block from the card and write it to flash immediately. This skips
    // the SD->PSRAM pre-load, so switching emulators is noticeably faster and uses
    // almost no RAM. (The PSRAM pre-load existed to avoid VGA flicker, but there's
    // no WiFi contention here and flash writes don't fight the SD bus badly.)
    statusLine("OTA", "Flashing from SD...", C_YELLOW);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_begin err:%d", err);
        bin.close();
        statusLine("OTA", msg, C_RED);
        return false;
    }

    size_t written = 0;
    int lastPct = -1;
    otaProgressMaybe(lastPct, 0, fileSize, "Flashing");

    const size_t CHUNK = 32 * 1024;
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
            statusLine("OTA", msg, C_RED);
            return false;
        }
        written += rd;
        otaProgressMaybe(lastPct, written, fileSize, "Flashing");
    }
    if (buf) free(buf);
    bin.close();

    if (written != fileSize) {
        esp_ota_abort(ota_handle);
        statusLine("OTA", "INCOMPLETE WRITE", C_RED);
        return false;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_end err:%d", err);
        statusLine("OTA", msg, C_RED);
        return false;
    }

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

        // entry.name() may be "BOOTLOADER" or "/BOOTLOADER" — take basename and skip it.
        const char* ename = entry.name();
        const char* bn = strrchr(ename, '/');
        bn = bn ? bn + 1 : ename;
        Serial.printf("scanFolders[v4]: entry='%s' basename='%s'\n", ename, bn);
        if (strncasecmp(bn, "BOOTLOADER", 10) == 0) {
            Serial.println("  >>> SKIP BOOTLOADER <<<");
            entry.close();
            continue;
        }

        char folderPath[128];
        snprintf(folderPath, sizeof(folderPath), "/%s", bn);
        char binPath[140], verPath[140];
        snprintf(binPath, sizeof(binPath), "%s/firmware.bin", folderPath);
        snprintf(verPath, sizeof(verPath), "%s/version.txt", folderPath);
        if (SD.exists(binPath) && SD.exists(verPath)) {
            strncpy(menuEntries[menuCount].name, bn, sizeof(menuEntries[0].name)-1);
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

// True if ota_0 holds a valid ESP32 app image (first byte of the image is 0xE9).
static bool ota0HasValidApp() {
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) return false;
    uint8_t magic = 0;
    if (esp_partition_read(ota0, 0, &magic, 1) != ESP_OK) return false;
    return (magic == 0xE9);
}

// Shows the autoboot screen with gamepad image and keymap labels.
// Returns:  1 = ESC pressed (open the menu)
//           0 = timeout or any other key (boot the installed emulator)
// The emulator path is read from NVS so we can load its keymap and
// display the button mappings around the gamepad artwork.
static int autobootWindow(uint32_t windowMs) {
    // Load keymap for the installed emulator so we can show button labels.
    Keymap km;
    km.clear();
    prefs.begin("sdloader", true);
    String bootPath = prefs.getString("path", "");
    prefs.end();
    if (bootPath.length() > 0 && bootPath != "/") {
        if (!loadKeymap(bootPath.c_str(), km)) {
            // No keymap.ini — use default mapping so buttons show on screen
            km.loadDefaults();
            // Set emulator name from folder
            const char* base = bootPath.c_str();
            while (*base == '/') base++;
            const char* slash = strrchr(base, '/');
            if (slash) base = slash + 1;
            strncpy(km.name, base, sizeof(km.name) - 1);
            km.name[sizeof(km.name) - 1] = '\0';
        }
    } else {
        // No emulator path in NVS — still show default mapping
        km.loadDefaults();
    }

    // Draw the full autoboot screen: logo, emulator name, gamepad, labels, hint
    drawAutobootScreen(km.valid ? &km : nullptr);

    // Countdown loop: ESC = menu, any other key = boot now, timeout = boot
    ps2_flush();
    uint32_t start = millis();
    int lastSec = -1;
    while (millis() - start < windowMs) {
        if (ps2_available()) {
            uint8_t key = ps2_get_key();
            if (key == KEY_ESC) return 1;   // ESC -> open menu
            if (key != 0) return 0;         // any other key -> boot immediately
        }
        int sec = (int)((windowMs - (millis() - start)) / 1000) + 1;
        if (sec != lastSec) {
            lastSec = sec;
            drawAutobootCountdown(sec);
        }
        delay(10);
    }
    return 0;   // timeout -> boot
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

    // Lê /bootl.rc e define ps2DatPin / ps2ClkPin / kbdConfigIdx
    loadRC();
    drawKbdBadge();   // redraw the badge now that loadRC() set the real kbdConfigIdx

    // Bring the keyboard up with the loaded C1/C2/C3 pins RIGHT NOW, so the user can
    // already press Y on the disclaimer even when the keyboard isn't on the C1 pins.
    ps2_init();
    delay(200);
    ps2_flush();

    {
        char kbdMsg[40];
        snprintf(kbdMsg, sizeof(kbdMsg), "DAT=GPIO%d CLK=GPIO%d", ps2DatPin, ps2ClkPin);
        statusLine(KBD_CONFIGS[kbdConfigIdx].badge, kbdMsg, C_CYAN);
    }

    // Configura magic button (lido do bootl.rc; GPIO36 INPUT-ONLY, pull-up externo obrigatório)
    pinMode(magicBtnPin, INPUT);

    // First-run legal disclaimer — shows until the user accepts (accepted=1 in bootl.rc).
    showDisclaimerIfNeeded();

    // First-run WiFi setup — if /wificonfig.rc is missing, scan networks and let the
    // user pick one + type the password, then save it. Needs PS/2 ready.
    ps2_init();
    delay(200);
    ps2_flush();
    wifiSetupIfNeeded();

    // --- Autoboot: if something is already flashed in ota_0, boot it directly.
    //     ESC = open menu. Any other key or timeout = boot the emulator. ---
    if (ota0HasValidApp()) {
        int abResult = autobootWindow((uint32_t)autoStartTime * 1000);
        if (abResult == 0) {
            // Timeout or non-ESC key -> boot the installed emulator
            Serial.println("Autoboot: launching installed emulator");
            prefs.begin("sdloader", true);
            String bootPath = prefs.getString("path", "");
            prefs.end();
            Serial.printf("Autoboot: last emulator path = '%s'\n", bootPath.c_str());
            if (bootPath.length() > 0 && bootPath != "/")
                sendKeymapForBoot(bootPath.c_str());   // SD still mounted here
            SD.end();
            bootEmulatorDirect();   // boots ota_0, does not return
            return;
        }
        // ESC pressed -> fall through to the menu
        fillRect(0, 0, HRES, VRES, C_BLACK);
        drawHeader();
    }

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
            saveRC();
            delay(1000); sendKeymapForBoot("/"); SD.end(); bootEmulatorDirect(); return;
        }
        statusLine("Status", "New firmware found!", C_YELLOW);
        bool ok = doOTA(FIRMWARE_FILE, "/", versionName);
        if (ok) saveRC();
        SD.end();
        if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
        else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }
        return;
    }

    // --- Modo 2: menu ---
    statusLine("Scanning folders...", C_YELLOW);
    scanFolders();

    if (menuCount == 0) {
        ps2_init();
        delay(500);
        ps2_flush();

        // No emulators on the SD yet. Offer to download them straight away; the user
        // can also open the maintenance menu (F1). Re-scan after each attempt.
        while (menuCount == 0) {
            fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
            statusY = MENU_Y_START;
            statusLine("NO EMULATORS FOUND", C_RED);
            statusLine("(F1 = maintenance menu)", C_CYAN);
            statusLine("Download emulators now? Y/N", C_YELLOW);
  

            ps2_flush();
            uint8_t key = 0;
            while (key == 0) key = ps2_get_key();

            if (key == 0x35) {                       // Y -> run the WiFi/HTTP updater
                runUpdater();
                ps2_flush();
                fillRect(0, 0, HRES, VRES, C_BLACK);
                drawHeader();
                scanFolders();                       // pick up whatever was installed
            } else if (key == KEY_F1 || key == KEY_1) {
                showMaintenanceMenu();
                ps2_flush();
                fillRect(0, 0, HRES, VRES, C_BLACK);
                drawHeader();
                scanFolders();
            }
            // any other key: loop and ask again
        }

        // Emulators now exist — fall through to the normal menu below.
        fillRect(0, 0, HRES, VRES, C_BLACK);
        drawHeader();
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

    // Always flash the selected emulator into ota_0, then reboot straight into it.
    bool ok = doOTA(binPath, folderPath, versionName);
    if (ok) saveRC();

    if (ok) {
        sendKeymapForBoot(folderPath);   // send gamepad keymap while SD is still up
        SD.end();
        bootEmulator();   // flashes done -> boot ota_0 (does not return)
    } else {
        SD.end();
        statusLine("Status", "ERROR! Press RESET", C_RED);
        while (true) delay(1000);
    }



    
    
}

void loop() {

}
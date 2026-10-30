#pragma once
// ===========================================================================
// updater.h - WiFi/HTTP emulator downloader
// ===========================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "bootloader.h"   // config, globals, render (drawProgress/statusLine/...)

#define EMUL_JSON_URL "http://alternativebits.com/esp32/emulators.php"
#define WIFICONFIG_FILE "/wificonfig.rc"
#define WIFI_BURST_TX_POWER 60

String updaterWifiIP = "";

static char updaterDlLabel[72] = "";
static int  updaterDlLabelY = 0;
static int  updaterDlBarY = 0;
static int8_t updaterSavedTxPower = 78;

bool isNewerVersion(const char* localVer, const char* remoteVer) {
    int lMaj = 0, lMin = 0, lPatch = 0;
    int rMaj = 0, rMin = 0, rPatch = 0;

    sscanf(localVer,  "%d.%d.%d", &lMaj, &lMin, &lPatch);
    sscanf(remoteVer, "%d.%d.%d", &rMaj, &rMin, &rPatch);

    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    return rPatch > lPatch;
}

bool updaterWifiConnect() {
    if (!SD.exists(WIFICONFIG_FILE)) return false;

    File f = SD.open(WIFICONFIG_FILE);
    if (!f) return false;

    String ssid = f.readStringUntil('\n');
    ssid.trim();

    String pass = f.readStringUntil('\n');
    pass.trim();

    f.close();

    if (ssid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    WiFi.begin(ssid.c_str(), pass.c_str());
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        updaterWifiIP = WiFi.localIP().toString();

        if (esp_wifi_get_max_tx_power(&updaterSavedTxPower) != ESP_OK) {
            updaterSavedTxPower = 78;
        }

        return true;
    }

    return false;
}

void ensureDirs(const char* fullPath) {
    char tmp[160];
    strncpy(tmp, fullPath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!SD.exists(tmp)) SD.mkdir(tmp);
            *p = '/';
        }
    }
}

static void updaterRadioIdleForDisplay() {
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    WiFi.setSleep(true);
}

static void updaterRadioWakeForBurst() {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(WIFI_BURST_TX_POWER);
}

static void updaterRadioRestore() {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(updaterSavedTxPower);
}

static void updaterPaintProgress(size_t done, size_t total,
                                const char* action) {
    if (updaterDlLabel[0]) {
        fillRect(0, updaterDlLabelY, HRES, 12, C_BLACK);
        drawString(10, updaterDlLabelY, updaterDlLabel, C_CYAN, C_BLACK);
    }

    int savedY = statusY;
    statusY = updaterDlBarY;

    int pct = (total > 0)
        ? (int)((done * 100) / total)
        : 0;

    fillRect(0, updaterDlBarY, HRES, 20, C_BLACK);
    drawProgress(pct, done, total, action);

    statusY = savedY;
}

// For chunked responses without Content-Length, the total size is unknown
// until the response finishes. Show bytes received and an animated bar.
static void updaterPaintUnknownProgress(size_t done) {
    if (updaterDlLabel[0]) {
        fillRect(0, updaterDlLabelY, HRES, 12, C_BLACK);
        drawString(10, updaterDlLabelY, updaterDlLabel, C_CYAN, C_BLACK);
    }

    int savedY = statusY;
    statusY = updaterDlBarY;

    fillRect(0, updaterDlBarY, HRES, 20, C_BLACK);

    char text[48];
    snprintf(text, sizeof(text), "Downloading %u KB",
             (unsigned)(done / 1024));
    drawString(20, statusY, text, C_YELLOW, C_BLACK);

    fillRect(20, statusY + 13, 280, 6, C_BLACK);

    int phase = (millis() / 20) % 480;
    int x = (phase <= 240) ? phase : 480 - phase;
    fillRect(20 + x, statusY + 13, 40, 6, C_GREEN);

    statusY = savedY;
}

// Stream adapter: HTTPClient writes decoded response data here. It forwards
// bytes to the SD file and updates the progress display as data arrives.
class UpdaterProgressStream : public Stream {
public:
    UpdaterProgressStream(File& file, int total)
        : _file(file), _total(total) {}

    int available() override {
        return 0;
    }

    int read() override {
        return -1;
    }

    int peek() override {
        return -1;
    }

    void flush() override {
        _file.flush();
    }

    size_t write(uint8_t value) override {
        return write(&value, 1);
    }

    size_t write(const uint8_t* data, size_t size) override {
        size_t n = _file.write(data, size);

        if (n != size) {
            setWriteError();
        }

        _written += n;

        if (_total > 0) {
            int pct = (int)((_written * 100) / (size_t)_total);
            if (pct != _lastPct) {
                _lastPct = pct;
                updaterPaintProgress(
                    _written,
                    (size_t)_total,
                    "Downloading"
                );
            }
        } else if (_written - _lastDrawn >= 65536) {
            _lastDrawn = _written;
            updaterPaintUnknownProgress(_written);
        }

        return n;
    }

    size_t bytesWritten() const {
        return _written;
    }

private:
    File& _file;
    int _total;
    size_t _written = 0;
    size_t _lastDrawn = 0;
    int _lastPct = -1;
};

// Downloads one file directly to SD.
//
// HTTPClient::writeToStream() handles Transfer-Encoding: chunked and consumes
// the terminal chunk. Reading getStreamPtr() directly can miss the end of a
// chunked body when the server keeps the connection alive.
bool updaterDownloadFile(const char* urlIn, const char* destPath) {
    // Force http:// because TLS does not fit in RAM on this build.
    // Redirects are disabled to avoid following a redirect into HTTPS.
    char url[300];

    if (strncmp(urlIn, "https://", 8) == 0) {
        snprintf(url, sizeof(url), "http://%s", urlIn + 8);
    } else {
        snprintf(url, sizeof(url), "%s", urlIn);
    }

    Serial.printf("[DL] stream core=%d url=%s\n", xPortGetCoreID(), url);
    updaterPaintProgress(0, 1, "Starting");

    ensureDirs(destPath);

    if (SD.exists(destPath)) {
        SD.remove(destPath);
    }

    File f = SD.open(destPath, FILE_WRITE);
    if (!f) {
        updaterRadioRestore();
        statusLine("Download", "SD open fail", C_RED);
        return false;
    }

    updaterRadioWakeForBurst();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(false);
    http.setTimeout(60000);

    if (!http.begin(url)) {
        f.close();
        updaterRadioRestore();
        statusLine("Download", "HTTP begin fail", C_RED);
        return false;
    }

    const char* responseHeaders[] = {
        "Transfer-Encoding"
    };
    http.collectHeaders(responseHeaders, 1);

    int code = http.GET();
    Serial.printf("[DL] HTTP %d\n", code);

    if (code != HTTP_CODE_OK) {
        http.end();
        f.close();
        updaterRadioRestore();

        char msg[32];
        snprintf(msg, sizeof(msg), "HTTP %d", code);
        statusLine("Download", msg, C_RED);
        return false;
    }

    int total = http.getSize();
    String transferEncoding = http.header("Transfer-Encoding");

    Serial.printf("[DL] Content-Length=%d Transfer-Encoding=%s\n",
                  total,
                  transferEncoding.length()
                      ? transferEncoding.c_str()
                      : "(none)");

    // HTTPClient decodes chunked responses and writes their payload through
    // this adapter, which updates the progress display during the download.
    UpdaterProgressStream output(f, total);
    int copied = http.writeToStream(&output);
    size_t written = output.bytesWritten();

    String copyError;
    if (copied <= 0) {
        copyError = http.errorToString(copied);
    }

    http.end();
    f.close();
    updaterRadioRestore();

    if (copied <= 0 || written == 0) {
        Serial.printf("[DL] body copy failed: %s (%d)\n",
                      copyError.c_str(), copied);
        statusLine("Download", "BODY READ FAILED", C_RED);
        return false;
    }

    if ((size_t)copied != written) {
        Serial.printf("[DL] write mismatch copied=%d written=%u\n",
                      copied, (unsigned)written);
        statusLine("Download", "SD WRITE FAILED", C_RED);
        return false;
    }

    if (total > 0 && written != (size_t)total) {
        Serial.printf("[DL] size mismatch got=%u want=%d\n",
                      (unsigned)written, total);
        statusLine("Download", "SIZE MISMATCH", C_RED);
        return false;
    }

    updaterPaintProgress(
        written,
        (total > 0) ? (size_t)total : written,
        "Downloaded"
    );

    Serial.printf("Saved %s (%u bytes)\n",
                  destPath, (unsigned)written);
    return true;
}

void runUpdater() {
    Serial.printf("[UPD] runUpdater core=%d (expect 1) WiFi stack usually core 0\n",
                  xPortGetCoreID());

    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("UPDATE / INSTALL EMULATORS", C_YELLOW);

    statusLine("WiFi Connecting...", C_YELLOW);
    if (!updaterWifiConnect()) {
        statusLine("WiFi Failed! Check wificonfig.rc", C_RED);
        return;
    }

    char ipMsg[40];
    snprintf(ipMsg, sizeof(ipMsg), "OK - IP: %s", updaterWifiIP.c_str());
    statusLine("WiFi", ipMsg, C_GREEN);

    statusLine("Getting emulator list...", C_YELLOW);
    updaterRadioWakeForBurst();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    if (!http.begin(EMUL_JSON_URL)) {
        http.end();
        updaterRadioRestore();
        statusLine("ERROR", "HTTP begin failed", C_RED);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    int code = http.GET();
    Serial.printf("[JSON] HTTP %d\n", code);

    String jsonStr;
    if (code == HTTP_CODE_OK) {
        jsonStr = http.getString();
    }

    http.end();
    updaterRadioIdleForDisplay();
    delay(20);

    if (code != HTTP_CODE_OK) {
        updaterRadioRestore();
        statusLine("ERROR", "Failed to get JSON!", C_RED);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    statusLine("List", "OK", C_GREEN);

    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
        updaterRadioRestore();
        statusLine("JSON", "Parse error!", C_RED);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    int emuCount = doc.size();

    for (int i = 0; i < emuCount; i++) {
        const char* name    = doc[i]["name"];
        const char* folder  = doc[i]["folder"];
        const char* version = doc[i]["version"];
        JsonArray files     = doc[i]["files"];

        char folderPath[80];
        char verPath[100];

        snprintf(folderPath, sizeof(folderPath), "/%s", folder);
        snprintf(verPath, sizeof(verPath), "/%s/version.txt", folder);

        bool folderExists = SD.exists(folderPath);
        String localVersion = "0.0.0";

        if (folderExists && SD.exists(verPath)) {
            File vf = SD.open(verPath);
            if (vf) {
                localVersion = vf.readStringUntil('\n');
                localVersion.trim();
                vf.close();
            }
        }

        bool isNew = !folderExists;
        bool needsUpdate = !isNew &&
                           isNewerVersion(localVersion.c_str(), version);

        if (!isNew && !needsUpdate) {
            char msg[70];
            snprintf(msg, sizeof(msg), "%s v%s up to date", name, version);
            statusLine(msg, C_GREEN);
            continue;
        }

        if (statusY > VRES - 40) {
            fillRect(0, MENU_Y_START + 12, HRES,
                     VRES - MENU_Y_START - 12, C_BLACK);
            statusY = MENU_Y_START + 12;
        }

        char question[80];
        if (isNew) {
            snprintf(question, sizeof(question),
                     "Install %s v%s? Y/N", name, version);
        } else {
            snprintf(question, sizeof(question),
                     "Update %s %s->%s? Y/N",
                     name, localVersion.c_str(), version);
        }

        statusLine(question, C_YELLOW);

        // Limpa o buffer do teclado antes de aguardar a resposta.
        ps2_flush();

        uint8_t key = 0;
        while (key == 0) {
            key = ps2_get_key();
        }

        // 0x35 = make code for 'Y' (PS/2 set 2, US)
        if (key != 0x35) {
            statusLine("Skip", name, C_WHITE);
            continue;
        }

        statusLine("Starting download...", C_CYAN);

        if (!folderExists) {
            SD.mkdir(folderPath);
        }

        int fileCount = files.size();
        bool allOk = true;

        for (int j = 0; j < fileCount; j++) {
            const char* url  = files[j]["url"];
            const char* dest = files[j]["dest"];

            char destPath[160];
            snprintf(destPath, sizeof(destPath),
                     "/%s/%s", folder, dest);

            if (statusY > VRES - 48) {
                fillRect(0, MENU_Y_START + 12, HRES,
                         VRES - MENU_Y_START - 12, C_BLACK);
                statusY = MENU_Y_START + 12;
            }

            snprintf(updaterDlLabel, sizeof(updaterDlLabel),
                     "%s (%d/%d)", dest, j + 1, fileCount);

            updaterDlLabelY = statusY;
            updaterDlBarY = updaterDlLabelY + 12;

            fillRect(0, updaterDlLabelY, HRES, 32, C_BLACK);
            drawString(10, updaterDlLabelY,
                       updaterDlLabel, C_CYAN, C_BLACK);
            statusY = updaterDlBarY;

            // Codifica espaços no URL.
            String urlEncoded = String(url);
            urlEncoded.replace(" ", "%20");

            if (!updaterDownloadFile(urlEncoded.c_str(), destPath)) {
                allOk = false;
            }

            statusY = updaterDlLabelY;
            updaterDlLabel[0] = '\0';
        }

        statusY = updaterDlBarY + 20;
        statusLine(name,
                   allOk ? "OK!" : "FAILED!",
                   allOk ? C_GREEN : C_RED);
    }

    updaterRadioRestore();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
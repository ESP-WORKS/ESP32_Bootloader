#pragma once
// ===========================================================================
// updater.h - WiFi/HTTP emulator downloader
// ===========================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "bootloader.h"   // config, globals, render (drawProgress/statusLine/...)

#define EMUL_JSON_URL "https://alternativebits.com/esp32/emulators.php"
#define WIFICONFIG_FILE "/wificonfig.rc"

// --- A/B test: WiFi download worker on core 0, UI/Canvas on core 1 (Arduino) ---
// FabGL already puts video-intensive tasks on (WIFI_CORE ^ 1) == core 1.
#ifndef WIFI_DL_CORE
#define WIFI_DL_CORE  0
#endif
#ifndef WIFI_DL_CHUNK
#define WIFI_DL_CHUNK  (256 * 1024)  // large bursts — core split handles flicker
#endif
#ifndef WIFI_UI_POLL_MS
#define WIFI_UI_POLL_MS  100   // how often UI samples live offset
#endif
#ifndef WIFI_BURST_TX_POWER
#define WIFI_BURST_TX_POWER  60
#endif
#ifndef WIFI_DL_STACK
#define WIFI_DL_STACK  16384
#endif
#ifndef WIFI_DL_WATCHDOG_MS
#define WIFI_DL_WATCHDOG_MS  120000
#endif

String updaterWifiIP = "";

static char updaterDlLabel[72] = "";
static int  updaterDlLabelY = 0;
static int  updaterDlBarY = 0;
static int8_t updaterSavedTxPower = 78;

struct UpdaterDlCtx {
    char url[288];
    uint8_t* buf;
    int total;
    bool rangesOk;
    volatile size_t offset;   // live byte count — UI polls this (no wait)
    volatile bool failed;
    volatile bool finished;
    SemaphoreHandle_t doneSem;
};

bool isNewerVersion(const char* localVer, const char* remoteVer) {
    int lMaj=0, lMin=0, lPatch=0;
    int rMaj=0, rMin=0, rPatch=0;
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
    String ssid = f.readStringUntil('\n'); ssid.trim();
    String pass = f.readStringUntil('\n'); pass.trim();
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
    strncpy(tmp, fullPath, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
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

static void updaterPaintProgress(size_t done, size_t total, const char* action) {
    if (updaterDlLabel[0]) {
        fillRect(0, updaterDlLabelY, HRES, 12, C_BLACK);
        drawString(10, updaterDlLabelY, updaterDlLabel, C_CYAN, C_BLACK);
    }
    int savedY = statusY;
    statusY = updaterDlBarY;
    int pct = (total > 0) ? (int)((done * 100) / total) : 0;
    fillRect(0, updaterDlBarY, HRES, 20, C_BLACK);
    drawProgress(pct, done, total, action);
    statusY = savedY;
}

// Read body while publishing live progress into ctx->offset (baseOffset + got).
// Does NOT block on UI — download speed stays independent of the bar.
static bool updaterReadBody(HTTPClient& http, uint8_t* dst, int expect,
                            UpdaterDlCtx* ctx, size_t baseOffset) {
    WiFiClient* stream = http.getStreamPtr();
    int got = 0;
    uint32_t lastDataMs = millis();
    while (got < expect) {
        if (!http.connected() && stream->available() == 0) break;
        int avail = stream->available();
        if (avail > 0) {
            int rd = stream->readBytes(dst + got, min(avail, expect - got));
            got += rd;
            lastDataMs = millis();
            if (ctx) ctx->offset = baseOffset + (size_t)got;
        } else {
            if (millis() - lastDataMs > 15000) break;
            delay(1);
        }
    }
    if (ctx) ctx->offset = baseOffset + (size_t)got;
    return got == expect;
}

static int updaterParseTotalFromContentRange(HTTPClient& http) {
    String cr = http.header("Content-Range");
    int slash = cr.lastIndexOf('/');
    if (slash < 0) return -1;
    return cr.substring(slash + 1).toInt();
}

static int updaterProbeSize(const char* url, bool& rangesOk) {
    rangesOk = false;
    const char* collect[] = { "Content-Length", "Accept-Ranges", "Content-Range" };

    HTTPClient probe;
    probe.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    probe.begin(url);
    probe.collectHeaders(collect, 3);
    int code = probe.sendRequest("HEAD");
    int total = -1;
    if (code == HTTP_CODE_OK || code == HTTP_CODE_NO_CONTENT) {
        total = probe.getSize();
        String ar = probe.header("Accept-Ranges");
        rangesOk = (ar.indexOf("bytes") >= 0);
    }
    probe.end();

    if (total > 0 && rangesOk) return total;

    HTTPClient rp;
    rp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    rp.begin(url);
    rp.collectHeaders(collect, 3);
    rp.addHeader("Range", "bytes=0-0");
    updaterRadioWakeForBurst();
    code = rp.GET();
    if (code == HTTP_CODE_PARTIAL_CONTENT) {
        total = updaterParseTotalFromContentRange(rp);
        rangesOk = (total > 0);
    } else if (code == HTTP_CODE_OK) {
        total = rp.getSize();
        rangesOk = false;
    }
    rp.end();
    updaterRadioIdleForDisplay();
    return total;
}

static void updaterWorkerFail(UpdaterDlCtx* ctx) {
    ctx->failed = true;
    ctx->finished = true;
    xSemaphoreGive(ctx->doneSem);
    Serial.printf("[DL] worker FAIL core=%d @ %u\n",
                  xPortGetCoreID(), (unsigned)ctx->offset);
    vTaskDelete(NULL);
}

// Runs on WIFI_DL_CORE (0): HTTP only. Publishes live offset; never waits on UI.
static void updaterDlWorkerTask(void* arg) {
    UpdaterDlCtx* ctx = (UpdaterDlCtx*)arg;
    Serial.printf("[DL] worker START core=%d chunk=%d\n", xPortGetCoreID(), WIFI_DL_CHUNK);

    const char* collect[] = { "Content-Length", "Accept-Ranges", "Content-Range" };
    size_t offset = 0;

    updaterRadioWakeForBurst();

    if (ctx->rangesOk) {
        while (offset < (size_t)ctx->total) {
            size_t end = offset + WIFI_DL_CHUNK - 1;
            if (end >= (size_t)ctx->total) end = ctx->total - 1;
            size_t want = end - offset + 1;

            HTTPClient http;
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.setTimeout(20000);
            http.begin(ctx->url);
            http.collectHeaders(collect, 3);
            char rangeHdr[48];
            snprintf(rangeHdr, sizeof(rangeHdr), "bytes=%u-%u",
                     (unsigned)offset, (unsigned)end);
            http.addHeader("Range", rangeHdr);

            int code = http.GET();
            bool ok = false;
            if (code == HTTP_CODE_PARTIAL_CONTENT) {
                int sz = http.getSize();
                if (sz <= 0) sz = (int)want;
                if (sz > (int)want) sz = (int)want;
                ok = updaterReadBody(http, ctx->buf + offset, sz, ctx, offset);
                if (ok) offset = ctx->offset;
            } else if (code == HTTP_CODE_OK && offset == 0) {
                Serial.printf("[DL] core%d Range ignored — one-shot\n", xPortGetCoreID());
                int sz = http.getSize();
                if (sz <= 0) sz = ctx->total;
                ok = updaterReadBody(http, ctx->buf, sz, ctx, 0);
                if (ok) offset = ctx->offset;
            } else {
                Serial.printf("[DL] HTTP %d unexpected\n", code);
            }
            http.end();

            if (!ok) {
                updaterWorkerFail(ctx);
                return;
            }
            ctx->offset = offset;
            if (offset >= (size_t)ctx->total) break;
        }
    } else {
        HTTPClient http;
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(60000);
        http.begin(ctx->url);
        Serial.println("[DL] GET full (no Range)");
        int code = http.GET();
        Serial.printf("[DL] HTTP %d\n", code);
        bool ok = false;
        if (code == HTTP_CODE_OK) {
            int sz = http.getSize();
            if (sz <= 0) sz = ctx->total;
            ok = updaterReadBody(http, ctx->buf, sz, ctx, 0);
            if (ok) offset = ctx->offset;
        }
        http.end();

        if (!ok) {
            updaterWorkerFail(ctx);
            return;
        }
    }

    ctx->offset = (size_t)ctx->total;
    ctx->finished = true;
    xSemaphoreGive(ctx->doneSem);
    Serial.printf("[DL] worker DONE core=%d\n", xPortGetCoreID());
    vTaskDelete(NULL);
}

bool updaterDownloadFile(const char* url, const char* destPath) {
    Serial.printf("[DL] UI thread core=%d url=%s\n", xPortGetCoreID(), url);

    // Immediate feedback so Y never looks "dead"
    updaterPaintProgress(0, 1, "Starting  ");

    bool rangesOk = false;
    updaterRadioWakeForBurst();
    int total = updaterProbeSize(url, rangesOk);
    updaterRadioIdleForDisplay();
    Serial.printf("[DL] probe total=%d ranges=%d\n", total, (int)rangesOk);

    if (total <= 0) {
        statusLine("Download", "size unknown", C_RED);
        updaterRadioRestore();
        return false;
    }

    uint8_t* fileBuf = (uint8_t*)ps_malloc(total);
    if (!fileBuf) {
        statusLine("Download", "PSRAM fail", C_RED);
        updaterRadioRestore();
        return false;
    }

    UpdaterDlCtx ctx = {};
    strncpy(ctx.url, url, sizeof(ctx.url) - 1);
    ctx.buf = fileBuf;
    ctx.total = total;
    ctx.rangesOk = rangesOk;
    ctx.offset = 0;
    ctx.failed = false;
    ctx.finished = false;
    ctx.doneSem = xSemaphoreCreateBinary();

    if (!ctx.doneSem) {
        free(fileBuf);
        updaterRadioRestore();
        return false;
    }

    Serial.printf("Download %d KB (%s) coreUI=%d coreDL=%d %s\n",
                  total / 1024, rangesOk ? "Range" : "one-shot",
                  xPortGetCoreID(), WIFI_DL_CORE, destPath);

    updaterPaintProgress(0, total, "Downloading");

    BaseType_t created = xTaskCreatePinnedToCore(
        updaterDlWorkerTask, "wifi_dl", WIFI_DL_STACK, &ctx,
        1, nullptr, WIFI_DL_CORE);

    if (created != pdPASS) {
        statusLine("Download", "task fail", C_RED);
        vSemaphoreDelete(ctx.doneSem);
        free(fileBuf);
        updaterRadioRestore();
        return false;
    }

    // Smooth bar: poll live offset; worker never waits on UI (keeps DL speed)
    int lastPct = -1;
    size_t lastDrawn = 0;
    size_t lastSeen = 0;
    uint32_t lastByteMs = millis();
    bool workerDone = false;
    while (!workerDone) {
        size_t done = ctx.offset;
        if (done != lastSeen) {
            lastSeen = done;
            lastByteMs = millis();
        }
        int pct = (total > 0) ? (int)((done * 100) / total) : 0;

        // Redraw on each % step (smooth) — download is not blocked by this
        if (done > 0 && pct != lastPct) {
            lastPct = pct;
            lastDrawn = done;
            updaterPaintProgress(done, total, "Downloading");
        } else if (done > lastDrawn && (done - lastDrawn) >= 32768) {
            lastDrawn = done;
            updaterPaintProgress(done, total, "Downloading");
        }

        if (xSemaphoreTake(ctx.doneSem, pdMS_TO_TICKS(WIFI_UI_POLL_MS)) == pdTRUE) {
            workerDone = true;
            break;
        }

        if (!ctx.finished && (millis() - lastByteMs > WIFI_DL_WATCHDOG_MS)) {
            Serial.println("[DL] UI watchdog — no byte progress");
            statusLine("Download", "TIMEOUT", C_RED);
            ctx.failed = true;
            workerDone = true;
            break;
        }
    }

    // Final frame at 100% / actual
    updaterPaintProgress(ctx.offset, total, "Downloading");
    delay(20);

    bool ok = !ctx.failed && (ctx.offset == (size_t)total);

    vSemaphoreDelete(ctx.doneSem);

    if (!ok) {
        free(fileBuf);
        updaterRadioRestore();
        statusLine("Download", "FAILED", C_RED);
        return false;
    }

    updaterPaintProgress(total, total, "Saving    ");
    ensureDirs(destPath);
    File f = SD.open(destPath, FILE_WRITE);
    if (!f) {
        free(fileBuf);
        updaterRadioRestore();
        return false;
    }
    f.write(fileBuf, total);
    f.close();
    free(fileBuf);

    updaterRadioRestore();
    Serial.printf("Saved %s\n", destPath);
    return true;
}

void runUpdater() {
    Serial.printf("[UPD] runUpdater core=%d (expect 1) WiFi stack usually core 0\n",
                  xPortGetCoreID());

    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("UPDATE / INSTALL EMULATORS", C_YELLOW);
    //statusLine("Test: DL@core0 UI@core1", C_CYAN);

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
    http.begin(EMUL_JSON_URL);
    int code = http.GET();
    String jsonStr;
    if (code == 200) jsonStr = http.getString();
    http.end();
    updaterRadioIdleForDisplay();
    delay(20);

    if (code != 200) {
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

        char folderPath[80], verPath[100];
        snprintf(folderPath, sizeof(folderPath), "/%s", folder);
        snprintf(verPath, sizeof(verPath), "/%s/version.txt", folder);

        bool folderExists = SD.exists(folderPath);
        String localVersion = "0.0.0";
        if (folderExists && SD.exists(verPath)) {
            File vf = SD.open(verPath);
            if (vf) { localVersion = vf.readStringUntil('\n'); localVersion.trim(); vf.close(); }
        }

        bool isNew = !folderExists;
        bool needsUpdate = !isNew && isNewerVersion(localVersion.c_str(), version);

        if (!isNew && !needsUpdate) {
            char msg[70];
            snprintf(msg, sizeof(msg), "%s v%s up to date", name, version);
            statusLine(msg, C_GREEN);
            continue;
        }

        if (statusY > VRES - 40) {
            fillRect(0, MENU_Y_START + 12, HRES, VRES - MENU_Y_START - 12, C_BLACK);
            statusY = MENU_Y_START + 12;
        }


        char question[80];
        if (isNew)
            snprintf(question, sizeof(question), "Install %s v%s? Y/N", name, version);
        else
            snprintf(question, sizeof(question), "Update %s %s->%s? Y/N", name, localVersion.c_str(), version);

        statusLine(question, C_YELLOW);

        // LIMPEZA DO BUFFER INSERIDA AQUI
        ps2_flush();

        uint8_t key = 0;
        while (key == 0) key = ps2_get_key();

        // 0x35 = make code for 'Y' (PS/2 set 2, US)
        if (key != 0x35) {
            statusLine("Skip", name, C_WHITE);
            continue;
        }


        statusLine("Starting download...", C_CYAN);
        if (!folderExists) SD.mkdir(folderPath);

        int fileCount = files.size();
        bool allOk = true;

        for (int j = 0; j < fileCount; j++) {
            const char* url  = files[j]["url"];
            const char* dest = files[j]["dest"];

            char destPath[160];
            snprintf(destPath, sizeof(destPath), "/%s/%s", folder, dest);

            if (statusY > VRES - 48) {
                fillRect(0, MENU_Y_START + 12, HRES, VRES - MENU_Y_START - 12, C_BLACK);
                statusY = MENU_Y_START + 12;
            }

            snprintf(updaterDlLabel, sizeof(updaterDlLabel), "%s (%d/%d)", dest, j + 1, fileCount);
            updaterDlLabelY = statusY;
            updaterDlBarY = updaterDlLabelY + 12;
            fillRect(0, updaterDlLabelY, HRES, 32, C_BLACK);
            drawString(10, updaterDlLabelY, updaterDlLabel, C_CYAN, C_BLACK);
            statusY = updaterDlBarY;

            // Remover o " " do URL para evitar problemas de download
            String urlEncoded = String(url);
            urlEncoded.replace(" ", "%20");
            if (!updaterDownloadFile(urlEncoded.c_str(), destPath)) allOk = false;
            

            statusY = updaterDlLabelY;
            updaterDlLabel[0] = '\0';
        }

        statusY = updaterDlBarY + 20;
        statusLine(name, allOk ? "OK!" : "FAILED!", allOk ? C_GREEN : C_RED);
    }

    updaterRadioRestore();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
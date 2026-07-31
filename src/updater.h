#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>

#define EMUL_JSON_URL "https://alternativebits.com/esp32/emulators.php"
#define WIFICONFIG_FILE "/wificonfig.rc"

String updaterWifiIP = "";

// Compara versões X.Y.Z - retorna true se remote > local
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
    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        updaterWifiIP = WiFi.localIP().toString();
        return true;
    }



    return false;
}

// Cria pastas intermediárias de um caminho (ex: /FMSX/bios/MSX.rom cria /FMSX e /FMSX/bios)
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


bool updaterDownloadFile(const char* url, const char* destPath) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(url);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }

    int total = http.getSize();
    if (total <= 0) { http.end(); return false; }

    // Aloca tudo em PSRAM
    uint8_t* fileBuf = (uint8_t*)ps_malloc(total);
    if (!fileBuf) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    int written = 0;
    int lastPercent = -1;

    // Baixa tudo pra PSRAM (sem tocar no SD)
    while (http.connected() && written < total) {
        int available = stream->available();
        if (available > 0) {
            int rd = stream->readBytes(fileBuf + written, min(available, total - written));
            written += rd;
            int pct = (written * 100) / total;
            if (pct / 10 != lastPercent / 10) {
                drawProgress(pct, written, total, "Downloading");
                lastPercent = pct;
            }
        }
    }
    http.end();

    // Grava tudo no SD de uma vez
    ensureDirs(destPath);
    File f = SD.open(destPath, FILE_WRITE);
    if (!f) { free(fileBuf); return false; }
    f.write(fileBuf, total);
    f.close();

    free(fileBuf);
    return written == total;
}


void runUpdater() {

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
    HTTPClient http;
    http.begin(EMUL_JSON_URL);
    int code = http.GET();
    if (code != 200) {
        statusLine("ERROR", "Failed to get JSON!", C_RED);
        http.end(); return;
    }
    String jsonStr = http.getString();
    http.end();

    DynamicJsonDocument doc(32768);  // maior por causa da bios com muitos arquivos
    if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
        statusLine("JSON", "Parse error!", C_RED);
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
            statusLine( msg, C_GREEN);
            continue;
        }

        // Scroll se necessário
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

        uint8_t key = 0;
        while (key == 0) key = ps2_get_key();

        if (key != 0x35) {
            statusLine("Skip", name, C_WHITE);
            continue;
        }

        if (!folderExists) SD.mkdir(folderPath);

        // Baixa todos os arquivos da lista
        int fileCount = files.size();
        bool allOk = true;
        
        for (int j = 0; j < fileCount; j++) {
            const char* url  = files[j]["url"];
            const char* dest = files[j]["dest"];

            char destPath[160];
            snprintf(destPath, sizeof(destPath), "/%s/%s", folder, dest);

            char dlMsg[70];
            snprintf(dlMsg, sizeof(dlMsg), "%s (%d/%d)", dest, j+1, fileCount);

            // AQUIIIII
            //fillRect(0, statusY, HRES, 12, C_BLACK);
            //drawString(10, statusY, dlMsg, C_CYAN, C_BLACK);
            //statusY += 12;   //  barra de progresso vai pra linha de baixo
            if (!updaterDownloadFile(url, destPath)) allOk = false;
            //statusY -= 12;   // volta, pra próximo arquivo reusar as duas linhas
        }

        statusLine(name, allOk ? "OK!                                                      " : "FAILED!", allOk ? C_GREEN : C_RED);
    }

    statusLine("Done", "Press any key to return", C_GREEN);
    WiFi.disconnect();

}
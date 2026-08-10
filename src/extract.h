#pragma once
// ===========================================================================
// extract.h - Extract emulator firmware from Eremus merged .bin files
// Looks for <PREFIX>*.bin in SD root, extracts app at offset 0x40000,
// saves to /<PREFIX>/firmware.bin + version.txt
// ===========================================================================
#include "bootloader.h"

#define EXTRACT_OFFSET 0x40000
#define EXTRACT_CHUNK  (64 * 1024)

// Emulator name prefixes (Eremus family). Add more as needed.
static const char* EXTRACT_PREFIXES[] = { "MSPX", "CPCESP", "ESPectrum" };
static const int   EXTRACT_PREFIX_COUNT = 3;

// Given "MSPX.0.1.alpha.bin" and prefix "MSPX" -> version "0.1.alpha"
static void extractVersionFromName(const char* fileName, const char* prefix, char* outVer, size_t outSz) {
    size_t plen = strlen(prefix);
    const char* p = fileName + plen;           // skip prefix
    if (*p == '.') p++;                          // skip separating dot
    strncpy(outVer, p, outSz - 1);
    outVer[outSz - 1] = '\0';
    // strip trailing ".bin"
    size_t vl = strlen(outVer);
    if (vl >= 4 && strcasecmp(outVer + vl - 4, ".bin") == 0) outVer[vl - 4] = '\0';
}

// Extract one file: /<fileName>  ->  /<prefix>/firmware.bin (+ version.txt)
static bool extractOne(const char* fileName, const char* prefix) {
    char srcPath[160];
    snprintf(srcPath, sizeof(srcPath), "/%s", fileName);

    File src = SD.open(srcPath, FILE_READ);
    if (!src) { statusLine("ERROR", "Cannot open source", C_RED); return false; }

    size_t srcSize = src.size();
    if (srcSize <= EXTRACT_OFFSET) {
        statusLine("ERROR", "File smaller than offset", C_RED);
        src.close();
        return false;
    }
    size_t appSize = srcSize - EXTRACT_OFFSET;

    // Destination folder /<prefix>
    char folderPath[80];
    snprintf(folderPath, sizeof(folderPath), "/%s", prefix);
    if (!SD.exists(folderPath)) SD.mkdir(folderPath);

    // firmware.bin
    char fwPath[120];
    snprintf(fwPath, sizeof(fwPath), "/%s/firmware.bin", prefix);
    if (SD.exists(fwPath)) SD.remove(fwPath);   // truncate: overwrite cleanly
    File dst = SD.open(fwPath, FILE_WRITE);
    if (!dst) { statusLine("ERROR", "Cannot create firmware.bin", C_RED); src.close(); return false; }

    src.seek(EXTRACT_OFFSET);

    uint8_t* buf = (uint8_t*)ps_malloc(EXTRACT_CHUNK);
    if (!buf) { statusLine("ERROR", "PSRAM alloc failed", C_RED); src.close(); dst.close(); return false; }

    size_t written = 0;
    int lastPct = -1;
    while (written < appSize) {
        size_t toRead = min((size_t)EXTRACT_CHUNK, appSize - written);
        int rd = src.read(buf, toRead);
        if (rd <= 0) break;
        dst.write(buf, rd);
        written += rd;
        int pct = (int)((written * 100) / appSize);
        if (pct / 10 != lastPct / 10) {
            drawProgress(pct, written, appSize, "Extracting");
            lastPct = pct;
        }
    }
    free(buf);
    dst.close();
    src.close();

    // version.txt
    char ver[64];
    extractVersionFromName(fileName, prefix, ver, sizeof(ver));
    char verPath[120];
    snprintf(verPath, sizeof(verPath), "/%s/version.txt", prefix);
    if (SD.exists(verPath)) SD.remove(verPath);  // truncate: overwrite cleanly
    File vf = SD.open(verPath, FILE_WRITE);
    if (vf) { vf.print(ver); vf.close(); }

    return (written == appSize);
}

// Scans SD root for files starting with any known prefix, offers to extract each.
void runExtractFirmware() {
    Serial.println("runExtractFirmware()");
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("EXTRACT FIRMWARE", C_YELLOW);

    File root = SD.open("/");
    if (!root) { statusLine("ERROR", "Cannot open SD root", C_RED); return; }

    int found = 0;

    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }

        // entry.name() may include path on some cores; take basename
        const char* full = entry.name();
        const char* base = strrchr(full, '/');
        base = base ? base + 1 : full;

        // Must end in .bin
        size_t bl = strlen(base);
        if (bl < 5 || strcasecmp(base + bl - 4, ".bin") != 0) { entry.close(); continue; }

        // Match a known prefix
        const char* matchedPrefix = nullptr;
        for (int i = 0; i < EXTRACT_PREFIX_COUNT; i++) {
            size_t pl = strlen(EXTRACT_PREFIXES[i]);
            if (strncasecmp(base, EXTRACT_PREFIXES[i], pl) == 0) {
                matchedPrefix = EXTRACT_PREFIXES[i];
                break;
            }
        }
        if (!matchedPrefix) { entry.close(); continue; }

        found++;

        // Scroll if needed
        if (statusY > VRES - 40) {
            fillRect(0, MENU_Y_START + 12, HRES, VRES - MENU_Y_START - 12, C_BLACK);
            statusY = MENU_Y_START + 12;
        }

        char q[100];
        snprintf(q, sizeof(q), "Extract %s? Y/N", base);
        statusLine(q, C_YELLOW);

        // copy name because entry will be reused
        char fileNameCopy[128];
        strncpy(fileNameCopy, base, sizeof(fileNameCopy) - 1);
        fileNameCopy[sizeof(fileNameCopy) - 1] = '\0';
        entry.close();

        ps2_flush();
        uint8_t key = 0;
        while (key == 0) key = ps2_get_key();
        if (key != 0x35) {  // not Y
            statusLine("Skipped", fileNameCopy, C_WHITE);
            continue;
        }

        bool ok = extractOne(fileNameCopy, matchedPrefix);
        statusY+=20;
        statusLine(matchedPrefix, ok ? "Extracted OK!" : "FAILED!", ok ? C_GREEN : C_RED);
       
    }
    root.close();

    if (found == 0) {
        statusLine("No matching .bin files found", C_RED);
    }

    statusLine("Done - press any key", C_GREEN);
    ps2_flush();
    while (ps2_get_key() == 0);
}
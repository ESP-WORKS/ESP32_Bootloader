#pragma once
// ===========================================================================
// render.h - All UI: rendering primitives, status, header, menu, speaker
// ===========================================================================
#include "bootloader.h"

void runUpdater();          // defined in updater.h (included after this)
void runExtractFirmware();  // defined in extract.h (included after this)




void drawString(int x, int y, const char* str, Color ink, Color paper,
                fabgl::FontInfo const* font = &fabgl::FONT_6x12) {
    cv.selectFont(font);
    cv.setPenColor(ink);
    cv.setBrushColor(paper);
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));
    cv.drawText(x, y, str);
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
// Badge de configuração de teclado — canto superior esquerdo (X=2, Y=2)
// Exibe "C1", "C2" ou "C3" com fundo colorido. Chamado após qualquer troca.
// C1=azul  C2=magenta  C3=vermelho
// ---------------------------------------------------------------------------
void drawKbdBadge() {
    const int bx = 2, by = 2, bw = 14, bh = 9;
    Color bg = (kbdConfigIdx == 0) ? C_BLUE :
               (kbdConfigIdx == 1) ? C_MAGENTA : C_RED;
    fillRect(bx, by, bw, bh, bg);
    drawString(bx + 1, by, KBD_CONFIGS[kbdConfigIdx].badge, C_WHITE, bg, &fabgl::FONT_5x7);
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
    drawString(200, 44, "F1/1=Menu/Help", C_MAGENTA, C_BLACK);
    drawLine(8, 56, HRES - 9, C_BLUE);
    drawKbdBadge();
}

static const int MENU_BAR_W = 140;      // list width (left-aligned)
static const int MENU_BAR_X = 8;        // list starts near left edge

// Emulator preview image (image.bmp inside the emulator folder)
static const int EMU_IMG_W = 160;
static const int EMU_IMG_H = 160;
static const int EMU_IMG_X = HRES - EMU_IMG_W;      // right side: 320-180 = 140
static const int EMU_IMG_Y = MENU_Y_START - 10;
//static const int EMU_IMG_Y = 45;
static const int MENU_COUNTER_CLEAR_W = 5 * 6;  // room for "99/99"

void drawMenuCounter(int selected) {
    fillRect(HRES - MENU_COUNTER_CLEAR_W - 4, 32, MENU_COUNTER_CLEAR_W, MENU_LINE_H, C_BLACK);
    char sc[8];
    snprintf(sc, sizeof(sc), "%d/%d", selected + 1, menuCount);
    drawString(HRES - (int)strlen(sc) * 6 - 4, 32, sc, C_YELLOW, C_BLACK);
}

void drawMenuRow(int idx, int selected, int scrollOffset, const String& currentPath) {
    int i = idx - scrollOffset;
    if (i < 0 || i >= MAX_VISIBLE || idx < 0 || idx >= menuCount) return;

    bool isSel = (idx == selected);
    Color ink   = isSel ? C_BLACK : C_GREEN;
    Color paper = isSel ? C_GREEN : C_BLACK;
    bool isInstalled = (String(menuEntries[idx].path) == currentPath);

    char line[42];
    snprintf(line, sizeof(line), "%s%s", isInstalled ? "*" : "", menuEntries[idx].name);

    int y = MENU_Y_START + i * MENU_LINE_H;
    fillRect(MENU_BAR_X, y, MENU_BAR_W, MENU_LINE_H, paper);

    cv.selectFont(&fabgl::FONT_6x12);
    drawString(MENU_BAR_X + 2, y, line, ink, paper);  // left-aligned
}

// Clears the preview image area.
void clearEmulatorImage() {
    fillRect(EMU_IMG_X, EMU_IMG_Y, EMU_IMG_W, EMU_IMG_H, C_BLACK);
}

// Footer info line: first line of /bootloader/<folderName>.txt, drawn below the menu area.
static const int EMU_INFO_Y = VRES - 13;   // one line from the bottom

void drawEmulatorInfo(const char* folderPath) {
    fillRect(0, EMU_INFO_Y, HRES, 12, C_BLACK);   // clear the footer line

    const char* name = folderPath;
    while (*name == '/') name++;

    char txtPath[200];
    snprintf(txtPath, sizeof(txtPath), "/assets/%s.txt", name);
    if (!SD.exists(txtPath)) return;

    File f = SD.open(txtPath, FILE_READ);
    if (!f) return;
    String line = f.readStringUntil('\n');
    f.close();
    line.trim();
    if (line.length() == 0) return;

    drawString(6, EMU_INFO_Y, line.c_str(), C_CYAN, C_BLACK);
}

// Draws /bootloader/<folderName>.bmp (8-bit or 24-bit uncompressed BMP, up to 180x180).
// folderPath comes in as "/ANOTHER WORLD"; the image is "/bootloader/ANOTHER WORLD.bmp".
// If the file is missing or not a valid BMP, nothing is drawn.
bool drawEmulatorImage(const char* folderPath) {
    clearEmulatorImage();

    const char* name = folderPath;
    while (*name == '/') name++;              // strip leading slash(es)

    char imgPath[200];
    snprintf(imgPath, sizeof(imgPath), "/assets/%s.bmp", name);

    if (!SD.exists(imgPath)) return true;

    File f = SD.open(imgPath, FILE_READ);
    if (!f) return true;

    uint8_t header[54];
    int rd = f.read(header, 54);
    if (rd != 54) { f.close(); return true; }
    if (header[0] != 'B' || header[1] != 'M') { f.close(); return true; }

    uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t  w  = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    int32_t  h  = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    uint16_t bpp = header[28] | (header[29] << 8);

    uint32_t headerSize = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);


    bool topDown = false;
    if (h < 0) { h = -h; topDown = true; }   // negative height = top-down BMP

    int drawW = (w < EMU_IMG_W) ? w : EMU_IMG_W;
    int drawH = (h < EMU_IMG_H) ? h : EMU_IMG_H;

    // Precompute Canvas colors per BMP palette entry (8-bit) — avoids per-pixel RGB math.
    RGB888 palColor[256];
    if (bpp == 8) {
        uint32_t palOffset = 14 + headerSize;       // file header (14) + DIB header
        f.seek(palOffset);
        for (int i = 0; i < 256; i++) {
            uint8_t e[4];
            if (f.read(e, 4) != 4) break;
            palColor[i] = RGB888(e[2], e[1], e[0]);  // BMP palette entry is B,G,R,0
        }
    }

    int rowSize = (bpp == 24) ? (((w * 3 + 3) / 4) * 4)
                              : (((w * 1 + 3) / 4) * 4);   // 8bpp = 1 byte/px, padded to 4

    // Read the whole pixel area into PSRAM in ONE SD read (seek-per-row over SPI is slow).
    uint32_t pixBytes = (uint32_t)rowSize * h;
    uint8_t* pix = (uint8_t*)ps_malloc(pixBytes);
    if (!pix) { f.close(); return true; }
    f.seek(dataOffset);
    f.read(pix, pixBytes);
    f.close();

    // Draw same-color horizontal runs as single lines. Abort early if the user presses
    // a key — navigation must never wait for the image to finish painting.
    for (int ry = 0; ry < drawH; ry++) {
        if (ps2_available()) { free(pix); return false; }  // key arrived: stop, retry later

        int srcRow = topDown ? ry : (h - 1 - ry);
        uint8_t* row = pix + (uint32_t)srcRow * rowSize;

        int y = EMU_IMG_Y + ry;
        int rx = 0;
        while (rx < drawW) {
            RGB888 c = (bpp == 8)
                ? palColor[row[rx]]
                : RGB888(row[rx*3 + 2], row[rx*3 + 1], row[rx*3 + 0]);
            int runStart = rx;
            rx++;
            while (rx < drawW) {
                RGB888 c2 = (bpp == 8)
                    ? palColor[row[rx]]
                    : RGB888(row[rx*3 + 2], row[rx*3 + 1], row[rx*3 + 0]);
                if (!(c2 == c)) break;
                rx++;
            }
            cv.setPenColor(c);
            cv.drawLine(EMU_IMG_X + runStart, y, EMU_IMG_X + rx - 1, y);
        }
    }

    free(pix);
    return true;
}

void drawMenu(int selected, int scrollOffset, const String& currentPath) {
    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawMenuCounter(selected);

    int visible = min(menuCount, MAX_VISIBLE);
    for (int i = 0; i < visible; i++) {
        int idx = i + scrollOffset;
        if (idx >= menuCount) break;
        drawMenuRow(idx, selected, scrollOffset, currentPath);
    }
}

// Incremental update: only old/new rows when scroll unchanged; full redraw on scroll
void updateMenuSelection(int oldSelected, int newSelected,
                         int oldScroll, int newScroll,
                         const String& currentPath) {
    if (oldScroll != newScroll) {
        drawMenu(newSelected, newScroll, currentPath);
        return;
    }
    if (oldSelected != newSelected) {
        drawMenuRow(oldSelected, newSelected, newScroll, currentPath);
        drawMenuRow(newSelected, newSelected, newScroll, currentPath);
    }
    drawMenuCounter(newSelected);
    cv.waitCompletion(false);  // drain FabGL queue before next key; prevents lockup on fast nav
}

void drawMaintenanceScreen() {
    fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawString(10, MENU_Y_START,       "*** MENU ***",                    C_YELLOW, C_BLACK);
    drawString(10, MENU_Y_START +  14, "N - Clear Bootloader NVS",        C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  28, "A - Clear ALL NVS",               C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  42, "O - Clear Otadata",               C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  56, "R - Reset ESP32",                 C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  70, "D - Download Emulators / Update", C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  84, "E - Extract Firmware (Eremus)",   C_WHITE,  C_BLACK);
    drawString(10, MENU_Y_START +  98, "ESC/SPACE - Cancel",              C_CYAN,   C_BLACK);
    
    drawString(10, MENU_Y_START + 124, "ESP32 BootLoader by FG1998 - github.com/fg1998", C_WHITE, C_BLACK, &fabgl::FONT_5x7);
    drawString(10, MENU_Y_START + 136, "Download and improvements by Joselito Oliveira", C_WHITE, C_BLACK, &fabgl::FONT_5x7);
    drawString(10, MENU_Y_START + 148, "Tested by Caca Couto, Miguel Roberto and Rodolfo Guerra",         C_WHITE, C_BLACK, &fabgl::FONT_5x7);

}

void showMaintenanceMenu() {
    drawMaintenanceScreen();


    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        const char* msg  = nullptr;
        const char* done = nullptr;

        if      (key == KEY_N_KEY) { msg = "Clear Bootloader NVS?"; done = "Bootloader NVS cleared!"; }
        else if (key == KEY_A_KEY) { msg = "Clear ALL NVS?";         done = "ALL NVS cleared!"; }
        else if (key == KEY_O_KEY) { msg = "Clear Otadata?";         done = "Otadata cleared!"; }
        else if (key == KEY_R_KEY) { msg = "Reset ESP32?";           done = "Resetting..."; }
        else if (key == KEY_D_KEY) { msg = "Download Emulators?";    done = "Download complete!"; }
        else if (key == KEY_E_KEY) { msg = "Extract Firmware?";      done = "Firmware extracted!"; }
        else if (key == KEY_ESC || key == KEY_SPACE) { Serial.println("ESC pressed");  break; }

        if (!msg) continue;

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      msg,                         C_YELLOW, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Y to confirm, N to cancel", C_WHITE,  C_BLACK);

        uint8_t conf = 0;
        while (conf == 0) conf = ps2_get_key();

        if (conf != 0x35) {
            // N — cancela e volta ao menu
            drawMaintenanceScreen();
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
            Serial.println("Reset ESP32 selected");
            shutdownForReboot();
            ESP.restart();
        }
        else if (key == KEY_D_KEY) {
            Serial.println("Download selected");
            runUpdater();
            ps2_flush();
            statusLine("Status", "Press any key to reboot", C_YELLOW);
            while (ps2_get_key() == 0);
            shutdownForReboot();
            ESP.restart();
        }
        else if (key == KEY_E_KEY) {
            Serial.println("Extract Firmware selected");
            runExtractFirmware();
            ps2_flush();
            statusY = statusY_original;
            drawMaintenanceScreen();
            continue;
        }

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      done,                        C_GREEN, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Press any key to reboot",   C_WHITE, C_BLACK);
        while (ps2_get_key() == 0);
        shutdownForReboot();
        ESP.restart();
    }
}


int runMenu() {

    cv.setPenColor(C_WHITE);
    cv.setBrushColor(C_BLACK);
    cv.selectFont(&fabgl::FONT_6x12);

    if (menuCount == 0) return -1;

    prefs.begin("sdloader", true);
    String currentPath = prefs.getString("path", "");
    prefs.end();

    int selected = 0;
    for (int i = 0; i < menuCount; i++) {
        if (String(menuEntries[i].path) == currentPath) { selected = i; break; }
    }

    int scrollOffset = 0;
    if (selected >= MAX_VISIBLE) scrollOffset = selected - MAX_VISIBLE + 1;

    ps2_init();
    delay(500);
    ps2_flush();  // drop noise from attach

    drawMenu(selected, scrollOffset, currentPath);

    bool hasInstalled = (currentPath.length() > 0);
    uint32_t autobootStart = millis();
    int lastSecsLeft = -1;
    // FONT_6x12: worst case "10s" = 3 chars; clear a 4-char slot to avoid ghosts
    const int countdownClearW = 4 * 6;
    const int countdownY = MENU_Y_START - 20;
    const int countdownClearX = HRES - countdownClearW - 4;

    // Magic button: debounce
    static uint32_t lastBtnMs = 0;
    bool lastBtnState = HIGH;

    // Deferred preview image: only load after navigation settles (keeps fast scroll smooth)
    const uint32_t IMG_DELAY_MS = 120;
    int  imgShownFor   = -1;                 // which selected index is currently drawn
    int  imgPendingFor = selected;           // index waiting to be drawn
    uint32_t imgPendingSince = millis();

    while (true) {

        // Draw the deferred preview image once navigation has paused
        if (imgPendingFor != imgShownFor &&
            (millis() - imgPendingSince) >= IMG_DELAY_MS) {
            drawEmulatorInfo(menuEntries[imgPendingFor].path);   // footer text (fast)
            if (drawEmulatorImage(menuEntries[imgPendingFor].path))
                imgShownFor = imgPendingFor;   // only mark done if it finished (not aborted)
        }

        // --- Polling magic button GPIO36 (cicla config de teclado) ---
        bool btnNow = digitalRead(magicBtnPin);
        if (btnNow == LOW && lastBtnState == HIGH && (millis() - lastBtnMs) > 300) {
            lastBtnMs = millis();
            // Cicla para próxima config
            ps2_shutdown();
            kbdConfigIdx = (kbdConfigIdx + 1) % KBD_CONFIG_COUNT;
            ps2DatPin = KBD_CONFIGS[kbdConfigIdx].dat;
            ps2ClkPin = KBD_CONFIGS[kbdConfigIdx].clk;
            ps2_init();
            delay(200);
            ps2_flush();
            Serial.printf("Magic button: trocou para %s\n", KBD_CONFIGS[kbdConfigIdx].label);
            // Atualiza badge sem redesenhar tudo
            drawKbdBadge();
            // Cancela autoboot ao trocar config
            if (hasInstalled) {
                hasInstalled = false;
                fillRect(countdownClearX, countdownY, countdownClearW, MENU_LINE_H, C_BLACK);
            }
        }
        lastBtnState = btnNow;

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
            drawMenu(selected, scrollOffset, currentPath);
            continue;
        }
 //       if (key == KEY_F2 || key == KEY_2) {
 //           runUpdater();
 //           ps2_flush();
 //           statusLine("Status", "Press any key to return", C_YELLOW);
 //           while (ps2_get_key() == 0);
 //           drawMenu(selected, scrollOffset, currentPath);
 //           statusY = statusY_original;
 //           continue;
 //       }
        if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && selected > 0) {
            int oldSelected = selected;
            int oldScroll = scrollOffset;
            selected--;
            if (selected < scrollOffset) scrollOffset--;
            updateMenuSelection(oldSelected, selected, oldScroll, scrollOffset, currentPath);
            imgPendingFor = selected; imgPendingSince = millis();
        } else if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) && selected < menuCount - 1) {
            int oldSelected = selected;
            int oldScroll = scrollOffset;
            selected++;
            if (selected >= scrollOffset + MAX_VISIBLE) scrollOffset++;
            updateMenuSelection(oldSelected, selected, oldScroll, scrollOffset, currentPath);
            imgPendingFor = selected; imgPendingSince = millis();
        } else if (key == KEY_PGUP || key == KEY_LEFT || key == KEY_O_KEY) {
            // Page up: jump back one full page (left arrow / O)
            if (selected > 0) {
                selected -= MAX_VISIBLE;
                if (selected < 0) selected = 0;
                scrollOffset = selected - (selected % MAX_VISIBLE);
                if (scrollOffset > menuCount - MAX_VISIBLE) scrollOffset = max(0, menuCount - MAX_VISIBLE);
                if (selected < scrollOffset) scrollOffset = selected;
                drawMenu(selected, scrollOffset, currentPath);
                imgPendingFor = selected; imgPendingSince = millis();
            }
        } else if (key == KEY_PGDN || key == KEY_RIGHT || key == KEY_P_KEY) {
            // Page down: jump forward one full page (right arrow / P)
            if (selected < menuCount - 1) {
                selected += MAX_VISIBLE;
                if (selected > menuCount - 1) selected = menuCount - 1;
                if (selected >= scrollOffset + MAX_VISIBLE)
                    scrollOffset = selected - MAX_VISIBLE + 1;
                if (scrollOffset < 0) scrollOffset = 0;
                drawMenu(selected, scrollOffset, currentPath);
                imgPendingFor = selected; imgPendingSince = millis();
            }
        } else if (key == KEY_ENTER) {
            return selected;
        }
    }
}
#pragma once
// ===========================================================================
// render.h - All UI: rendering primitives, status, header, menu, speaker
// ===========================================================================
#include "bootloader.h"

void runUpdater();              // defined in updater.h (included after this)
void runExtractFirmware();     // defined in extract.h (included after this)
void showDisclaimer();          // defined in disclaimer.h (included after this)




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

// Same path as the folder preview BMPs (drawEmulatorImage, 8bpp):
// createRawPixel(RGB222) -> setRawPixel, straight into the framebuffer.
// gamepad_data[] is already RGB222 packed R | G<<2 | B<<4 (values 0..63).
void drawGamepad(int x, int y) {
    uint8_t rawPal[64];
    for (int i = 0; i < 64; i++)
        rawPal[i] = DisplayController.createRawPixel(
            RGB222(i & 3, (i >> 2) & 3, (i >> 4) & 3));

    const uint8_t* p = gamepad_data;
    for (int ry = 0; ry < GPAD_H; ry++) {
        int sy = y + ry;
        for (int rx = 0; rx < GPAD_W; rx++, p++)
            DisplayController.setRawPixel(x + rx, sy, rawPal[*p & 63]);
    }
}

// ===========================================================================
// Autoboot screen layout (320 x 240)
//
//   y=0      logo (o mais no topo que cabe)
//   y=40     gamepad.img 160x160, centrado  (arte útil ~ (26,32)-(133,119))
//   y=220    "Press any key for the menu"
//   y=228    countdown  (o mais embaixo que cabe)
//
// Labels do keymap: offsets (ox, oy) RELATIVOS ao canto superior esquerdo
// do gamepad. Pixel na tela = (AUTOBOOT_GPAD_X + ox, AUTOBOOT_GPAD_Y + oy).
// Mova AUTOBOOT_GPAD_X/Y para deslocar o controle; os labels acompanham.
// Para um botão específico, edite só a linha correspondente em
// AUTOBOOT_GP_LABELS[] — o índice é o mesmo de GP_BUTTONS[] / keymap.ini.
//
// align: 0 = ox é a borda esquerda do texto
//        1 = ox é a borda direita  (texto cresce para a esquerda)
//        2 = ox é o centro do texto
// ===========================================================================
#define AUTOBOOT_LOGO_X   ((HRES - LOGO_W) / 2)
#define AUTOBOOT_LOGO_Y   0
#define AUTOBOOT_GPAD_X   ((HRES - GPAD_W) / 2)   // 80
#define AUTOBOOT_GPAD_Y   40
#define AUTOBOOT_HINT_Y   (VRES - 24)             // 216  FONT_6x12 "ESC=Menu"
#define AUTOBOOT_COUNT_Y  (VRES - 12)             // 228  FONT_6x12
#define AUTOBOOT_NAME_Y   (LOGO_H + 2)            // 20   FONT_6x12, under logo

#define GP_LBL_LEFT    0
#define GP_LBL_RIGHT   1
#define GP_LBL_CENTER  2
#define GP_LBL_CHAR_W  6                          // FONT_6x12 glyph width

struct AutobootGpLabel {
    int16_t ox;
    int16_t oy;
    uint8_t align;
};

// Centros visuais APROXIMADOS dos botões DENTRO do gamepad.img 160x160
// (para você reposicionar um label perto do botão certo):
//
//            L2 (45,36)                    R2 (115,36)
//            L1 (42,48)                    R1 (118,48)
//         UP (48,58)                    Y (112,58)
//   LEFT (36,70)  RIGHT (60,70)    X (100,70)  B (124,70)
//        DOWN (48,82)                   A (112,82)
//        L3 (55,100)   HOME (80,88)    R3 (105,100)
//              SELECT (70,78)  START (90,78)
//
// ox negativo = esquerda da imagem; ox>=160 = direita da imagem.
// Com FONT_6x12 (12px alto), espaçamento vertical ~14px entre labels.
// Ajuste ox/oy aqui para reposicionar qualquer label individualmente.
static const AutobootGpLabel AUTOBOOT_GP_LABELS[GP_BUTTON_COUNT] = {
    //                                              ox    oy   align
    /*  0  A       (cruz, baixo do losango)     */ { 164,  82, GP_LBL_LEFT   },
    /*  1  B       (bola, direita do losango)   */ { 164,  68, GP_LBL_LEFT   },
    /*  2  X       (quadrado, esquerda)         */ { 164,  54, GP_LBL_LEFT   },
    /*  3  Y       (triângulo, topo)            */ { 164,  40, GP_LBL_LEFT   },
    /*  4  UP      (dpad cima)                  */ {  -4,  40, GP_LBL_RIGHT  },
    /*  5  DOWN    (dpad baixo)                 */ {  -4,  68, GP_LBL_RIGHT  },
    /*  6  LEFT    (dpad esquerda)              */ {  -4,  54, GP_LBL_RIGHT  },
    /*  7  RIGHT   (dpad direita)               */ {  -4,  82, GP_LBL_RIGHT  },
    /*  8  L1      (shoulder esquerdo)          */ {  -4,  26, GP_LBL_RIGHT  },
    /*  9  R1      (shoulder direito)           */ { 164,  26, GP_LBL_LEFT   },
    /* 10  L2      (trigger esquerdo, topo)     */ {  -4,  12, GP_LBL_RIGHT  },
    /* 11  R2      (trigger direito, topo)      */ { 164,  12, GP_LBL_LEFT   },
    /* 12  L3      (click analogico esquerdo)   */ {  -4,  96, GP_LBL_RIGHT  },
    /* 13  R3      (click analogico direito)    */ { 164,  96, GP_LBL_LEFT   },
    /* 14  START                                */ { 164, 118, GP_LBL_LEFT   },
    /* 15  SELECT                               */ {  -4, 118, GP_LBL_RIGHT  },
    /* 16  HOME    (botao central, abaixo)      */ {  80, 136, GP_LBL_CENTER },
};

static void drawAutobootLabel(int ox, int oy, uint8_t align,
                              const char* btn, const char* key) {
    char buf[28];
    snprintf(buf, sizeof(buf), "%s=%s", btn, (key && key[0]) ? key : "--");
    int w = (int)strlen(buf) * GP_LBL_CHAR_W;   // GP_LBL_CHAR_W = 6 (FONT_6x12)
    int x = AUTOBOOT_GPAD_X + ox;
    int y = AUTOBOOT_GPAD_Y + oy;
    if (align == GP_LBL_RIGHT)  x -= w;
    if (align == GP_LBL_CENTER) x -= w / 2;
    if (x < 0) x = 0;
    if (x + w > HRES) x = HRES - w;
    if (y < 0) y = 0;
    if (y + 12 > VRES) y = VRES - 12;
    drawString(x, y, buf, C_GREEN, C_BLACK);     // FONT_6x12 (default), verde
}

void drawAutobootKeymap(const Keymap* km) {
    for (int i = 0; i < GP_BUTTON_COUNT; i++) {
        if (!gpButtonPresent(i)) continue;
        const AutobootGpLabel& L = AUTOBOOT_GP_LABELS[i];
        const char* key = (km && km->keys[i][0]) ? km->keys[i] : "--";
        drawAutobootLabel(L.ox, L.oy, L.align, GP_BUTTONS[i].iniName, key);
    }
}

void drawAutobootCountdown(int sec) {
    fillRect(0, AUTOBOOT_COUNT_Y, HRES, 12, C_BLACK);
    char cd[28];
    snprintf(cd, sizeof(cd), "Booting in %ds", sec);
    int w = (int)strlen(cd) * 6;                 // FONT_6x12
    int x = (HRES - w) / 2;
    if (x < 0) x = 0;
    drawString(x, AUTOBOOT_COUNT_Y, cd, C_CYAN, C_BLACK);
}

void drawAutobootScreen(const Keymap* km) {
    fillRect(0, 0, HRES, VRES, C_BLACK);
    drawLogo(AUTOBOOT_LOGO_X, AUTOBOOT_LOGO_Y);

    // Nome do emulador centralizado logo abaixo do logo (FONT_6x12)
    if (km && km->name[0]) {
        int w = (int)strlen(km->name) * GP_LBL_CHAR_W;
        int x = (HRES - w) / 2;
        if (x < 0) x = 0;
        drawString(x, AUTOBOOT_NAME_Y, km->name, C_CYAN, C_BLACK);
    }

    // Hint: ESC abre o menu, qualquer outra tecla pula direto pro emulador
    const char* hint = "ESC=Menu";
    int hw = (int)strlen(hint) * GP_LBL_CHAR_W;
    drawString((HRES - hw) / 2, AUTOBOOT_HINT_Y, hint, C_YELLOW, C_BLACK);

    cv.waitCompletion(false);   // drain Canvas before setRawPixel (same as menu images)
    drawGamepad(AUTOBOOT_GPAD_X, AUTOBOOT_GPAD_Y);
    drawAutobootKeymap(km);
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

#define PROMPT_NO    0
#define PROMPT_YES   1
#define PROMPT_MAINT 2
#define PROMPT_ABORT 3

static const char* const YESNO_LABEL[2] = { "YES", "NO" };

static void drawYesNoBar(int selected) {
    const int fy = VRES - 10;
    fillRect(0, fy - 2, HRES, 12, C_BLACK);

    int widths[2];
    int totalW = 0;
    for (int i = 0; i < 2; i++) {
        widths[i] = (int)strlen(YESNO_LABEL[i]) * 5;
        totalW += widths[i];
    }
    const int gap = 16;
    totalW += gap;
    int x = (HRES - totalW) / 2;

    for (int i = 0; i < 2; i++) {
        bool isSel = (i == selected);
        Color ink   = isSel ? C_BLACK : C_CYAN;
        Color paper = isSel ? C_GREEN : C_BLACK;
        if (isSel) fillRect(x - 1, fy - 2, widths[i] + 2, 11, paper);
        drawString(x, fy, YESNO_LABEL[i], ink, paper, &fabgl::FONT_5x7);
        x += widths[i] + gap;
    }
}

static void clearYesNoBar() {
    fillRect(0, VRES - 12, HRES, 12, C_BLACK);
}

// Footer YES|NO. Left/Right moves; Enter confirms the bar.
// A=ENTER confirms; B=SPACE and N = NO; HOME/ESC aborts to the previous screen.
// Keyboard Y still means YES.
// defaultYes: start on YES (install/update) or NO (reinstall).
// allowMaint: F1 / 1 return PROMPT_MAINT (Right stays on the bar).
int promptYesNo(bool defaultYes, bool allowMaint = false) {
    int sel = defaultYes ? 0 : 1;
    drawYesNoBar(sel);
    ps2_flush();

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) { delay(1); continue; }

        if (allowMaint && (key == KEY_F1 || key == KEY_1)) {
            clearYesNoBar();
            return PROMPT_MAINT;
        }
        if (key == 0x35) {                 // Y
            clearYesNoBar();
            return PROMPT_YES;
        }
        if (key == KEY_ESC) {
            clearYesNoBar();
            return PROMPT_ABORT;
        }
        if (key == KEY_N_KEY || key == KEY_SPACE) {
            clearYesNoBar();
            return PROMPT_NO;
        }

        bool isLeft  = (key == KEY_LEFT  || key == 0x6B);
        bool isRight = (key == KEY_RIGHT || key == 0x74);
        if (isLeft) {
            sel = 0;
            drawYesNoBar(sel);
            continue;
        }
        if (isRight) {
            sel = 1;
            drawYesNoBar(sel);
            continue;
        }
        if (key != KEY_ENTER) continue;

        clearYesNoBar();
        return (sel == 0) ? PROMPT_YES : PROMPT_NO;
    }
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
    drawString(188, 44, "F1/1/Right=Menu", C_MAGENTA, C_BLACK);
    drawLine(8, 56, HRES - 9, C_BLUE);
    drawKbdBadge();
}

static const int MENU_BAR_W = 140;      // list width (left-aligned)
static const int MENU_BAR_X = 8;        // list starts near left edge

// Emulator preview image (image.bmp inside the emulator folder)
static const int EMU_IMG_W = 160;
static const int EMU_IMG_H = 160;
static const int EMU_IMG_X = HRES - EMU_IMG_W - 12;      // right side
static const int EMU_IMG_Y = MENU_Y_START - 10;
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
// Images are blitted with setRawPixel (framebuffer). Canvas fillRect alone
// does not erase that, so a missing BMP would leave the previous console.
void clearEmulatorImage() {
    fillRect(EMU_IMG_X, EMU_IMG_Y, EMU_IMG_W, EMU_IMG_H, C_BLACK);
    cv.waitCompletion(false);
    uint8_t black = DisplayController.createRawPixel(RGB222(0, 0, 0));
    for (int y = 0; y < EMU_IMG_H; y++) {
        int py = EMU_IMG_Y + y;
        for (int x = 0; x < EMU_IMG_W; x++)
            DisplayController.setRawPixel(EMU_IMG_X + x, py, black);
    }
}

// Footer info line: first line of /assets/<folderName>.txt, drawn below the menu area.
// If the text is wider than the screen it scrolls sideways (marquee).
static const int EMU_INFO_Y = VRES - 13;   // one line from the bottom
static const int EMU_INFO_X = 6;           // left margin
static const int EMU_INFO_CHAR_W = 6;      // FONT_6x12 glyph width

static char     emuInfoText[160] = "";     // current footer text
static int      emuInfoPixW = 0;           // text width in pixels
static int      emuInfoScroll = 0;         // current horizontal offset (px)
static bool     emuInfoNeedsScroll = false;
static uint32_t emuInfoLastTick = 0;
static uint32_t emuInfoStartMs = 0;        // when this text was loaded (for initial delay)

static const uint32_t EMU_INFO_START_DELAY_MS = 3000;  // wait before scrolling starts

// Load the footer text for an emulator and reset the marquee state.
void drawEmulatorInfo(const char* folderPath) {
    fillRect(0, EMU_INFO_Y, HRES, 12, C_BLACK);   // clear the footer line
    emuInfoText[0] = '\0';
    emuInfoPixW = 0;
    emuInfoScroll = 0;
    emuInfoNeedsScroll = false;

    const char* name = folderPath;
    while (*name == '/') name++;

    char txtPath[200];
    snprintf(txtPath, sizeof(txtPath), "/assets/%s.txt", name);
    Serial.printf("[emutxt] folder='%s' path='%s'\n", name, txtPath);
    if (!SD.exists(txtPath)) {
        Serial.println("[emutxt] NOT FOUND");
        cv.waitCompletion(false);
        return;
    }

    File f = SD.open(txtPath, FILE_READ);
    if (!f) {
        Serial.println("[emutxt] open FAIL");
        return;
    }
    String line = f.readStringUntil('\n');
    f.close();
    line.trim();
    Serial.printf("[emutxt] bytes=%d line='%s'\n", line.length(), line.c_str());
    if (line.length() == 0) return;

    strncpy(emuInfoText, line.c_str(), sizeof(emuInfoText) - 1);
    emuInfoText[sizeof(emuInfoText) - 1] = '\0';
    emuInfoPixW = (int)strlen(emuInfoText) * EMU_INFO_CHAR_W;

    int avail = HRES - EMU_INFO_X;          // space from left margin to right edge
    emuInfoNeedsScroll = (emuInfoPixW > avail);
    emuInfoLastTick = millis();
    emuInfoStartMs  = millis();             // start the 3s pause before scrolling

    // Draw the initial (left-aligned) frame.
    drawString(EMU_INFO_X, EMU_INFO_Y, emuInfoText, C_CYAN, C_BLACK);
}

// Advance the footer marquee one step if the text is wider than the screen.
// Call every loop; it self-throttles with EMU_INFO_STEP_MS so it moves at a
// readable speed. The text scrolls left, then wraps around with a gap.
void tickEmulatorInfo() {
    if (!emuInfoNeedsScroll || emuInfoText[0] == '\0') return;

    // Hold the text still for a few seconds after it appears, so it can be read
    // from the start before the marquee begins.
    if (millis() - emuInfoStartMs < EMU_INFO_START_DELAY_MS) return;

    const uint32_t EMU_INFO_STEP_MS = 120;   // lower = faster scroll
    if (millis() - emuInfoLastTick < EMU_INFO_STEP_MS) return;
    emuInfoLastTick = millis();

    const int GAP = 24;                      // px gap before the text repeats
    int span = emuInfoPixW + GAP;            // one full cycle width
    emuInfoScroll += EMU_INFO_CHAR_W;        // step by one glyph (stays pixel-aligned)
    if (emuInfoScroll >= span) emuInfoScroll = 0;

    // Clear the footer line, then draw the text twice (current + wrapped copy) so
    // the tail of one and the head of the next are both visible during the wrap.
    fillRect(0, EMU_INFO_Y, HRES, 12, C_BLACK);
    int x1 = EMU_INFO_X - emuInfoScroll;
    drawString(x1, EMU_INFO_Y, emuInfoText, C_CYAN, C_BLACK);
    drawString(x1 + span, EMU_INFO_Y, emuInfoText, C_CYAN, C_BLACK);
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
    Serial.printf("[emuimg] folder='%s' path='%s' ps2avail=%d\n",
                  name, imgPath, (int)ps2_available());

    if (!SD.exists(imgPath)) {
        Serial.println("[emuimg] NOT FOUND (slot cleared)");
        return true;
    }

    File f = SD.open(imgPath, FILE_READ);
    if (!f) {
        Serial.println("[emuimg] open FAIL");
        return true;
    }
    Serial.printf("[emuimg] opened size=%u\n", (unsigned)f.size());

    uint8_t header[54];
    int rd = f.read(header, 54);
    if (rd != 54) {
        Serial.printf("[emuimg] header short rd=%d\n", rd);
        f.close();
        return true;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        Serial.printf("[emuimg] not BMP magic=%02X %02X\n", header[0], header[1]);
        f.close();
        return true;
    }

    uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t  w  = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    int32_t  h  = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    uint16_t bpp = header[28] | (header[29] << 8);

    uint32_t headerSize = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);

    bool topDown = false;
    if (h < 0) { h = -h; topDown = true; }   // negative height = top-down BMP

    Serial.printf("[emuimg] w=%d h=%d bpp=%u off=%u dib=%u topDown=%d\n",
                  (int)w, (int)h, bpp, dataOffset, headerSize, (int)topDown);

    int drawW = (w < EMU_IMG_W) ? w : EMU_IMG_W;
    int drawH = (h < EMU_IMG_H) ? h : EMU_IMG_H;
    if (EMU_IMG_X + drawW > HRES) drawW = HRES - EMU_IMG_X;   // never write past the screen
    if (EMU_IMG_Y + drawH > VRES) drawH = VRES - EMU_IMG_Y;

    // Precompute a raw framebuffer byte per 8-bit palette entry (fast path, 64-color BMPs).
    uint8_t rawPal[256];
    if (bpp == 8) {
        uint32_t palOffset = 14 + headerSize;       // file header (14) + DIB header
        f.seek(palOffset);
        for (int i = 0; i < 256; i++) {
            uint8_t e[4];
            if (f.read(e, 4) != 4) break;
            rawPal[i] = DisplayController.createRawPixel(RGB222(e[2] >> 6, e[1] >> 6, e[0] >> 6)); // e=B,G,R,0
        }
    }

    int rowSize = (bpp == 24) ? (((w * 3 + 3) / 4) * 4)
                              : (((w * 1 + 3) / 4) * 4);   // 8bpp = 1 byte/px, padded to 4

    // Read the whole pixel area into PSRAM in ONE SD read (seek-per-row over SPI is slow).
    uint32_t pixBytes = (uint32_t)rowSize * h;
    uint8_t* pix = (uint8_t*)ps_malloc(pixBytes);
    if (!pix) {
        Serial.printf("[emuimg] ps_malloc FAIL bytes=%u\n", pixBytes);
        f.close();
        return true;
    }
    f.seek(dataOffset);
    int got = f.read(pix, pixBytes);
    f.close();
    Serial.printf("[emuimg] pixels got=%d want=%u rowSize=%d draw=%dx%d\n",
                  got, pixBytes, rowSize, drawW, drawH);

    // Write straight to the framebuffer (no Canvas queue). Abort on keypress so
    // navigation never waits for the image.
    for (int ry = 0; ry < drawH; ry++) {
        if (ps2_available()) {
            Serial.printf("[emuimg] ABORT row=%d/%d\n", ry, drawH);
            free(pix);
            return false;
        }

        int srcRow = topDown ? ry : (h - 1 - ry);
        uint8_t* row = pix + (uint32_t)srcRow * rowSize;
        int y = EMU_IMG_Y + ry;

        if (bpp == 8) {
            for (int rx = 0; rx < drawW; rx++)
                DisplayController.setRawPixel(EMU_IMG_X + rx, y, rawPal[row[rx]]);
        } else {
            for (int rx = 0; rx < drawW; rx++) {
                uint8_t rawb = DisplayController.createRawPixel(
                    RGB222(row[rx*3+2] >> 6, row[rx*3+1] >> 6, row[rx*3+0] >> 6));
                DisplayController.setRawPixel(EMU_IMG_X + rx, y, rawb);
            }
        }
    }

    free(pix);
    Serial.println("[emuimg] OK");
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

enum {
    MAINT_CLEAR_BL = 0,
    MAINT_CLEAR_ALL,
    MAINT_CLEAR_OTA,
    MAINT_RESET,
    MAINT_DOWNLOAD,
    MAINT_EXTRACT,
    MAINT_DISCLAIMER,
    MAINT_WIFI,
    MAINT_GAMEPAD,
    MAINT_CANCEL,
    MAINT_COUNT
};

static const char* const MAINT_ITEMS[MAINT_COUNT] = {
    "Clear Bootloader NVS",
    "Clear ALL NVS",
    "Clear Otadata",
    "Reset ESP32",
    "Download / Update Emulators",
    "Extract Firmware (Eremus)",
    "Show Disclaimer",
    "Reset Wifi Credentials",
    "Config Gamepad",
    "Cancel",
};

static const int MAINT_BAR_X = 8;
static const int MAINT_BAR_W = 304;

void drawMaintRow(int idx, int selected) {
    if (idx < 0 || idx >= MAINT_COUNT) return;
    bool isSel = (idx == selected);
    Color ink   = isSel ? C_BLACK : C_GREEN;
    Color paper = isSel ? C_GREEN : C_BLACK;
    int y = MENU_Y_START + idx * MENU_LINE_H;
    fillRect(MAINT_BAR_X, y, MAINT_BAR_W, MENU_LINE_H, paper);
    cv.selectFont(&fabgl::FONT_6x12);
    drawString(MAINT_BAR_X + 2, y, MAINT_ITEMS[idx], ink, paper);
}

void drawMaintenanceScreen(int selected) {
    fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    for (int i = 0; i < MAINT_COUNT; i++) drawMaintRow(i, selected);
    int cy = MENU_Y_START + MAINT_COUNT * MENU_LINE_H + 8;
    drawString(10, cy,      "ESP32 BootLoader by FG1998 - github.com/fg1998", C_WHITE, C_BLACK, &fabgl::FONT_5x7);
    drawString(10, cy + 9,  "Download and improvements by Joselito Oliveira", C_WHITE, C_BLACK, &fabgl::FONT_5x7);
    drawString(10, cy + 18, "Tested by Caca Couto, Miguel Roberto and Rodolfo Guerra", C_WHITE, C_BLACK, &fabgl::FONT_5x7);
}

static void updateMaintSelection(int oldSelected, int newSelected) {
    if (oldSelected == newSelected) return;
    drawMaintRow(oldSelected, newSelected);
    drawMaintRow(newSelected, newSelected);
    cv.waitCompletion(false);
}

static bool maintConfirm(const char* msg) {
    fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawString(10, MENU_Y_START, msg, C_YELLOW, C_BLACK);
    const char* opts[2] = { "Confirm", "Cancel" };
    int selected = 1;
    auto drawOpt = [&](int idx) {
        bool isSel = (idx == selected);
        Color ink   = isSel ? C_BLACK : C_GREEN;
        Color paper = isSel ? C_GREEN : C_BLACK;
        int y = MENU_Y_START + 16 + idx * MENU_LINE_H;
        fillRect(MAINT_BAR_X, y, MAINT_BAR_W, MENU_LINE_H, paper);
        cv.selectFont(&fabgl::FONT_6x12);
        drawString(MAINT_BAR_X + 2, y, opts[idx], ink, paper);
    };
    drawOpt(0);
    drawOpt(1);
    cv.waitCompletion(false);
    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        speakerClick();
        if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && selected > 0) {
            selected = 0;
            drawOpt(0);
            drawOpt(1);
            cv.waitCompletion(false);
        } else if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) && selected < 1) {
            selected = 1;
            drawOpt(0);
            drawOpt(1);
            cv.waitCompletion(false);
        } else if (key == KEY_ENTER) {
            return selected == 0;
        } else if (key == KEY_ESC || key == KEY_SPACE) {
            return false;
        }
    }
}

static const char* const GP_PRESS_HINT[GP_BUTTON_COUNT] = {
    "A - South", "B - East", "X - West", "Y - North",
    "UP - North", "DOWN - South", "LEFT - West", "RIGHT - East",
    "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT", "HOME",
};

static void drawGamepadConfigFrame(const char* title, const char* hint) {
    fillRect(0, 56, HRES, VRES - 56, C_BLACK);
    int tw = (int)strlen(title) * 6;
    int tx = (HRES - tw) / 2;
    if (tx < 0) tx = 0;
    drawString(tx, 56, title, C_YELLOW, C_BLACK);
    if (hint) {
        int hw = (int)strlen(hint) * 6;
        int hx = (HRES - hw) / 2;
        if (hx < 0) hx = 0;
        drawString(hx, 68, hint, C_CYAN, C_BLACK);
    }
    cv.waitCompletion(false);
    drawGamepad((HRES - GPAD_W) / 2, 80);
}

static void gpCfgWaitKey() {
    ps2_flush();
    while (ps2_get_key() == 0) delay(1);
}

static void gpCfgMsg(const char* title, const char* line, Color col) {
    fillRect(0, 56, HRES, VRES - 56, C_BLACK);
    drawString(10, 80, title, col, C_BLACK);
    if (line) drawString(10, 96, line, C_WHITE, C_BLACK);
    drawString(10, 112, "Press any key", C_CYAN, C_BLACK);
    cv.waitCompletion(false);
    gpCfgWaitKey();
}

static bool gpCfgNeedTdisplay() {
    //if (ps2_tdisplay_present()) return true;
    //gpCfgMsg("Please insert PS/2 - bluetooth adapter", nullptr, C_YELLOW);
    //return false;
    return true;
}

enum {
    GPCFG_PAIR = 0,
    GPCFG_UNPAIR,
    GPCFG_BUTTONS,
    GPCFG_BACK,
    GPCFG_COUNT
};

static const char* const GPCFG_ITEMS[GPCFG_COUNT] = {
    "Pair Gamepad",
    "Unpair Gamepad",
    "Configure Buttons",
    "Back",
};

static void drawGpCfgRow(int idx, int selected) {
    bool isSel = (idx == selected);
    Color ink   = isSel ? C_BLACK : C_GREEN;
    Color paper = isSel ? C_GREEN : C_BLACK;
    int y = MENU_Y_START + 16 + idx * MENU_LINE_H;
    fillRect(MAINT_BAR_X, y, MAINT_BAR_W, MENU_LINE_H, paper);
    cv.selectFont(&fabgl::FONT_6x12);
    drawString(MAINT_BAR_X + 2, y, GPCFG_ITEMS[idx], ink, paper);
}

static void drawGpCfgMenu(int selected, const char* statusLine) {
    fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawString(10, MENU_Y_START - 10, "CONFIG GAMEPAD", C_YELLOW, C_BLACK);
    if (statusLine && statusLine[0])
        drawString(10, MENU_Y_START + 2, statusLine, C_CYAN, C_BLACK);
    for (int i = 0; i < GPCFG_COUNT; i++) drawGpCfgRow(i, selected);
    cv.waitCompletion(false);
}

static void showGamepadPair() {
    if (!gpCfgNeedTdisplay()) return;
    if (!ps2_gp_scan_on()) {
        gpCfgMsg("Scan failed", "T-Display did not answer", C_RED);
        return;
    }

    char names[PS2TX_GP_MAX][PS2TX_GP_NAME_MAX + 1];
    uint8_t idxs[PS2TX_GP_MAX];
    int count = 0;
    int selected = 0;
    uint32_t lastList = 0;

    auto draw = [&]() {
        fillRect(0, 56, HRES, VRES - 56, C_BLACK);
        drawString(10, 56, "PAIR GAMEPAD", C_YELLOW, C_BLACK);
        drawString(10, 68, "Put pad in pairing mode", C_CYAN, C_BLACK);
        if (count == 0) {
            drawString(10, 92, "Searching...", C_WHITE, C_BLACK);
        } else {
            if (selected >= count) selected = count - 1;
            if (selected < 0) selected = 0;
            for (int i = 0; i < count; i++) {
                bool isSel = (i == selected);
                Color ink   = isSel ? C_BLACK : C_GREEN;
                Color paper = isSel ? C_GREEN : C_BLACK;
                int y = 88 + i * MENU_LINE_H;
                fillRect(MAINT_BAR_X, y, MAINT_BAR_W, MENU_LINE_H, paper);
                drawString(MAINT_BAR_X + 2, y, names[i], ink, paper);
            }
        }
        drawString(10, VRES - 14, "ENTER pair   ESC back", C_WHITE, C_BLACK);
        cv.waitCompletion(false);
    };

    draw();
    while (true) {
        if (millis() - lastList > 500) {
            lastList = millis();
            int n = 0;
            if (ps2_gp_list(names, idxs, PS2TX_GP_MAX, &n)) count = n;
            draw();
        }
        if (ps2_available()) {
            uint8_t key = ps2_get_key();
            if (key == 0) continue;
            speakerClick();
            if (key == KEY_ESC || key == KEY_SPACE) break;
            if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && selected > 0) {
                selected--;
                draw();
            } else if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) &&
                       selected < count - 1) {
                selected++;
                draw();
            } else if (key == KEY_ENTER) {
                if (count <= 0) continue;
                uint8_t st = 0;
                if (ps2_gp_pair(idxs[selected], &st) && st == 1) {
                    ps2_gp_scan_off();
                    char msg[40];
                    snprintf(msg, sizeof(msg), "Paired: %s", names[selected]);
                    gpCfgMsg(msg, "Discovery off", C_GREEN);
                    return;
                }
                gpCfgMsg("Pair failed", "Try again", C_RED);
                draw();
            }
        }
        delay(1);
    }
    ps2_gp_scan_off();
}

static void showGamepadUnpair() {
    if (!gpCfgNeedTdisplay()) return;
    GpStatus st;
    char line[40] = "Forget saved pad?";
    if (ps2_gp_status(&st) && (st.flags & 0x01) && st.name[0])
        snprintf(line, sizeof(line), "Unpair %s?", st.name);
    if (!maintConfirm(line)) return;

    uint8_t ack = 0;
    if (!ps2_gp_unpair(&ack)) {
        gpCfgMsg("Unpair failed", "T-Display did not answer", C_RED);
        return;
    }
    gpCfgMsg("Unpaired", "T-Display is restarting", C_YELLOW);
}

static void showGamepadButtons() {
    if (!gpCfgNeedTdisplay()) return;
    {
        GpStatus st;
        if (!ps2_gp_status(&st) || !(st.flags & 0x02)) {
            gpCfgMsg("No gamepad connected", "Pair first, then configure", C_YELLOW);
            return;
        }
    }

    if (!ps2_learn_begin()) {
        gpCfgMsg("Learn mode failed", "T-Display did not answer", C_RED);
        return;
    }

    bool aborted = false;
    for (int i = 0; i < GP_BUTTON_COUNT && !aborted; i++) {
        if (!gpButtonPresent(i)) continue;
        char title[40];
        snprintf(title, sizeof(title), "Press %s", GP_PRESS_HINT[i]);
        drawGamepadConfigFrame(title, "15s skip   ESC abort");
        uint8_t hid = 0;
        int ev = ps2_wait_pad_event(&hid, 15000);
        if (ev < 0) { aborted = true; break; }
        if (ev == 1 && hid >= 1 && hid <= 0x11) {
            gGpHid[i] = hid;
            Serial.printf("[gpcfg] %s <- hid %u\n", GP_BUTTONS[i].iniName, hid);
            speakerClick();
        } else {
            Serial.printf("[gpcfg] %s skip (keep %u)\n",
                          GP_BUTTONS[i].iniName, (unsigned)gGpHid[i]);
        }
        ps2_learn_settle();
    }

    if (!aborted) {
        drawGamepadConfigFrame("Move LEFT stick", "15s skip   ESC abort");
        uint8_t hid = 0;
        int ev = ps2_wait_pad_event(&hid, 15000);
        (void)hid;
        if (ev < 0) aborted = true;
        else if (ev == 2) gGpAnaL = 1;
        ps2_learn_settle();
    }
    if (!aborted) {
        drawGamepadConfigFrame("Move RIGHT stick", "15s skip   ESC abort");
        uint8_t hid = 0;
        int ev = ps2_wait_pad_event(&hid, 15000);
        (void)hid;
        if (ev < 0) aborted = true;
        else if (ev == 2) gGpAnaR = 1;
        ps2_learn_settle();
    }

    ps2_learn_end();
    if (!aborted) {
        saveRC();
        ps2_send_gp_remap();
        gpCfgMsg("Saved to /bootl.rc", "Remap sent", C_GREEN);
    } else {
        gpCfgMsg("Config cancelled", nullptr, C_YELLOW);
    }
}

void showGamepadConfig() {
    int selected = 0;
    char status[40] = "";
    GpStatus st;
    if (ps2_gp_status(&st)) {
        if (st.flags & 0x02) snprintf(status, sizeof(status), "Connected: %s", st.name);
        else if (st.flags & 0x01) snprintf(status, sizeof(status), "Paired: %s", st.name);
        else snprintf(status, sizeof(status), "No gamepad paired");
    }
    drawGpCfgMenu(selected, status);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        speakerClick();
        if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && selected > 0) {
            int old = selected;
            selected--;
            drawGpCfgRow(old, selected);
            drawGpCfgRow(selected, selected);
            cv.waitCompletion(false);
            continue;
        }
        if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) &&
            selected < GPCFG_COUNT - 1) {
            int old = selected;
            selected++;
            drawGpCfgRow(old, selected);
            drawGpCfgRow(selected, selected);
            cv.waitCompletion(false);
            continue;
        }
        if (key == KEY_ESC || key == KEY_SPACE ||
            (key == KEY_ENTER && selected == GPCFG_BACK)) {
            break;
        }
        if (key != KEY_ENTER) continue;

        if (selected == GPCFG_PAIR) showGamepadPair();
        else if (selected == GPCFG_UNPAIR) showGamepadUnpair();
        else if (selected == GPCFG_BUTTONS) showGamepadButtons();

        {
            GpStatus st2;
            if (ps2_gp_status(&st2)) {
                if (st2.flags & 0x02) snprintf(status, sizeof(status), "Connected: %s", st2.name);
                else if (st2.flags & 0x01) snprintf(status, sizeof(status), "Paired: %s", st2.name);
                else snprintf(status, sizeof(status), "No gamepad paired");
            }
        }
        ps2_flush();
        drawHeader();
        drawGpCfgMenu(selected, status);
    }
}

void showMaintenanceMenu() {
    int selected = 0;
    drawMaintenanceScreen(selected);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        speakerClick();

        if ((key == KEY_UP || key == KEY_UP_EXT || key == KEY_Q) && selected > 0) {
            int old = selected;
            selected--;
            updateMaintSelection(old, selected);
            continue;
        }
        if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) && selected < MAINT_COUNT - 1) {
            int old = selected;
            selected++;
            updateMaintSelection(old, selected);
            continue;
        }
        if (key == KEY_ESC || key == KEY_SPACE ||
            (key == KEY_ENTER && selected == MAINT_CANCEL)) {
            break;
        }
        if (key != KEY_ENTER) continue;

        if (selected == MAINT_DOWNLOAD) {
            runUpdater();
            ps2_flush();
            statusY = statusY_original;
            drawMaintenanceScreen(selected);
            continue;
        }
        if (selected == MAINT_DISCLAIMER) {
            showDisclaimer();
            ps2_flush();
            drawMaintenanceScreen(selected);
            continue;
        }
        if (selected == MAINT_GAMEPAD) {
            showGamepadConfig();
            ps2_flush();
            drawHeader();
            drawMaintenanceScreen(selected);
            continue;
        }

        const char* msg  = nullptr;
        const char* done = nullptr;
        if      (selected == MAINT_CLEAR_BL)  { msg = "Clear Bootloader NVS?";     done = "Bootloader NVS cleared!"; }
        else if (selected == MAINT_CLEAR_ALL) { msg = "Clear ALL NVS?";            done = "ALL NVS cleared!"; }
        else if (selected == MAINT_CLEAR_OTA) { msg = "Clear Otadata?";            done = "Otadata cleared!"; }
        else if (selected == MAINT_RESET)     { msg = "Reset ESP32?";              done = "Resetting..."; }
        else if (selected == MAINT_EXTRACT)   { msg = "Extract Firmware?";         done = "Firmware extracted!"; }
        else if (selected == MAINT_WIFI)      { msg = "Reset WiFi Credentials?";   done = "WiFi reset! Restarting..."; }
        if (!msg) continue;

        if (!maintConfirm(msg)) {
            drawMaintenanceScreen(selected);
            continue;
        }

        if      (selected == MAINT_CLEAR_BL)  { prefs.begin("sdloader", false); prefs.clear(); prefs.end(); }
        else if (selected == MAINT_CLEAR_ALL) { nvs_flash_erase(); nvs_flash_init(); }
        else if (selected == MAINT_CLEAR_OTA) {
            const esp_partition_t* otadata = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
            if (otadata) esp_partition_erase_range(otadata, 0, otadata->size);
        }
        else if (selected == MAINT_RESET) {
            shutdownForReboot();
            ESP.restart();
        }
        else if (selected == MAINT_EXTRACT) {
            runExtractFirmware();
            ps2_flush();
            statusY = statusY_original;
            drawMaintenanceScreen(selected);
            continue;
        }
        else if (selected == MAINT_WIFI) {
            if (SD.exists("/wificonfig.rc")) SD.remove("/wificonfig.rc");
        }

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      done,                      C_GREEN, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Press any key to reboot", C_WHITE, C_BLACK);
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
            Serial.printf("[emuimg] swap pending=%d shown=%d path='%s'\n",
                          imgPendingFor, imgShownFor,
                          menuEntries[imgPendingFor].path);
            drawEmulatorInfo(menuEntries[imgPendingFor].path);   // footer text (fast)
            bool ok = drawEmulatorImage(menuEntries[imgPendingFor].path);
            if (ok) imgShownFor = imgPendingFor;
            Serial.printf("[emuimg] swap done ok=%d shown=%d\n", (int)ok, imgShownFor);
        }

        // Scroll the footer text sideways if it's wider than the screen.
        tickEmulatorInfo();

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
        }
        lastBtnState = btnNow;

        if (!ps2_available()) {
            delay(1);
            continue;
        }

        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        // Do not wipe the ring here — that raced the ISR and dropped make codes

        speakerClick();
        Serial.printf("Key: 0x%02X\n", key);

        if (key == KEY_F1 || key == KEY_1 || key == KEY_RIGHT) {
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
            Serial.printf("[emuimg] nav UP sel=%d pending=%d shown=%d\n",
                          selected, imgPendingFor, imgShownFor);
        } else if ((key == KEY_DOWN || key == KEY_DOWN_EXT || key == KEY_A_NAV) && selected < menuCount - 1) {
            int oldSelected = selected;
            int oldScroll = scrollOffset;
            selected++;
            if (selected >= scrollOffset + MAX_VISIBLE) scrollOffset++;
            updateMenuSelection(oldSelected, selected, oldScroll, scrollOffset, currentPath);
            imgPendingFor = selected; imgPendingSince = millis();
            Serial.printf("[emuimg] nav DOWN sel=%d pending=%d shown=%d\n",
                          selected, imgPendingFor, imgShownFor);
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
        } else if (key == KEY_PGDN || key == KEY_P_KEY) {
            // Page down: jump forward one full page (P)
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
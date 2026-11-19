#pragma once
// ===========================================================================
// disclaimer.h - First-run legal disclaimer with accept gate (EN / PT / ES).
// Shown once until the user accepts; the accepted=1 flag is stored in bootl.rc.
// Footer menu: Left/Right to move, Space/Enter to select.
// ===========================================================================
#include "bootloader.h"

// Each language is an array of pre-wrapped lines (FONT_5x7, ~60 chars/line).
static const char* DISC_EN[] = {
    "DISCLAIMER",
    "",
    "The ESP32 Multiloader, the emulators and the other works",
    "made available here are offered free of charge, as open",
    "source, without any warranty or obligation of support,",
    "assistance or maintenance. The projects developed by",
    "ESP-WORKS and its collaborators have no link, association",
    "or endorsement to any specific board or hardware, nor any",
    "relationship with Espressif or other makers of compatible",
    "hardware. Likewise, there is no direct or indirect link",
    "with Robgo_rg, except for our commitment to seek the",
    "maximum possible compatibility with its products. The use",
    "of any software, project or resource provided is the sole",
    "responsibility of the user, who assumes all risks arising",
    "from its use.",
};

static const char* DISC_PT[] = {
    "TERMO DE ISENCAO",
    "",
    "O ESP32 Multiloader, os emuladores e os demais trabalhos",
    "aqui disponibilizados sao oferecidos gratuitamente, em",
    "regime open source, sem qualquer garantia ou obrigacao de",
    "suporte, assistencia ou manutencao. Os projetos",
    "desenvolvidos por ESP-WORKS e seus colaboradores nao",
    "possuem vinculo, associacao ou endosso a qualquer placa ou",
    "hardware especifico, tampouco qualquer relacao com a",
    "Espressif ou outros fabricantes de hardware compativel. Da",
    "mesma forma, nao existe qualquer vinculo direto ou indireto",
    "com a Robgo_rg, exceto pelo nosso compromisso de buscar a",
    "maxima compatibilidade possivel com seus produtos. O uso",
    "de qualquer software, projeto ou recurso disponibilizado e",
    "de inteira responsabilidade do usuario, que assume todos",
    "os riscos decorrentes de sua utilizacao.",
};

static const char* DISC_ES[] = {
    "AVISO LEGAL",
    "",
    "El ESP32 Multiloader, los emuladores y los demas trabajos",
    "aqui disponibles se ofrecen gratuitamente, en regimen open",
    "source, sin garantia ni obligacion de soporte, asistencia",
    "o mantenimiento. Los proyectos desarrollados por ESP-WORKS",
    "y sus colaboradores no tienen vinculo, asociacion ni",
    "respaldo a ninguna placa o hardware especifico, ni",
    "relacion alguna con Espressif u otros fabricantes de",
    "hardware compatible. Del mismo modo, no existe vinculo",
    "directo o indirecto con Robgo_rg, salvo nuestro compromiso",
    "de buscar la maxima compatibilidad posible con sus",
    "productos. El uso de cualquier software, proyecto o recurso",
    "proporcionado es responsabilidad exclusiva del usuario,",
    "que asume todos los riesgos derivados de su uso.",
};

struct DiscLang { const char** lines; int count; const char* tag; };

static const DiscLang DISC_LANGS[] = {
    { DISC_EN, (int)(sizeof(DISC_EN) / sizeof(DISC_EN[0])), "EN" },
    { DISC_PT, (int)(sizeof(DISC_PT) / sizeof(DISC_PT[0])), "PT" },
    { DISC_ES, (int)(sizeof(DISC_ES) / sizeof(DISC_ES[0])), "ES" },
};
static const int DISC_LANG_COUNT = 3;

#define DISC_LANG_EN 0
#define DISC_LANG_PT 1
#define DISC_LANG_ES 2

#define DISC_MENU_COUNT 5
#define DISC_OPT_PT     0
#define DISC_OPT_EN     1
#define DISC_OPT_ES     2
#define DISC_OPT_AGREE  3
#define DISC_OPT_DECLINE 4

static const char* const DISC_MENU[DISC_MENU_COUNT] = {
    "PORTUGUES", "ENGLISH", "ESPANOL", "I AGREE", "DECLINE",
};

static void drawDisclaimerMenu(int selected) {
    const int fy = VRES - 10;
    fillRect(0, fy - 2, HRES, 12, C_BLACK);

    int totalW = 0;
    int widths[DISC_MENU_COUNT];
    for (int i = 0; i < DISC_MENU_COUNT; i++) {
        widths[i] = (int)strlen(DISC_MENU[i]) * 5;
        totalW += widths[i];
    }
    const int gap = 8;
    totalW += gap * (DISC_MENU_COUNT - 1);
    int x = (HRES - totalW) / 2;
    if (x < 2) x = 2;

    for (int i = 0; i < DISC_MENU_COUNT; i++) {
        bool isSel = (i == selected);
        Color ink   = isSel ? C_BLACK : C_CYAN;
        Color paper = isSel ? C_GREEN : C_BLACK;
        if (isSel) fillRect(x - 1, fy - 2, widths[i] + 2, 11, paper);
        drawString(x, fy, DISC_MENU[i], ink, paper, &fabgl::FONT_5x7);
        x += widths[i] + gap;
    }
}

static void drawDisclaimer(int lang, int menuSel) {
    fillRect(0, 0, HRES, VRES, C_BLACK);
    drawKbdBadge();   // keyboard config badge in the top-left corner

    const DiscLang& L = DISC_LANGS[lang];

    // Title in yellow, body in white, 9px line height with FONT_5x7.
    int y = 13;   // one line lower than before (was 4)
    drawString(6, y, L.lines[0], C_YELLOW, C_BLACK, &fabgl::FONT_5x7);
    y += 12;
    for (int i = 1; i < L.count; i++) {
        drawString(6, y, L.lines[i], C_WHITE, C_BLACK, &fabgl::FONT_5x7);
        y += 9;
    }

    drawDisclaimerMenu(menuSel);
}

// Core disclaimer loop. Left/Right moves the footer bar; Space/Enter selects.
// When allowDecline is false (re-showing from the menu), Decline just closes.
// Redraws the header on exit so the logo the disclaimer cleared comes back.
static void runDisclaimer(bool allowDecline) {
    ps2_init();
    delay(200);
    ps2_flush();

    int lang = DISC_LANG_EN;
    int menuSel = DISC_OPT_EN;
    drawDisclaimer(lang, menuSel);

    uint32_t lastBtnMs = 0;
    bool lastBtnState = HIGH;
    Serial.printf("[DISC] magic button pin=GPIO%d initial=%d\n",
                  magicBtnPin, digitalRead(magicBtnPin));
    uint32_t dbgLast = 0;

    while (true) {
        bool btnNow = digitalRead(magicBtnPin);
        if (millis() - dbgLast > 1000) {
            dbgLast = millis();
            Serial.printf("[DISC] btn=%d\n", btnNow);
        }
        if (btnNow == LOW && lastBtnState == HIGH && (millis() - lastBtnMs) > 300) {
            lastBtnMs = millis();
            ps2_shutdown();
            kbdConfigIdx = (kbdConfigIdx + 1) % KBD_CONFIG_COUNT;
            ps2DatPin = KBD_CONFIGS[kbdConfigIdx].dat;
            ps2ClkPin = KBD_CONFIGS[kbdConfigIdx].clk;
            ps2_init();
            delay(200);
            ps2_flush();
            Serial.printf("[DISC] magic button -> %s\n", KBD_CONFIGS[kbdConfigIdx].label);
            drawKbdBadge();
        }
        lastBtnState = btnNow;

        if (!ps2_available()) { delay(1); continue; }
        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        bool isLeft  = (key == KEY_LEFT  || key == 0x6B);
        bool isRight = (key == KEY_RIGHT || key == 0x74);
        if (isLeft) {
            menuSel = (menuSel + DISC_MENU_COUNT - 1) % DISC_MENU_COUNT;
            drawDisclaimerMenu(menuSel);
            continue;
        }
        if (isRight) {
            menuSel = (menuSel + 1) % DISC_MENU_COUNT;
            drawDisclaimerMenu(menuSel);
            continue;
        }
        if (key != KEY_ENTER && key != KEY_SPACE) continue;

        if (menuSel == DISC_OPT_PT) {
            lang = DISC_LANG_PT;
            drawDisclaimer(lang, menuSel);
        } else if (menuSel == DISC_OPT_EN) {
            lang = DISC_LANG_EN;
            drawDisclaimer(lang, menuSel);
        } else if (menuSel == DISC_OPT_ES) {
            lang = DISC_LANG_ES;
            drawDisclaimer(lang, menuSel);
        } else if (menuSel == DISC_OPT_AGREE) {
            if (allowDecline) {
                blAccepted = 1;
                saveRC();
                Serial.println("[DISC] accepted");
            }
            drawHeader();
            return;
        } else if (menuSel == DISC_OPT_DECLINE) {
            if (allowDecline) {
                Serial.println("[DISC] declined -> reset");
                shutdownForReboot();
                ESP.restart();
            } else {
                drawHeader();
                return;
            }
        }
    }
}

void showDisclaimerIfNeeded() {
    if (blAccepted == 1) return;
    runDisclaimer(true);
}

void showDisclaimer() {
    runDisclaimer(false);
}
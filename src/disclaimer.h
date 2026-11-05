#pragma once
// ===========================================================================
// disclaimer.h - First-run legal disclaimer with accept gate (EN / PT / ES).
// Shown once until the user accepts; the accepted=1 flag is stored in bootl.rc.
//   L = switch language   Y = accept and continue
// ===========================================================================
#include "bootloader.h"

// Each language is an array of pre-wrapped lines (FONT_5x7, ~60 chars/line).
static const char* DISC_EN[] = {
    "DISCLAIMER",
    "",
    "The ESP32 Bootloader, the emulators and the other works",
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
    "O ESP32 Bootloader, os emuladores e os demais trabalhos",
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
    "El ESP32 Bootloader, los emuladores y los demas trabajos",
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

// Direct language keys (PS/2 set 2 make codes)
#define KEY_DISC_E  0x24   // E - English
#define KEY_DISC_P  0x4D   // P - Portugues
#define KEY_DISC_S  0x1B   // S - Espanol
#define KEY_Y_SCAN  0x35   // Y - accept
#define KEY_N_SCAN  0x31   // N - decline (reset)

// Language index order must match DISC_LANGS[] below: 0=EN, 1=PT, 2=ES.
#define DISC_LANG_EN 0
#define DISC_LANG_PT 1
#define DISC_LANG_ES 2

static void drawDisclaimer(int lang) {
    fillRect(0, 0, HRES, VRES, C_BLACK);

    const DiscLang& L = DISC_LANGS[lang];

    // Title in yellow, body in white, 9px line height with FONT_5x7.
    int y = 4;
    drawString(6, y, L.lines[0], C_YELLOW, C_BLACK, &fabgl::FONT_5x7);
    y += 12;
    for (int i = 1; i < L.count; i++) {
        drawString(6, y, L.lines[i], C_WHITE, C_BLACK, &fabgl::FONT_5x7);
        y += 9;
    }

    // Footer bar with controls (localized)
    int fy = VRES - 10;
    fillRect(0, fy - 2, HRES, 12, C_BLACK);
    const char* foot;
    if (lang == DISC_LANG_PT)
        foot = "P=Port  E=Ingl  S=Esp    Y=Concordo  N=Recusar";
    else if (lang == DISC_LANG_ES)
        foot = "P=Port  E=Ingl  S=Esp    Y=Acepto  N=Rechazar";
    else
        foot = "P=Port  E=Engl  S=Span   Y=I Agree  N=Decline";
    drawString(6, fy, foot, C_CYAN, C_BLACK, &fabgl::FONT_5x7);
}

// Core disclaimer loop. P/E/S switch language; Y accepts (saves accepted=1); N resets.
// When allowDecline is false (re-showing from the menu), N just closes without reset.
// Redraws the header on exit so the logo the disclaimer cleared comes back.
static void runDisclaimer(bool allowDecline) {
    ps2_init();
    delay(200);
    ps2_flush();

    int lang = DISC_LANG_EN;   // start in English
    drawDisclaimer(lang);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        if (key == KEY_DISC_E) {
            lang = DISC_LANG_EN; drawDisclaimer(lang);
        } else if (key == KEY_DISC_P) {
            lang = DISC_LANG_PT; drawDisclaimer(lang);
        } else if (key == KEY_DISC_S) {
            lang = DISC_LANG_ES; drawDisclaimer(lang);
        } else if (key == KEY_Y_SCAN) {
            if (allowDecline) {        // first-run accept persists the flag
                blAccepted = 1;
                saveRC();
                Serial.println("[DISC] accepted");
            }
            drawHeader();
            return;
        } else if (key == KEY_N_SCAN) {
            if (allowDecline) {
                Serial.println("[DISC] declined -> reset");
                shutdownForReboot();
                ESP.restart();
            } else {
                drawHeader();          // re-show from menu: just close
                return;
            }
        }
    }
}

// First-run gate: shows the disclaimer only if not yet accepted. Y accepts, N resets.
void showDisclaimerIfNeeded() {
    if (blAccepted == 1) return;
    runDisclaimer(true);
}

// Menu option: always show the disclaimer. Y or N just closes (no reset, no flag change).
void showDisclaimer() {
    runDisclaimer(false);
}
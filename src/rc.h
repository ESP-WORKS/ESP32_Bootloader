#pragma once
// ===========================================================================
// rc.h - Leitura e escrita de /bootl.rc na raiz do SD
//
// Formato do arquivo:
//   kbddat=32
//   kbdclk=33
//
// Extensível: chaves desconhecidas são preservadas no saveRC().
// ===========================================================================
#include "bootloader.h"

#define RC_FILE "/bootl.rc"
#define RC_MAX_LINES 32
#define RC_LINE_LEN  64

// ---------------------------------------------------------------------------
// Configurações de teclado disponíveis
// ---------------------------------------------------------------------------
struct KbdConfig {
    int         dat;
    int         clk;
    const char* badge;   // exibido no canto da tela: "C1", "C2", "C3"
    const char* label;   // log serial
};

static const KbdConfig KBD_CONFIGS[] = {
    { 32, 33, "C1", "C1: DAT=GPIO32 CLK=GPIO33 (padrao mono/stereo)" },
    { 27, 26, "C2", "C2: DAT=GPIO27 CLK=GPIO26 (alt mono/stereo)"   },
    { 27,  2, "C3", "C3: DAT=GPIO27 CLK=GPIO02 (so stereo)"         },
};
static const int KBD_CONFIG_COUNT = 3;

// Índice ativo (0-based). Definido em main.cpp.
extern int kbdConfigIdx;

// Pino do magic button. Lido do bootl.rc (chave magicb=). Default 36.
extern int magicBtnPin;

// Versão do bootloader instalada, lida da chave version= do bootl.rc.
// Usada pela auto-atualização para comparar com a versão do servidor.
// Fica no SD (não na NVS) para sobreviver a "Clear NVS" e permitir edição manual.
extern String blInstalledVersion;

// Flag de aceite do disclaimer (chave accepted= do bootl.rc). 1 = já aceitou.
extern int blAccepted;

// Tempo (em segundos) da janela de autoboot. Chave autostarttime= do bootl.rc.
// Default 3 se a chave não existir (e nesse caso é gravada).
extern int autoStartTime;

// ---------------------------------------------------------------------------
// Parse simples de "chave=valor\n"
// Retorna true se a chave foi encontrada; escreve o valor em outVal.
// ---------------------------------------------------------------------------
static bool rcParseLine(const char* line, const char* key, char* outVal, size_t outSz) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return false;
    if (line[klen] != '=') return false;
    const char* v = line + klen + 1;
    // strip trailing \r\n
    size_t vlen = strlen(v);
    while (vlen > 0 && (v[vlen-1] == '\r' || v[vlen-1] == '\n')) vlen--;
    if (vlen >= outSz) vlen = outSz - 1;
    memcpy(outVal, v, vlen);
    outVal[vlen] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// loadRC() — lê /bootl.rc e seta kbdConfigIdx + ps2DatPin/ps2ClkPin.
// Se o arquivo não existir ou os valores não baterem com nenhuma config
// conhecida, usa C1 como default e cria o arquivo.
// Deve ser chamado APÓS o SD estar montado.
// ---------------------------------------------------------------------------
void saveRC();   // forward decl — defined below

void loadRC() {
    int dat = -1, clk = -1;
    bool found = false;
    bool hadVersionKey = false;
    bool hadAutoStart = false;

    File f = SD.open(RC_FILE, FILE_READ);
    if (f) {
        char line[RC_LINE_LEN];
        while (f.available()) {
            int len = 0;
            while (f.available() && len < (int)sizeof(line) - 1) {
                char c = f.read();
                if (c == '\n') break;
                line[len++] = c;
            }
            line[len] = '\0';
            if (len == 0) continue;

            char val[32];
            if (rcParseLine(line, "kbddat",  val, sizeof(val))) dat = atoi(val);
            if (rcParseLine(line, "kbdclk",  val, sizeof(val))) clk = atoi(val);
            if (rcParseLine(line, "magicb",  val, sizeof(val))) magicBtnPin = atoi(val);
            if (rcParseLine(line, "version", val, sizeof(val))) { blInstalledVersion = String(val); hadVersionKey = true; }
            if (rcParseLine(line, "accepted", val, sizeof(val))) blAccepted = atoi(val);
            if (rcParseLine(line, "autostarttime", val, sizeof(val))) { autoStartTime = atoi(val); hadAutoStart = true; }
        }
        f.close();
        found = true;
    }

    // Se a chave version não estava no arquivo, assume a versão compilada.
    if (blInstalledVersion.length() == 0) blInstalledVersion = String(BL_VERSION);

    // Procura a config que bate com dat/clk lidos
    int idx = 0;  // default C1
    if (found && dat >= 0 && clk >= 0) {
        for (int i = 0; i < KBD_CONFIG_COUNT; i++) {
            if (KBD_CONFIGS[i].dat == dat && KBD_CONFIGS[i].clk == clk) {
                idx = i;
                break;
            }
        }
    }

    kbdConfigIdx = idx;
    ps2DatPin    = KBD_CONFIGS[idx].dat;
    ps2ClkPin    = KBD_CONFIGS[idx].clk;

    Serial.printf("RC: %s  (dat=%d clk=%d magicb=%d version=%s)\n",
                  KBD_CONFIGS[idx].label, dat, clk, magicBtnPin,
                  blInstalledVersion.c_str());
    Serial.printf("RC: found=%d hadVersionKey=%d\n", (int)found, (int)hadVersionKey);

    // Se o arquivo não existia, cria com C1 + version + autostarttime
    if (!found) {
        Serial.println("RC: bootl.rc nao encontrado, criando com C1");
        File wf = SD.open(RC_FILE, FILE_WRITE);
        if (wf) {
            wf.printf("kbddat=%d\n", KBD_CONFIGS[0].dat);
            wf.printf("kbdclk=%d\n", KBD_CONFIGS[0].clk);
            wf.printf("magicb=%d\n", magicBtnPin);
            wf.printf("version=%s\n", blInstalledVersion.c_str());
            wf.printf("autostarttime=%d\n", autoStartTime);
            wf.close();
        }
    } else if (!hadVersionKey || !hadAutoStart) {
        // Arquivo existia mas sem version= e/ou autostarttime=: adiciona preservando o resto.
        Serial.println("RC: adicionando chaves faltantes ao bootl.rc existente");
        saveRC();
    }
}

// ---------------------------------------------------------------------------
// saveRC() — reescreve /bootl.rc com os valores atuais de kbdConfigIdx.
// Preserva linhas com chaves desconhecidas (extensível para futuros GPIOs).
// ---------------------------------------------------------------------------
void saveRC() {
    // 1. Lê todas as linhas existentes (para preservar chaves desconhecidas)
    char lines[RC_MAX_LINES][RC_LINE_LEN];
    int  lineCount = 0;
    bool hadDat = false, hadClk = false, hadMagic = false, hadVersion = false, hadAccepted = false, hadAutoStart = false;

    File rf = SD.open(RC_FILE, FILE_READ);
    if (rf) {
        while (rf.available() && lineCount < RC_MAX_LINES) {
            int len = 0;
            while (rf.available() && len < RC_LINE_LEN - 1) {
                char c = rf.read();
                if (c == '\n') break;
                lines[lineCount][len++] = c;
            }
            lines[lineCount][len] = '\0';
            if (len > 0 && lines[lineCount][len-1] == '\r') lines[lineCount][--len] = '\0';
            if (len == 0) continue;
            lineCount++;
        }
        rf.close();
    }

    // Substitui kbddat/kbdclk/magicb inline; marca se encontrou
    for (int i = 0; i < lineCount; i++) {
        char val[16];
        if (rcParseLine(lines[i], "kbddat", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "kbddat=%d", KBD_CONFIGS[kbdConfigIdx].dat);
            hadDat = true;
        } else if (rcParseLine(lines[i], "kbdclk", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "kbdclk=%d", KBD_CONFIGS[kbdConfigIdx].clk);
            hadClk = true;
        } else if (rcParseLine(lines[i], "magicb", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "magicb=%d", magicBtnPin);
            hadMagic = true;
        } else if (rcParseLine(lines[i], "version", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "version=%s", blInstalledVersion.c_str());
            hadVersion = true;
        } else if (rcParseLine(lines[i], "accepted", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "accepted=%d", blAccepted);
            hadAccepted = true;
        } else if (rcParseLine(lines[i], "autostarttime", val, sizeof(val))) {
            snprintf(lines[i], RC_LINE_LEN, "autostarttime=%d", autoStartTime);
            hadAutoStart = true;
        }
    }

    // Adiciona chaves que não existiam
    if (!hadDat   && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "kbddat=%d", KBD_CONFIGS[kbdConfigIdx].dat);
    if (!hadClk   && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "kbdclk=%d", KBD_CONFIGS[kbdConfigIdx].clk);
    if (!hadMagic && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "magicb=%d", magicBtnPin);
    if (!hadVersion && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "version=%s", blInstalledVersion.c_str());
    if (!hadAccepted && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "accepted=%d", blAccepted);
    if (!hadAutoStart && lineCount < RC_MAX_LINES)
        snprintf(lines[lineCount++], RC_LINE_LEN, "autostarttime=%d", autoStartTime);

    // 4. Reescreve o arquivo
    if (SD.exists(RC_FILE)) SD.remove(RC_FILE);
    File wf = SD.open(RC_FILE, FILE_WRITE);
    if (wf) {
        for (int i = 0; i < lineCount; i++) wf.printf("%s\n", lines[i]);
        wf.close();
        Serial.printf("RC: bootl.rc salvo com %s\n", KBD_CONFIGS[kbdConfigIdx].label);
    } else {
        Serial.println("RC: ERRO ao salvar bootl.rc");
    }
}
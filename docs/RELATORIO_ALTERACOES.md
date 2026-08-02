# Relatório de alterações — ESP32 Bootloader (estabilidade VGA / SD / OTA / WiFi)

**Projeto:** ESP32 Bootloader (TTGO VGA32 + FabGL)  
**Versão de referência:** 0.6.0a  
**Data:** 2026-08-02  
**Documento técnico complementar:** `docs/VGA_STABILITY.md`

---

## 1. Objetivo

Melhorar estabilidade da UI VGA, montagem do SD em placas distintas, OTA a partir do cartão, teclado PS/2 e, principalmente, eliminar o **flicker durante download WiFi** mantendo progresso visível e boa velocidade.

---

## 2. Resumo executivo

| Área | Resultado |
|------|-----------|
| Menu / splash VGA | Menos spam de primitivas; countdown e navegação estáveis |
| SD Card | Auto-detecção MISO (GPIO2 / GPIO35) via eFuse + fallback |
| OTA (SD → flash) | **Sem flicker** com VGA ligada; SD→PSRAM→flash; progresso ~10% |
| WiFi download | **Sem flicker** com download no core 0 e UI no core 1; barra linear |
| PS/2 | Ring buffer com critical section |
| Shutdown / docs | VGA encerrada antes do reboot; README alinhado ao `partitions.csv` |

---

## 3. Alterações por área

### 3.1 UI do menu e splash (`src/main.cpp`)

| Item | O que mudou | Por quê |
|------|-------------|---------|
| Countdown autoboot | Só redesenha quando o segundo muda; `x` dentro de 320 px | Evitava inundar a fila FabGL e overflow na borda |
| Idle do menu | `delay(1)` quando não há tecla | Evita busy-spin a 100% CPU |
| `statusY_original` | Passou a `MENU_Y_START` (68) | Evitava texto sobre o header após F2 |
| Logo | `drawBitmap` RGBA2222 (1 primitiva) em vez de ~3600 `setPixel` | Splash mais leve |
| `queueSize` | 128 → **512** | Margem para UI sem estourar a fila |
| Menu UP/DOWN | Redesenho incremental (linha antiga + nova) | Menos flicker ao navegar |

### 3.2 SD Card — auto-detecção de GPIOs (`src/main.cpp`)

Portado do ESPectrum (`FileUtils` / `hardpins.h`):

- CLK=14, MOSI=12, CS=13 (fixos)
- MISO = **GPIO2** (TTGO / LILYGO / PICOD4) ou **GPIO35** (WROVER / Olimex)
- Escolha inicial via `esp_efuse_get_pkg_ver()`
- Fallback cruzado 2↔35 se o mount falhar
- Status na tela: `SD Card FOUND (MISO=GPIOx)`

### 3.3 OTA a partir do SD (`src/main.cpp` — `doOTA`)

| Item | Detalhe |
|------|---------|
| Caminho preferido | SD → PSRAM (chunks 64 KB) → `esp_ota_write` |
| Fallback | Streaming SD→flash (16 KB) se não houver PSRAM |
| Progresso | Atualização a cada ~10% (`otaProgressMaybe`) na VGA |
| VGA durante flash | Default `OTA_SUSPEND_VGA_DURING_FLASH=0` — VGA permanece ligada |
| Opção de emergência | `-DOTA_SUSPEND_VGA_DURING_FLASH=1` apaga VGA só na gravação (não necessário após validação) |
| Validações | Arquivo vazio, maior que `ota_0`, escrita incompleta |

#### Resultado validado em hardware

- OTA pelo SD **sem flicker** perceptível com VGA ligada  
- Barra de progresso visível (degraus ~10%) durante leitura e gravação  
- Suspender a VGA **não é necessário** neste fluxo (diferente do caso WiFi, onde a separação de cores foi o fator decisivo)

### 3.4 Flicker no download WiFi — solução final (`src/updater.h`, `platformio.ini`)

> Abaixo está **apenas a etapa que resolveu** o flicker de forma satisfatória (VGA ligada, progresso visível, boa velocidade).

#### Diagnóstico (contexto)

No ESP32 clássico, o sinal VGA exige DMA contínuo. O stack WiFi e o DMA de vídeo competem por CPU/barramento no mesmo chip. Pintar a barra no mesmo core do download piorava o sintoma.

#### Solução adotada

1. **Task de download** (`wifi_dl`) pinada no **core 0** (mesmo core típico do WiFi).
2. **UI / Canvas / barra** no **core 1** (Arduino `setup`/`loop` + tasks de vídeo da FabGL).
3. VGA **permanece ligada** durante o download (sem `DisplayController.end()`).
4. Worker publica progresso em `volatile offset` **enquanto lê bytes**; a UI **só consulta** esse valor (não bloqueia o download).
5. Barra redesenhada a cada **1%** (poll ~100 ms) → avanço linear.
6. Chunks HTTP Range grandes (**256 KB**) para manter throughput.
7. `WIFI_PS_NONE` na conexão; WiFi desligado (`WIFI_OFF`) ao terminar o updater.

#### Flags atuais (`platformio.ini`)

```ini
-DWIFI_DL_CORE=0
-DWIFI_DL_CHUNK=262144
-DWIFI_UI_POLL_MS=100
```

#### Resultado validado em hardware

- Sem flicker perceptível na VGA durante o download  
- Progresso visível e linear  
- Velocidade aceitável com chunks grandes + UI não bloqueante  

### 3.5 Outros ajustes do updater (`src/updater.h`)

- Labels de arquivo `nome (n/m)` durante o download  
- Timeout de stall / watchdog se o worker parar de avançar bytes  
- Ao fim: `WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)`  

### 3.6 PS/2 (`src/main.cpp`)

- `portMUX` entre ISR e main (`flush` / `available` / `pop`)  
- Ring cheio descarta byte (não sobrescreve sem controle)  
- Removido o wipe bruto do buffer após cada tecla no menu  
- `ps2_flush()` só no init e ao sair de F1/F2  
- Resync se `ps2_bit > 11`  

### 3.7 Shutdown limpo e documentação

| Item | Detalhe |
|------|---------|
| `shutdownForReboot()` | PS2 → `DisplayController.end()` → SD/HSPI antes de `ESP.restart()` |
| README | Partições alinhadas ao `partitions.csv`: factory `0x120000` @ `0x10000`, ota_0 `0x2C0000` @ `0x130000` (máx. **2816 KB**) |
| Código morto removido | `wifiInit`, stubs de painel/`infoText`, includes WiFi órfãos no `main` |

---

## 4. Arquivos principais tocados

| Arquivo | Papel |
|---------|--------|
| `src/main.cpp` | VGA UI, SD auto, OTA, PS/2, shutdown |
| `src/updater.h` | WiFi updater + download multi-core + barra live |
| `platformio.ini` | Flags `WIFI_DL_*` |
| `partitions.csv` | Layout real (já existente; README corrigido) |
| `README.md` | Partições e nota PS/2 |
| `revision.txt` | Histórico de versão |
| `docs/VGA_STABILITY.md` | Log técnico detalhado |
| `docs/RELATORIO_ALTERACOES.md` | Este relatório |

---

## 5. Como validar (checklist)

1. Boot — logo e menu estáveis; countdown sem flicker  
2. SD — mensagem com MISO correto (2 ou 35)  
3. **OTA** de um emulador — VGA ligada, sem flicker; progresso ~10%; flash ok  
4. **F2** — download sem flicker; barra ~linear; Serial com `[DL] worker START core=0` e UI no core 1  
5. Teclado — UP/DOWN/ENTER/F1/F2 após cold boot  
6. Boot do emulador — handover sem hang  

---

## 6. Pendências / melhorias futuras (opcional)

- Validar em mais hosts HTTP (com e sem `Accept-Ranges`)  
- Ajustar `WIFI_DL_CHUNK` / `WIFI_UI_POLL_MS` se mudar o perfil de rede  
- Driver PS/2 da FabGL, se ainda houver edge cases de teclado  

---

## 7. Conclusão

As mudanças cobrem estabilidade da UI VGA, compatibilidade de SD, OTA e teclado mais robusto.

- **WiFi:** flicker resolvido pela **separação de cores** (download no core 0, UI no core 1) com progresso live não bloqueante.  
- **OTA (SD):** validado **sem flicker** com VGA ligada — buffer PSRAM + progresso raro (~10%); suspender a VGA ficou apenas como opção de emergência (`=1`), não como default.

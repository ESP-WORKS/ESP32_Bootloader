# Estabilidade VGA — ESP32 Bootloader

Documentação do diagnóstico e das alterações feitas para reduzir flicker, sparkles e glitches na saída VGA do bootloader (TTGO VGA32 + FabGL).

## Arquitetura

```
App (Canvas) → fila de primitivas FabGL (queueSize)
     → VGA16Controller (framebuffer 4 bpp, paleta 16 cores)
     → DMA / I2S → GPIO RGB222 + HSYNC / VSYNC
```

| Item | Valor |
|------|--------|
| Resolução | `QVGA_320x240_60Hz` (DoubleScan) |
| Controller | `fabgl::VGA16Controller` |
| Pins VGA (default FabGL / TTGO) | R 22/21, G 19/18, B 5/4, HSYNC 23, VSYNC 15 |
| SD (HSPI) | CLK 14, MOSI 12, CS 13; MISO **2** (TTGO/LILYGO) ou **35** (WROVER/Olimex) — auto via eFuse + fallback |
| PS/2 | DAT 32, CLK 33 |

**Nota importante:** SD e VGA **não compartilham o mesmo canal DMA**. O conflito observado é de **barramento de memória / CPU / flash cache** enquanto o DMA de vídeo continua lendo o framebuffer.

## Sintomas → causa → mitigação

| Sintoma | Causa provável | Mitigação |
|---------|----------------|-----------|
| Flicker no menu com countdown | `drawString` em busy-loop (milhares/s) | Redesenhar só quando o segundo muda + `delay` no idle |
| Lixo na borda direita | Texto em x=300 estoura HRES=320 | Calcular `x = HRES - textW - 4` |
| CPU a 100%, UI irregular | Idle sem yield (`continue` puro) | `delay(1)` nos loops de espera |
| Texto sobre o header após F2 | `statusY_original = 43` | Resetar para `MENU_Y_START` |
| Sparkles durante OTA | SD + flash + redraw a cada 4 KB | Fase 3: UI rara + PSRAM buffer (planejado) |
| Flicker no WiFi update | Rádio + modem sleep + Canvas | Fase 4: `WIFI_PS_NONE` (planejado) |
| Splash lento / engasgo | Logo com 3600× `setPixel` + fila 128 | Fase 2: Bitmap / queueSize (planejado) |

## Plano de fases

| Fase | Conteúdo | Status |
|------|----------|--------|
| 0 | Este documento | Feito |
| 1 | Countdown, idle delay, statusY | Feito (Sessão A) |
| SD | Auto-detecção MISO (ESPectrum) | Feito |
| 2 | Logo Bitmap / queueSize / menu incremental | Feito (Sessão B) |
| 3 | OTA: UI estática, SD→PSRAM→flash + suspend VGA no flash | Feito (C + F) |
| 4 | WiFi updater (power-save off) | Feito (Sessão D) |
| 5 | `DisplayController.end()` no boot, README partições | Feito (Sessão E) |
| — | PS/2 + 3.4 suspend flash + dead code | Feito (Sessão F) |

---

## Changelog

### Passo 5.1 — Shutdown VGA limpo antes do reboot

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`shutdownForReboot`, `bootEmulator*`, maintenance, retries)
- **Problema observado:** Reboot com DMA/I2S VGA ainda ativos; PS2/SD liberados, vídeo não.
- **Causa:** Falta `DisplayController.end()` no caminho de saída.
- **Mudança:** Helper `shutdownForReboot()` — PS2 → `DisplayController.end()` → speaker GPIO → SD/HSPI → delay; usado em boots OTA, menu maintenance, retry SD e restart de erro.
- **Por quê:** Para o stream VGA de forma ordenada antes do `ESP.restart()`, deixando GPIOs/periféricos em estado limpo para o emulador.
- **Como testar:** Boot direto e pós-flash para o emulador; reset via F1→R; sem hang no handover.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Remover `DisplayController.end()` do helper.

### Passo 5.2 — README alinhado a `partitions.csv`

- **Data:** 2026-08-02
- **Arquivos:** `README.md`
- **Problema observado:** Docs ainda citavam factory `0x90000` / ota_0 `@0xA0000` / 3392KB.
- **Causa:** Tabela antiga comentada no CSV; README não foi atualizado com o layout atual.
- **Mudança:** Documentar factory `0x120000` @ `0x10000`, ota_0 `0x2C0000` @ `0x130000` (máx. **2816KB**).
- **Por quê:** Evitar bins de emulador grandes demais ou offsets errados ao adaptar projetos.
- **Como testar:** Conferir `partitions.csv` ↔ seção 5/6 do README.
- **Resultado no hardware:** N/A (doc)
- **Rollback:** Restaurar texto antigo (incorreto).

### Passo 5.4 — `revision.txt`

- **Data:** 2026-08-02
- **Arquivos:** `revision.txt`
- **Mudança:** Entrada `0.6.0a` resumindo a série de estabilidade VGA + SD + OTA + WiFi + shutdown.
- **Por quê:** Histórico de versão legível fora do doc técnico.

### Passo 4.5 — Teste A/B: download no core 0, UI no core 1

- **Data:** 2026-08-02
- **Arquivos:** `src/updater.h`, `platformio.ini`
- **Resultado inicial:** **Sem flicker** com chunks 16 KB + hold 400 ms — mas download lento.
- **Resultado:** sem flicker com DL@core0 / UI@core1.
- **Retune velocidade (flicker resolvido pelo split de core, não por sleep):**
  - `WIFI_DL_CHUNK=262144` (256 KB)
  - `WIFI_PROGRESS_HOLD_MS=20`
  - `WIFI_UI_EVERY_N_CHUNKS=3` (1º + último sempre; sem wait nos demais)
  - Rádio **sempre acordada** durante o arquivo (sem modem sleep por chunk)
  - TX mais alto (`60`)
- **Se flicker voltar:** `-DWIFI_DL_CHUNK=131072` ou `-DWIFI_UI_EVERY_N_CHUNKS=1`
- **Se ainda lento e estável:** `-DWIFI_DL_CHUNK=524288`

### Passo 4.4 — Separar WiFi e VGA no tempo (VGA sempre ligada)

- **Data:** 2026-08-02 (revisto: **sem** `DisplayController.end()` — tela não apaga)
- **Arquivos:** `src/updater.h`
- **Problema observado:** Apagar a VGA esconde o progresso; deixar WiFi+VGA juntos causa flicker contínuo.
- **Causa:** No mesmo chip, rádio e DMA de vídeo interferem se ativos juntos. Separação = **tempo**, não desligar o monitor.
- **Mudança:**
  1. **VGA permanece sempre ON** (framebuffer + sync contínuos).
  2. Download em bursts HTTP `Range` (~32 KB):
     - `updaterRadioWakeForBurst()` → GET Range → lê chunk
     - `updaterRadioIdleForDisplay()` (`WIFI_PS_MAX_MODEM` + sleep) → rádio quieta
     - pinta barra + `delay(WIFI_PROGRESS_HOLD_MS)` com imagem estável
  3. TX reduzido só no burst; resto do tempo modem sleep.
  4. Sem Range no servidor → one-shot (pode haver flicker nesse arquivo; aviso na tela).
- **Por quê:** Progresso sempre visível; contenção só em janelas curtas de rádio; entre bursts a UI fica limpa.
- **Expectativa:** Possível sparkle **breve** durante cada burst; nos ~350 ms de hold a tela deve ficar estável.
- **Ajustes:** `-DWIFI_DL_CHUNK=16384` (bursts menores), `-DWIFI_PROGRESS_HOLD_MS=500`.
- **Como testar:** F2 → Y — tela **não** apaga; barra sobe; Serial `burst ok` / `UI ... [radio idle]`.
- **Resultado no hardware:** _(preencher após teste)_

### Passo 4.1 — WiFi sem modem sleep (`WIFI_PS_NONE`)

- **Data:** 2026-08-02
- **Arquivos:** `src/updater.h` (`updaterWifiConnect`)
- **Problema observado:** Flicker/jitter na VGA durante F2 (updater) com WiFi ativo.
- **Causa:** Modem sleep / WiFi power-save gera interrupções irregulares que afetam o timing do DMA VGA (FabGL).
- **Mudança:** `WiFi.mode(WIFI_STA)`, `WiFi.setSleep(false)`, `esp_wifi_set_ps(WIFI_PS_NONE)` antes, depois do `begin` e após conectar.
- **Por quê:** Mantém a rádio acordada de forma estável enquanto a UI VGA está ligada.
- **Como testar:** F2 com `wificonfig.rc` — conectar e baixar com menos flicker que antes.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Remover as chamadas de power-save.

### Passo 4.2 — Progress de download a cada ~10% + timeout

- **Data:** 2026-08-02
- **Arquivos:** `src/updater.h` (`updaterDownloadFile`)
- **Problema observado:** (já parcialmente ok) + risco de hang se o stream parar.
- **Causa:** Loop sem yield/timeout com VGA ainda ativa.
- **Mudança:** Progress em dezenas de %; `delay(1)` quando sem dados; timeout 30 s sem bytes → falha.
- **Por quê:** Menos Canvas durante rádio; evita travar o menu se a rede cair.
- **Como testar:** Download normal; simular rede ruim (opcional) e ver falha em ~30 s.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Loop antigo sem timeout.

### Passo 4.3 — Label do arquivo + desligar WiFi ao terminar

- **Data:** 2026-08-02
- **Arquivos:** `src/updater.h` (`runUpdater`)
- **Problema observado:** Nome do arquivo em download estava comentado (`// AQUIIIII`); WiFi ficava ligado após o updater.
- **Causa:** Label comentado por conflito com a barra; rádio residual continua a competir com VGA.
- **Mudança:** Mostrar `dest (n/m)` numa linha e progress na seguinte (reuso por arquivo); ao fim `WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)`.
- **Por quê:** UX legível sem spam de UI; libera a rádio depois do update.
- **Como testar:** F2 — ver nome do arquivo + barra; ao voltar ao menu, Serial sem WiFi ativo.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Comentar de novo o label; só `WiFi.disconnect()`.

### Passo 3.1 — Progress OTA raro (a cada ~10%)

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`otaProgressMaybe`, `doOTA`)
- **Problema observado:** `drawProgress` a cada chunk de 4 KB durante SD+flash → sparkles/flicker.
- **Causa:** Canvas + SPI SD + erase/write de flash competindo com o DMA VGA no barramento.
- **Mudança:** Helper `otaProgressMaybe` só redesenha quando a dezena do percentual muda (0, 10, 20…100).
- **Por quê:** Mantém feedback visual sem spam de primitivas no pior momento de I/O.
- **Como testar:** Flashar um emulador — barra salta de 10 em 10%; menos “neve” na tela.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Chamar `drawProgress` a cada chunk.

### Passo 3.2 / 3.3 — SD → PSRAM → flash (+ fallback streaming)

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`doOTA`)
- **Problema observado:** Leitura SD e `esp_ota_write` intercalados maximizam contenção com o vídeo.
- **Causa:** Dois picos de I/O (SD + flash) ao mesmo tempo que o DMA lê o framebuffer.
- **Mudança:**
  1. Se houver PSRAM e `ps_malloc(fileSize)` ok → ler o bin inteiro do SD para PSRAM (chunks 64 KB), fechar o arquivo, depois gravar OTA a partir da PSRAM.
  2. Se PSRAM indisponível/insuficiente → fallback streaming SD→flash com chunks de 16 KB (ou 4 KB se `malloc` falhar).
  3. Validação: arquivo vazio, tamanho > `ota_0`, escrita incompleta.
- **Por quê:** Separa o pico de SD do pico de flash; mesmo padrão já usado no WiFi updater.
- **Como testar:**
  1. Com PSRAM: Serial/status `Loading SD -> PSRAM` depois `Flashing from PSRAM`.
  2. Sem PSRAM (ou bin enorme): `Streaming from SD...` e flash ok.
  3. Emulador sobe após reboot.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Loop antigo read 4 KB → `esp_ota_write` → `drawProgress`.

### Passo 3.4 — Suspend VGA durante flash (implementado)

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`doOTA`, `OTA_SUSPEND_VGA_DURING_FLASH`)
- **Problema observado:** Sparkle residual possível só na fase `esp_ota_write` (flash cache).
- **Causa:** DMA VGA + erase/write flash no mesmo barramento.
- **Mudança:** Default `OTA_SUSPEND_VGA_DURING_FLASH=0` (VGA ligada + progresso ~10%). Com `=1`: após SD→PSRAM (ou antes do stream flash), `DisplayController.end()`; progresso só no Serial; ao terminar `begin` + `setResolution` + `drawHeader`.
- **Por quê:** Zero contenção de vídeo no pior I/O; trade-off = tela preta durante o flash.
- **Como testar:** Flashar emulador — tela apaga na fase flash; Serial mostra `%`; depois UI volta com `FLASHED OK`.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** build flag `0` ou remover o bloco `#if OTA_SUSPEND_VGA_DURING_FLASH`.

### Passo PS/2 — Critical section + sem wipe do ring

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (driver PS/2, `runMenu`, empty-menu loop)
- **Problema observado:** Teclado inconsistente (known issue README).
- **Causa:** ISR e main mexiam em `head`/`tail` sem exclusão mútua; após cada tecla o menu zerava o buffer (corrida + perda de bytes).
- **Mudança:** `portMUX` no ISR/`pop`/`flush`/`available`; ring cheio descarta; `ps2_flush()` só no init e ao sair de F1/F2; removido reset bruto pós-tecla; recuperação se `ps2_bit > 11`.
- **Por quê:** Elimina a corrida clássica ISR↔main do bit-bang PS/2.
- **Como testar:** Navegar menu, F1, F2, ENTER — teclas estáveis após cold boot; menos necessidade de power-cycle.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Restaurar acessos diretos a `ps2_head`/`ps2_tail`.

### Passo limpeza — Código morto

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp`
- **Mudança:** Removidos `wifiInit`/`wifiIP`, `infoBitmap`/`lastBitmapPath`, `PANEL_*`, `infoText`/`loadInfoText`, `drawStrings` comentado, includes WiFi órfãos no `main` (permanecem em `updater.h`).
- **Por quê:** Menos ruído e heap/estática desperdiçada; WiFi do updater já é o caminho real.

### Passo 2.1 — Logo via `drawBitmap` (RGBA2222)

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`ensureLogoBitmap`, `drawLogo`)
- **Problema observado:** Splash com ~3600× `setPixel` + `setPenColor` inundava a fila FabGL.
- **Causa:** Uma primitiva por pixel; com `queueSize` baixo o desenho engasgava.
- **Mudança:** Converter `logo_data` (--RRGGBB) uma vez para buffer RGBA2222 (AABBGGRR) e desenhar com um único `cv.drawBitmap`.
- **Por quê:** Uma primitiva no splash em vez de milhares; mesmas cores RGB222.
- **Como testar:** Boot — logo nítido, sem atraso/flicker no splash.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Restaurar loop `setPixel` antigo.

### Passo 2.2 — `queueSize` 128 → 512

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`setup`)
- **Problema observado:** Fila 128 era apertada para UI (header, status, menu).
- **Causa:** Cada `drawText`/`fillRect` consome slot; overflow bloqueia a task de primitivas.
- **Mudança:** `BitmappedDisplayController::queueSize = 512`.
- **Por quê:** Margem confortável sem o custo de 1024+ do default típico da FabGL; logo/menu já reduziram pressão.
- **Como testar:** Navegar menu + status lines sem engasgo; observar heap no Serial se quiser.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Voltar a 128 (não recomendado) ou 256 como meio-termo.

### Passo 2.3 — Menu incremental na navegação

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`drawMenuRow`, `drawMenuCounter`, `updateMenuSelection`, `runMenu`)
- **Problema observado:** Cada UP/DOWN limpava e redesenhava a lista inteira → flicker.
- **Causa:** `fillRect` full-area + N linhas a cada tecla.
- **Mudança:** Com scroll estável, redesenhar só a linha antiga e a nova + contador `n/m`. Scroll muda → redraw completo. `currentVersion` passado por parâmetro (sem reabrir NVS a cada frame).
- **Por quê:** Menos primitivas e menos “piscar” ao navegar.
- **Como testar:** UP/DOWN na lista — só a seleção se move; ao rolar além de `MAX_VISIBLE`, redraw full ok.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Sempre chamar `drawMenu` full no UP/DOWN.

### Passo SD — Auto-detecção de GPIOs do SD-CARD

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp`
- **Origem:** ESPectrum 1.4.15b — `FileUtils::initFileSystem()` + `hardpins.h`
- **Problema observado:** MISO fixo em GPIO2 falha em placas WROVER/Olimex (MISO=GPIO35). Em alguns chips/placas o pinout “óvio” não monta o cartão.
- **Causa:** Só o MISO muda entre variantes; CLK=14, MOSI=12, CS=13 são comuns. O package do chip (`esp_efuse_get_pkg_ver()`) indica o set preferido.
- **Mudança:**
  - `initSDAuto()` / `tryMountSD(miso)` com Arduino `SD` + `SPIClass(HSPI)`
  - Package `1` (D0WDQ5/WROVER) → tenta MISO=35 primeiro; demais → MISO=2
  - Se falhar, fallback cruzado 2↔35 (libera bus entre tentativas)
  - Status na tela: `SD Card FOUND (MISO=GPIOx)`
- **Por quê:** Mesma correção já validada no ESPectrum; o bootloader precisa montar o SD em TTGO e variantes compatíveis antes de qualquer menu/OTA.
- **Como testar:**
  1. TTGO VGA32: deve montar com MISO=GPIO2 (pkg 5 ou 0)
  2. Serial: linhas `SD: pkg=... try MISO=...` e `mounted OK`
  3. Em placa WROVER/Olimex: MISO=GPIO35 (ou fallback)
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Voltar a `spiSD.begin(14, 2, 12, 13)` + `SD.begin` fixo.

### Passo 0 — Documentação base

- **Data:** 2026-08-02
- **Arquivos:** `docs/VGA_STABILITY.md`
- **Problema observado:** Falta de registro do diagnóstico e do plano.
- **Causa:** N/A (processo).
- **Mudança:** Criado este documento com arquitetura, tabela de sintomas e plano.
- **Por quê:** Rastrear o *porquê* de cada alteração e facilitar rollback/revisão.
- **Como testar:** N/A
- **Resultado no hardware:** —
- **Rollback:** Remover o arquivo.

### Passo 1.1 — Autoboot: não spammar a fila FabGL

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`runMenu`)
- **Problema observado:** Com emulador já instalado, o countdown rodava em busy-loop chamando `drawString` a cada iteração, inundando `queueSize = 128`.
- **Causa:** Contenção da task de primitivas da FabGL / CPU.
- **Mudança:** Guardar `lastSecsLeft` e só redesenhar quando o valor em segundos muda.
- **Por quê:** Elimina milhares de primitivas/s desnecessárias — principal suspeito de flicker no menu.
- **Como testar:** Boot com firmware já na NVS; observar countdown 10→1 sem flicker.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Restaurar o `drawString` incondicional no loop.

### Passo 1.2 — Countdown com X seguro dentro de HRES

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`runMenu`)
- **Problema observado:** `drawString(300, …)` + `"10s "` (fonte 6×12) terminava em x≈324 > 320.
- **Causa:** Overflow horizontal do texto no framebuffer.
- **Mudança:** `textX = HRES - strlen*6 - 4`; limpar retângulo fixo ao atualizar/cancelar.
- **Por quê:** Evita artefato/corrupção na margem direita.
- **Como testar:** Countdown visível no canto superior direito da área do menu, sem lixo na borda.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Voltar a x fixo 300 (não recomendado).

### Passo 1.3 — Yield nos idle loops

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp` (`runMenu`, loop “No emulators found”)
- **Problema observado:** `if (ps2_head == ps2_tail) continue;` sem delay — core a 100%.
- **Causa:** Busy-spin atrasa outras tasks (WiFi / primitivas FabGL).
- **Mudança:** `delay(1)` antes do `continue` nos waits de teclado ociosos.
- **Por quê:** Devolve tempo ao FreeRTOS sem prejudicar a latência do teclado.
- **Como testar:** Menu responsivo; consumo de CPU menor (opcional: monitor).
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Remover os `delay(1)`.

### Passo 1.4 — `statusY_original` alinhado ao menu

- **Data:** 2026-08-02
- **Arquivos:** `src/main.cpp`
- **Problema observado:** Após F2 (updater), `statusY = statusY_original` com valor 43 desenhava sobre o header.
- **Causa:** Constante desatualizada (área do header, não do menu).
- **Mudança:** `statusY` / `statusY_original` inicializados com `MENU_Y_START` (68).
- **Por quê:** Status pós-updater não sobrescreve logo/versão/ajuda.
- **Como testar:** F2 → updater → tecla → menu; header intacto.
- **Resultado no hardware:** _(preencher após teste)_
- **Rollback:** Restaurar `43` (incorreto visualmente).

---

## Próximos passos

Série de estabilidade concluída. Melhorias futuras possíveis:

1. Driver PS/2 da FabGL em vez do bit-bang (se ainda houver edge cases).
2. Remover magic `WRITE_PERI_REG` no `ps2_shutdown` se o reset de GPIO bastar.
3. Atualizar “Known issues” do README sobre PS/2 após validação em hardware.

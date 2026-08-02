<img src="https://alternativebits.com/esp32/logo_app.png" alt="ESP32 Logo"/>

**NEW VERSION 0.2.0**

Allows **multiple emulators on a single card** . Place the `firmware.bin` and `version.txt` files inside folders in the root directory of the SD card (for example: `MSX`,`ESPectrum`, `CPCESP`, etc.), and a new menu will be displayed with the option to choose which emulator will be flashed/executed.

If you prefer to use one card for each emulator, simply place `firmware.bin`  and `version.txt` in the root directory of the SD card, and the flashing process will run when you swap cards.



If you use a single card for multiple emulators, don't forget to remove `firmware.bin` and `version.txt` from the root directory of the SD card.

## What is this ???

The **ESP32 Bootloader** is a firmware for the **TTGO VGA32 v1.4** (or any ESP32) that turns the ESP32 into an emulator loader (or any other software) via SD card.

The idea is simple: instead of needing a computer to flash a different emulator onto the ESP32 every time, you simply insert an SD card with the desired firmware, power up the device, and it does everything automatically — checks if the firmware is new, flashes it if necessary, and starts the emulator.

The bootloader occupies the `factory` partition of the ESP32. Emulators are flashed to the `ota_0` partition. Next time you power up with the same firmware, it goes directly to the emulator without going through the bootloader. If you swap the card, it loads the new firmware. Pretty much like Multicore does.

> ⚠️ **WARNING: This project is in ULTRA SUPER BETA stage.** Use at your own risk, bugs are expected, things will break, and everything can change at any time. You have been warned! 😄

<img src='https://alternativebits.com/esp32/cards.png' width="500px">
Organize your emulators like this !

## Table of Contents

* [1. How does it work?](#1-how-does-it-work)

* [2. How to flash the bootloader?](#2-how-to-flash-the-bootloader)

* [3. Where to find the emulators?](#3-where-to-find-the-emulators)

* [3.1 How to extract the correct firmware.bin](#31-how-to-extract-the-correct-firmwarebin)

* [4. Known issues](#4-known-issues)

* [5. How to adapt any project to work with the bootloader](#5-How-to-adapt-any-project-to-work-with-the-bootloader)

* [6. Bootloader partition table (for reference)](#6-Bootloader-partition-table)

* [7. ULTRA SUPER BETA](#7-ultra-super-beta)

* [8. Credits](#8-credits)

* [9. If you liked it](#9-if-you-liked-it-you-know-what-to-do)

* [10. License](#10-License)

***

## 1. How does it work?

### Boot flow

```
Power on ESP32
    │
    ▼
Display splash screen (ESP32 BOOTLOADER)
    │
    ▼
Check SD card
    ├── Not found → press any key to retry
    └── Found
            │
            ▼
        firmware.bin + version.txt in root?
            │
            ├── Yes → single-emulator mode
            │           │
            │           ▼
            │       Version matches stored?
            │           ├── Yes → boot directly to emulator ✅
            │           └── No  → flash firmware.bin → boot to emulator ✅
            │
            └── No  → multi-emulator mode
                        │
                        ▼
                    Scan all folders on SD card
                        │
                        ▼
                    For each folder with firmware.bin + version.txt
                        → add to menu (folder name)
                        │
                        ▼
                    Show menu (UP/DOWN/ENTER via PS2)
                    Cursor starts on the currently installed emulator (*)
                        │
                        ▼
                    User selects an entry
                        │
                        ▼
                    Version matches stored?
                        ├── Yes → boot directly to emulator ✅
```

### SD card structure

### Single-emulator mode (files in SD root)

| File           | Description                                                            |
| -------------- | ------------------------------------------------------------------------ |
| `firmware.bin` | The emulator firmware                                                  |
| `version.txt`  | A simple text with the firmware name/version (e.g.: `ESPectrum_1.4.5`) |

### Multi-emulator mode (one folder per emulator)

```
/SD card root
├── ESPectrum/
│   ├── firmware.bin
│   └── version.txt
├── CPCESP/
│   ├── firmware.bin
│   └── version.txt
├── MSPX/
│   ├── firmware.bin
│   └── version.txt
└── ...
```

| File/Folder      | Description                                                              |
| ---------------- | ------------------------------------------------------------------------ |
| `<FolderName>/`   | One folder per emulator. The folder name is what shows up in the menu  |
| `firmware.bin`    | The emulator firmware, inside its folder                               |
| `version.txt`     | Firmware name/version for that emulator (e.g.: `CPCESP.0.85`)          |

If both `firmware.bin` and `version.txt` exist in the SD card root, the bootloader uses single-emulator mode and skips the menu. Otherwise, it scans every folder on the card and builds the menu from the ones containing both files.

### About firmware.bin

**Do not use the web flasher** **`.bin`** **directly!** For **ESPectrum**, use the `.upg` file directly — just rename it to `firmware.bin`. For **MSPX** and **CPC**, you need to extract the app from the merged `.bin` (see section 4.1) or when the author makes the files available in their repository..

## 2. How to flash the bootloader?

**Option 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Option 2 — Compile yourself:**

```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 3. Where to find the emulators?

* **ESPectrum:** [Visite EremusOne Oficial ESPectrum github page](https://github.com/EremusOne/ESPectrum)

  get the `.upg` file, rename it to `firmware.bin` and put on a sd card together with `version.txt` file

* **CPC:** [Visite EremusOne Oficial CPCESP github page](https://github.com/EremusOne/CPCESP_alpha))

  Instructions how to extract firmware.bin in [section 3.1](#31-how-to-extract-the-correct-firmwarebin)

* **MSPX:** Available only to Eremus sponsors. Link coming soon.

* **VIC20:** [Direct download](https://alternativebits.com/esp32/VIC20.zip)

* **TRS COLOR:** [Direct download](http://alternativebits.com/esp32/coco.zip)

## 3.1. How to extract the correct firmware.bin

### For MSPX and CPC (offset 0x40000)

```python
python3 -c "
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
app = data[0x40000:]
with open('firmware.bin', 'wb') as f:
    f.write(app)
print(f'firmware.bin: {len(app)//1024} KB')"
```

### To find the correct offset for any .bin

```python
python3 -c "
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
print(f'Total size: {len(data)} bytes ({len(data)//1024} KB)')
for offset in [0x0000, 0x1000, 0x8000, 0xe000, 0x10000, 0x40000, 0x90000, 0xa0000]:
    if offset < len(data):
        print(f'offset 0x{offset:05X}: 0x{data[offset]:02X}')"
```

Look for offsets returning `0xE9`. The **first** is the bootloader (skip). The **second** is the app (use this).

## 4. Known issues

* ⚠️ **PS2 Keyboard** — improved in 0.6.0a (ISR critical section + no buffer wipe). If it still misbehaves after boot, power off and on again.

* ⚠️ **PocketTRS** — not compatible yet due to WiFi conflicts and partition differences.

* ⚠️ **First flash after erase** — some emulators save settings in NVS. After erase flash, settings are lost.

* ⚠️ **ESPectrum self-update** — no longer works due to partition changes. Use the bootloader to update instead.

***

## 5. How to adapt any project to work with the bootloader

The bootloader always flashes firmware to the `ota_0` partition (`0x130000`). For the app to return control to the bootloader on the next power-up, it must erase the `otadata` partition during startup — otherwise the ESP32 will boot directly into the app, bypassing the bootloader.

Add the following block at the **very beginning of `setup()`**, before anything else:

```cpp
#include "esp_ota_ops.h"
#include "esp_partition.h"

void setup() {
    // Ensures the bootloader runs on the next power-up
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }
    // ... rest of original setup
}
```

Once this is done:

1. Compile the project normally (`pio run`)
2. Copy `.pio/build/<env>/firmware.bin` to the emulator's folder on the SD card
3. Create a `version.txt` file with the version name (e.g. `MyApp_1.0`)
4. The bootloader will flash and boot it automatically

> **Note:** The maximum firmware size is **2816KB** (`0x2C0000`). If your firmware exceeds this limit, it won't fit in the `ota_0` partition.

> **ESP-IDF projects** (`framework = espidf`) may not need this modification, depending on whether the project manages `otadata` internally.

---

## 6. Bootloader partition table

This is the partition layout used by the bootloader. It is provided here for reference only — you do not need to replicate this in your own projects.

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
factory,  app,  factory, 0x10000,  0x120000
ota_0,    app,  ota_0,   0x130000, 0x2C0000
spiffs,   data, spiffs,  0x3F0000, 0x10000
```

| Partition | Offset     | Size    | Description                        |
|-----------|------------|---------|------------------------------------|
| nvs       | 0x9000     | 20KB    | Non-volatile storage               |
| otadata   | 0xE000     | 8KB     | OTA boot selector                  |
| factory   | 0x10000    | 1152KB  | The bootloader itself              |
| ota_0     | 0x130000   | 2816KB  | Where emulator firmwares are stored|
| spiffs    | 0x3F0000   | 64KB    | Reserved                           |

> This table matches `partitions.csv`. Older docs that mentioned factory `0x90000` / ota_0 `@0xA0000` / 3392KB are outdated.

## 7. ULTRA SUPER BETA

**It can crash · The API may change · It has bugs · But it works!** — most of the time.

## 8. Credits

* **[EremusOne](https://github.com/EremusOne)** — for ESPectrum, CPCEsp and MSPX

* **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — for the FabGL library

## 9. If you liked it, you know what to do!

Consider a donation through **[this link](https://github.com/sponsors/fg1998)**. I will spend all donated money on BEER 🍺

If you want to hire me as a freelancer for WEB or embedded projects, get in touch!

🇧🇷 Se você for brasileiro e quiser doar qualquer valor via PIX use a chave <fg1998@gmail.com>

*by Fernando Garcia —* *[fg1998](https://github.com/fg1998)*

*find me —* *<fg1998@gmail.com>*


## 10. License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

See the LICENSE file for more details.

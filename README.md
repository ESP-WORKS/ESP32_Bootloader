<img src="https://alternativebits.com/esp32/logo_app.png" alt="ESP32 Logo"/>

## What is this ???

The **ESP32 Bootloader** is a firmware for the **TTGO VGA32 v1.4** (or any ESP32 with FAGB VGA Output) that turns the ESP32 into an emulator loader (or any other software) via SD card. Instead of needing a computer to flash a different emulator onto the ESP32 every time, you simply insert an SD card with the desired firmware, power up the device, and it does everything automatically — checks if the firmware is new, flashes it if necessary, and starts the emulator.

Allows **multiple emulators on a single card**. Place `firmware.bin` and `version.txt` files inside folders in the root directory of the SD card (for example: `MSX`, `ESPectrum`, `CPCESP`, etc.), and a menu will be displayed with the option to choose which emulator will be flashed/executed. If you prefer to use one card for each emulator, simply place `firmware.bin` and `version.txt` in the root directory of the SD card, and the flashing process will run when you swap cards.

The bootloader occupies the `factory` partition of the ESP32. Emulators are flashed to the `ota_0` partition. Next time you power up with the same firmware, it goes directly to the emulator without going through the bootloader. If you swap the card, it loads the new firmware. Pretty much like Multicore does.

## Table of Contents

* [1. How does it work?](#1-how-does-it-work)

* [2. How to flash the bootloader?](#2-how-to-flash-the-bootloader)

* [3. Downloading emulators over WiFi](#3-downloading-emulators-over-wifi)

* [4. How to adapt any project to work with the bootloader](#4-how-to-adapt-any-project-to-work-with-the-bootloader)

* [5. Bootloader partition table (for reference)](#5-bootloader-partition-table)

* [6. Credits and Acknowledgments](#6-credits-and-acknowledgments)

* [7. If you liked it](#7-if-you-liked-it-you-know-what-to-do)

* [8. License](#8-license)

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

#### Single-emulator mode (files in SD root)

| File           | Description                                                              |
| -------------- | ------------------------------------------------------------------------ |
| `firmware.bin` | The emulator firmware                                                    |
| `version.txt`  | A simple text with the firmware name/version (e.g.: `ESPectrum_1.4.5`)   |

#### Multi-emulator mode (one folder per emulator)

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

| File/Folder     | Description                                                              |
| --------------- | ------------------------------------------------------------------------ |
| `<FolderName>/` | One folder per emulator. The folder name is what shows up in the menu    |
| `firmware.bin`  | The emulator firmware, inside its folder                                 |
| `version.txt`   | Firmware name/version for that emulator (e.g.: `CPCESP.0.85`)            |

If both `firmware.bin` and `version.txt` exist in the SD card root, the bootloader uses single-emulator mode and skips the menu. Otherwise, it scans every folder on the card and builds the menu from the ones containing both files.

## 2. How to flash the bootloader?

**Option 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Option 2 — Compile yourself:**

```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 3. Downloading emulators over WiFi

The bootloader can also fetch emulators directly from the internet, so you don't need to prepare the SD card on a computer every time a new emulator is released.

To enable this, create a file called **`wificonfig.rc`** in the **root of the SD card** with your network credentials:

```
ssid=YourNetworkName
password=YourPassword
```

When the bootloader boots and finds `wificonfig.rc`, it connects to the network and adds a **"Download emulators"** option to the main menu. This option queries the online catalog, lists the emulators available for download, and lets you pick one. The selected firmware is downloaded, saved into its own folder on the SD card (`/<EmulatorName>/firmware.bin` + `version.txt`), and then flashed and started as usual.

**Notes:**

* `wificonfig.rc` is only read — the bootloader never overwrites it.
* If the file is missing or the connection fails, the bootloader keeps working normally in offline mode.
* Downloaded emulators stay on the SD card, so you can reuse them later without WiFi.

## 4. How to adapt any project to work with the bootloader

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

## 5. Bootloader partition table

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

## 6. Credits and Acknowledgments

**Credits**

* **[fg1998](https://github.com/fg1998)** — Bootloader development
* **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — for the FabGL library

**Special thanks to:**

* [Joselito Oliveira](https://github.com/joselito668) - Boooloader Download module and many improvements !
* [Leomanes](https://github.com/leomanes) - A lot of compatible emulators !
* [Evandro Massini](https://github.com/EvandroMassini) — MSX2 full working emulator
* [reyco2000](https://github.com/reyco2000) - Macintosh and TRS-80 compatible emulators
* [Victor Iborra (EremusOne)](https://github.com/eremusone) - ESPectrum, MSPX and CPCESP emulators

## 7. If you liked it, you know what to do!

Consider a donation through **[this link](https://github.com/sponsors/fg1998)**. I will spend all donated money on BEER 🍺

If you want to hire me as a freelancer for WEB or embedded projects, get in touch!

🇧🇷 Se você for brasileiro e quiser doar qualquer valor via PIX use a chave <fg1998@gmail.com>

*by Fernando Garcia —* *[fg1998](https://github.com/fg1998)*

*find me —* *<fg1998@gmail.com>*


## 8. License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

See the LICENSE file for more details.

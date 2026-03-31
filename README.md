# TTGO T-Display OBD HUD

**[Thông tin & Hướng dẫn bằng tiếng Việt - Nhấn vào đây](README-VN.md)**

Firmware for the **LilyGO TTGO T-Display** (ESP32, ST7789 135×240, USB‑C): connects over **Bluetooth Classic** to an **ELM327** OBD-II adapter and draws a **LVGL 9** dashboard (RPM, speed, coolant temperature, battery voltage). **V1.2** adds a **compile-time option to swap the RPM and speed positions** on screen (see **`HUD_SWAP_RPM_SPEED_LAYOUT`** below). Backlight is still adjusted with the **onboard GPIO35 button** (100% / 50% / 20%), polled on a **dedicated FreeRTOS task** so it keeps working while Bluetooth connect can block the main loop for a long time. With **`HUD_USE_BUTTON_BACKLIGHT=0`**, the backlight stays at the **PWM level from boot** (typically full brightness) and the button is not polled.

### Firmware versions


| Version  | Notes                                                                                         |
| -------- | --------------------------------------------------------------------------------------------- |
| **V1.0** | Stable baseline: fixed backlight, full telemetry UI.                                        |
| **V1.1** | **Button backlight:** short press on **BUTTON1 (GPIO35)** cycles PWM **100% → 50% → 20% → 100%** (debounced). LilyGO pin table: **BUTTON1 = 35**, **BUTTON2 = 0** ([pinmap](https://raw.githubusercontent.com/Xinyuan-LilyGO/TTGO-T-Display/master/image/pinmap.jpg)). |
| **V1.2** | **Layout:** set **`HUD_SWAP_RPM_SPEED_LAYOUT=1`** to show **large centered speed** with a small **`km/h`** label and **RPM** (with **`RPM` label**) at the bottom-left; the RPM **bar** is unchanged. Requires **Montserrat 28** in `lv_conf.h` (already enabled in this repo). **Backlight:** GPIO35 is polled on **core 0** so brightness steps and optional **`HUD_SERIAL_DEBUG_BACKLIGHT`** serial lines are not frozen for tens of seconds during **`SerialBT.connect()`**; debug prints use **non-blocking TX** when the USB UART buffer is full. |


## Tested adapter (reference setup)

This project is written for adapters that expose a **Bluetooth Classic (BR/EDR, “BT 3.0”)** serial profile — **not** Bluetooth Low Energy (BLE).

**Reference hardware:** **Vgate iCar Pro Bluetooth 3.0** — Classic Bluetooth only (no BLE). Pairing is by the adapter’s Bluetooth device name and/or MAC, same as other ELM327 serial dongles.

> **ESP32 note:** The ESP32 supports Bluetooth Classic, which is why it can talk to this class of dongle. Boards that only support BLE cannot use this firmware with a BT 3.0 ELM327 adapter without an extra bridge.

## Changing the Bluetooth adapter name

The firmware opens a **BluetoothSerial** connection using the name (and optionally the MAC) compiled into the binary.

1. Open `platformio.ini` in the project root.
2. In `[env:ttgo_t_display]` → `build_flags`, find the line:
  ```ini
   -D OBD_BT_NAME=\"YourAdapterName\"
  ```
3. Replace `YourAdapterName` with the **exact Bluetooth name** your phone or PC shows when scanning (case-sensitive in practice; the firmware uppercases responses but the initial pairing target is the name you configure here).
4. **Optional — fixed MAC:** If multiple devices share similar names or pairing is flaky, set:
  ```ini
   -D OBD_BT_MAC=\"AA:BB:CC:DD:EE:FF\"
  ```
   Use your adapter’s real MAC (colons, uppercase hex is typical). You can leave `OBD_BT_MAC` as `\"\"` to connect by name only (see connection order in `src/main.cpp`).
5. **Rebuild and re-flash** after any change (`pio run -e ttgo_t_display -t upload`).

Defaults in repo may still show a placeholder name (e.g. `V-LINK`); **you must set `OBD_BT_NAME` to match your Vgate (or other) dongle.**

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- LilyGO TTGO T-Display
- ELM327 adapter with **Bluetooth Classic** (e.g. Vgate iCar Pro BT 3.0)

## Build & upload

Default environment: `ttgo_t_display` (board `esp32dev`, partition `huge_app` for firmware size).

### Using VS Code

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Open the Extensions view (**Ctrl+Shift+X** / **Cmd+Shift+X**), search for **PlatformIO IDE**, install it, then **reload** the window when prompted (first launch may download the PlatformIO Core / toolchains — wait until the status bar shows PlatformIO is ready).
3. **File → Open Folder…** and select the project root (the folder that contains `platformio.ini`).
4. Wait for PlatformIO to finish indexing; confirm the active environment is **`ttgo_t_display`** (project environments appear in the PlatformIO toolbar at the bottom of the window — pick `ttgo_t_display` if multiple are listed).
5. **Build:** PlatformIO icon in the left activity bar → **PROJECT TASKS** → **ttgo_t_display** → **General** → **Build**, or use the **checkmark** (“PlatformIO: Build”) in the bottom status bar.
6. **Upload:** Connect the board via USB, then **General** → **Upload**, or the **arrow** (“PlatformIO: Upload”) in the status bar. Put the board in download mode if your cable/driver requires it (TTGO T-Display usually uploads without extra buttons).
7. **Serial monitor:** **General** → **Monitor**, or the **plug** icon (“PlatformIO: Serial Monitor”) — default baud **115200** is set in `platformio.ini`.

If **Upload** or **Monitor** fails, install the USB-UART driver for your OS (CP210x or CH340, depending on the board) and choose the correct COM port under **PlatformIO: Upload Port** / device list.

### Command line (CLI)

```bash
pio run -e ttgo_t_display -t upload
```

Serial monitor (115200 baud):

```bash
pio device monitor -b 115200
```

### Maintainer note (documentation)

When you change this **README.md**, update **[README-VN.md](README-VN.md)** so the Vietnamese guide stays aligned.

## Configuration summary


| `platformio.ini` define               | Purpose                                                                                         |
| ------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `OBD_BT_NAME`                         | Bluetooth name of the ELM327 adapter (must match your dongle)                                   |
| `OBD_BT_MAC`                          | Optional fixed adapter MAC if name-only connect is unreliable                                   |
| `BRIDGE_RPM_MS`, `BRIDGE_SPEED_MS`, … | PID request intervals in milliseconds (names are legacy; they only set poll timing in `src/main.cpp`) |
| `HUD_USE_BUTTON_BACKLIGHT`            | Default `1`: GPIO35 short-press cycles backlight **100% / 50% / 20%**. Set `0` to disable button polling; backlight stays at boot level. |
| `HUD_BL_BUTTON_PIN`                   | GPIO for that button (default **35** = LilyGO **BUTTON1**).                                     |
| `HUD_BL_BUTTON_ACTIVE_HIGH`           | Default `0` (pressed = LOW). Set to `1` if your board reads HIGH when pressed.                  |
| `HUD_LEDC_WRITE_USES_GPIO_PIN`        | Usually auto from Arduino core: `0` = `ledcWrite(channel, …)` (core 2.x), `1` = `ledcWrite(GPIO, …)` (core 3.x). Set manually if backlight never changes after a core upgrade. |
| `HUD_SERIAL_DEBUG_BACKLIGHT`          | Add this define (no value) to print GPIO / PWM lines on **Serial** at **115200** baud.        |
| `HUD_SWAP_RPM_SPEED_LAYOUT`           | Default `0`. Set `1`: **speed** centered as **large digits + small `km/h`** (flex row); **RPM** at **bottom-left** as **28 pt value + small `RPM`**. The RPM **bar** is unchanged. |


Display pins and **TFT_eSPI** options for the TTGO board are also in `platformio.ini`.

**Backlight:** PWM on **`TFT_BL`** (pin **4** on this build). Default **`HUD_BL_BUTTON_PIN`** is **35** (LilyGO **BUTTON1**): onboard pull-up to 3.3 V, **pressed = LOW**. If you use **BUTTON2**, set `HUD_BL_BUTTON_PIN=0` (note: holding **GPIO0** low at reset enters download mode). Requires `TFT_BL` in `platformio.ini` (already set for TTGO T-Display). After **LVGL** and **Bluetooth** init, the firmware **re-attaches LEDC** on `TFT_BL` so PWM is not left in plain GPIO mode. If brightness still does not change when you press the button, toggle **`HUD_LEDC_WRITE_USES_GPIO_PIN`** (`0` vs `1`) for your Arduino-ESP32 core.

### Backlight button: Serial Monitor (troubleshooting)

1. Add to `build_flags` in `platformio.ini`: `-D HUD_SERIAL_DEBUG_BACKLIGHT`
2. Rebuild, flash, open the serial monitor at **115200** baud.
3. You should see a boot line with `TFT_BL`, button GPIO, and `ledc_writes_pin` vs `ledc_writes_channel`. Every ~400 ms: `GPIO35 raw=…` (0 = pressed on a typical board), `step`, and `duty`.
4. If you press the **GPIO35** button and **`step` / `duty` never change** but `raw` toggles, note the `ledc_writes_*` line — after an **Arduino-ESP32 3.x** upgrade you may need `-D HUD_LEDC_WRITE_USES_GPIO_PIN=1` (or `0` if it was wrong).
5. If **`raw` never changes** when you press either onboard key, try `-D HUD_BL_BUTTON_PIN=0` for **BUTTON2**, or `-D HUD_BL_BUTTON_ACTIVE_HIGH=1` if your hardware is inverted.
6. If **`raw` stays `1` and `released=1` forever** while you press **BUTTON1**, the MCU never sees **LOW** on that GPIO (wrong pin, broken switch, or a clone without the usual pull-up on **GPIO35** — ESP32 cannot enable an internal pull-up on GPIO **34–39**). Try **`HUD_BL_BUTTON_PIN=0`** (other key; do not hold it low at reset), verify the [LilyGO pinmap](https://raw.githubusercontent.com/Xinyuan-LilyGO/TTGO-T-Display/master/image/pinmap.jpg), or add an external **10kΩ to 3.3V** if the input floats.

### Serial: `ASSERT_WARN(… lc_task.c …)` at boot

Long **`ASSERT_WARN`** lines from **`lc_task.c`** come from the ESP32 **Bluetooth stack** (Bluedroid), not from the OBD parser. They are often seen when Classic BT is active and are usually **non-fatal** if the adapter still connects and telemetry updates.

## Project layout


| Path                      | Role                                          |
| ------------------------- | --------------------------------------------- |
| `src/main.cpp`            | Bluetooth ELM327 client, OBD parsing, LVGL UI |
| `include/lv_conf.h`       | LVGL configuration and enabled fonts          |
| `images/elm_327_mini.png` | Reference image for documentation             |


## Optional: offline OBD line check

`tools/obd_normalizer.py` replays captured serial lines to validate hex / mode `01` parsing (no car or flash required).

## Verified vehicle

Firmware has been **run successfully** on a **Mazda 2**, **automatic transmission**, **2023** model, with the reference adapter above.

## Credits

- **Author:** Van Tech Corner  
- **Assistant:** Cursor AI  
- **Inspired by:** the [Car HUD](https://github.com/fbiego/car-hud) project by [fbiego](https://github.com/fbiego) — an ESP32 + LVGL OBD telemetry concept (upstream focuses on BLE and a different display stack; this repository targets the TTGO T-Display and **Bluetooth Classic** ELM327 adapters).

## License

See `LICENSE`.

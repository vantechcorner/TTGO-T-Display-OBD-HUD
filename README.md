# TTGO T-Display OBD HUD

Firmware for the **LilyGO TTGO T-Display** (ESP32, ST7789 135×240, USB‑C): connects over **Bluetooth Classic** to an **ELM327** OBD-II adapter and draws a **LVGL 9** dashboard (RPM, speed, coolant temperature, battery voltage).

### Firmware versions

| Version | Notes |
|---------|--------|
| **V1.00** | Stable baseline: fixed backlight, full telemetry UI. |
| **V1.1** | Adds **auto backlight** from OBD **Mode 01 PID `0x3E`** (*Auxiliary input / output status*): if the masked bit(s) indicate “lights on”, backlight PWM is set to **20%**; otherwise **100%**. If the ECU never returns `41 3E …` (unsupported / `NO DATA`), brightness stays at **100%**. Bit meanings are **not standardized** across brands — you may need to change `OBD_LIGHTS_ON_MASK` in `platformio.ini` or use a scope / log to find the correct bit for your car. |

## Tested adapter (reference setup)

This project is written for adapters that expose a **Bluetooth Classic (BR/EDR, “BT 3.0”)** serial profile — **not** Bluetooth Low Energy (BLE).

**Reference hardware:** **Vgate iCar Pro Bluetooth 3.0** — Classic Bluetooth only (no BLE). Pairing is by the adapter’s Bluetooth device name and/or MAC, same as other ELM327 serial dongles.

> **ESP32 note:** The ESP32 supports Bluetooth Classic, which is why it can talk to this class of dongle. Boards that only support BLE cannot use this firmware with a BT 3.0 ELM327 adapter without an extra bridge.

## Changing the Bluetooth adapter name

The firmware opens a **BluetoothSerial** connection using the name (and optionally the MAC) compiled into the binary.

1. Open **`platformio.ini`** in the project root.
2. In **`[env:ttgo_t_display]` → `build_flags`**, find the line:
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

Default environment: **`ttgo_t_display`** (board `esp32dev`, partition **`huge_app`** for firmware size).

```bash
pio run -e ttgo_t_display -t upload
```

Serial monitor (115200 baud):

```bash
pio device monitor -b 115200
```

## Configuration summary

| `platformio.ini` define | Purpose |
|-------------------------|---------|
| `OBD_BT_NAME` | Bluetooth name of the ELM327 adapter (must match your dongle) |
| `OBD_BT_MAC` | Optional fixed adapter MAC if name-only connect is unreliable |
| `BRIDGE_RPM_MS`, `BRIDGE_SPEED_MS`, … | PID request intervals in milliseconds (names are legacy; they only set poll timing in `src/main.cpp`) |
| `BRIDGE_LIGHTS_MS` | How often to request PID `0x3E` for backlight logic (V1.1+, default 2000 ms) |
| `OBD_LIGHTS_ON_MASK` | Bitmask applied to the `0x3E` payload (decimal, e.g. `1` = bit 0, `256` = bit 8). When `(raw & mask) != 0`, firmware treats exterior/lights as **on** and dims to 20%. |

Display pins and **TFT_eSPI** options for the TTGO board are also in **`platformio.ini`**.

**Backlight hardware:** PWM on **`TFT_BL`** (pin **4** on this build). Requires `TFT_BL` defined in `platformio.ini` (already set for TTGO T-Display).

## Project layout

| Path | Role |
|------|------|
| `src/main.cpp` | Bluetooth ELM327 client, OBD parsing, LVGL UI |
| `include/lv_conf.h` | LVGL configuration and enabled fonts |
| `images/elm_327_mini.png` | Reference image for documentation |

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

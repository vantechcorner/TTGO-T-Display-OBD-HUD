# Handoff / continuation notes

**Date:** 2026-03-28  
**Project:** TTGO T-Display OBD HUD (`TTGO-T-Display-OBD-HUD`)  
**Firmware label in tree:** **V1.2** (`src/main.cpp` header, `README.md`, `README-VN.md`, comment in `platformio.ini`).

Use this file when resuming work in Cursor or another session. User-facing docs remain in **README.md** / **README-VN.md** (keep them in sync per `.cursor/rules`).

---

## Summary of recent work (from chat)

1. **Backlight button (GPIO35)**  
   - Debounced edge detection with **two consecutive `digitalRead`s** and **~25 ms** stability.  
   - Earlier multi-sample + `delayMicroseconds` loop was removed (simpler + less load).

2. **Serial debug (`HUD_SERIAL_DEBUG_BACKLIGHT`)**  
   - **`hud_bl_dbg_emitf()`**: format to a stack buffer, send only if `Serial.availableForWrite()` has space — avoids **blocking** when the USB-UART TX buffer is full.  
   - Periodic log updates **`g_hud_bl_dbg_log_ms` only after a successful emit**.  
   - **`Serial.setTxBufferSize(1024)`** before `Serial.begin` when debug is enabled.

3. **Long pauses (~30–45 s) with no `[BL]` lines / dead backlight**  
   - **Cause:** **`SerialBT.connect()`** runs inside `loop()` and can block for tens of seconds; **`hud_backlight_button_poll()`** used to run only at the start of `loop()`.  
   - **Fix:** FreeRTOS task **`hud_bl`** on **core 0** (`hud_backlight_poll_task`), **`vTaskDelay(10 ms)`**, calls **`hud_backlight_button_poll()`** only. **`loop()`** no longer calls the poll (single caller). Arduino **`loop()`** runs on **core 1** on typical dual-core ESP32.

4. **Layout**  
   - **`HUD_SWAP_RPM_SPEED_LAYOUT`**: large centered speed + small `km/h`, RPM bottom-left + label; RPM bar unchanged. **`LV_FONT_MONTSERRAT_28`** enabled in `include/lv_conf.h` for swap layout.

5. **Version label**  
   - Briefly documented as V1.3 then corrected to **V1.2** everywhere.

---

## Pointers

| Topic | Where |
|--------|--------|
| OBD / BT / UI / backlight | `src/main.cpp` |
| Build flags | `platformio.ini` |
| LVGL fonts / config | `include/lv_conf.h` |
| Bilingual docs | `README.md`, `README-VN.md` |

---

## Cursor conversation history

Full AI chat transcripts for this workspace are stored by Cursor under your user profile, e.g.:

`%USERPROFILE%\.cursor\projects\<workspace-folder-name>\agent-transcripts\`

(Exact folder name may match the project path.) You can also use **Cursor chat history** in the UI to reopen past threads.

---

## Optional next steps (ideas only)

- Async or time-sliced **Bluetooth reconnect** to shorten blocking on core 1 (larger change).  
- Document or default **`HUD_SERIAL_DEBUG_BACKLIGHT`** off in `platformio.ini` for release builds if desired.

/*
 * TTGO T-Display ESP32 — OBD-II (BT Classic ELM327) + LVGL racing UI.
 * Display: ST7789 135x240, rotation 1 => 240x135 landscape.
 * Driver: TFT_eSPI + LVGL 9 (partial double-buffer, no full-frame flicker).
 *
 * Firmware: V1.2 — optional RPM/speed layout swap (HUD_SWAP_RPM_SPEED_LAYOUT); GPIO35 backlight
 * cycling (100% / 50% / 20%) polled on a core-0 task so BT connect does not block the button.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <BluetoothSerial.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
#include <stdarg.h>
#endif

#ifndef OBD_BT_MAC
#define OBD_BT_MAC ""
#endif
#ifndef OBD_BT_NAME
#define OBD_BT_NAME "V-LINK"
#endif
#ifndef BRIDGE_RPM_MS
#define BRIDGE_RPM_MS 180U
#endif
#ifndef BRIDGE_SPEED_MS
#define BRIDGE_SPEED_MS 280U
#endif
#ifndef BRIDGE_TEMP_MS
#define BRIDGE_TEMP_MS 8000U
#endif
#ifndef BRIDGE_VOLT_MS
#define BRIDGE_VOLT_MS 2000U
#endif

/* 1 = cycle backlight via HUD_BL_BUTTON_PIN. 0 = fixed backlight from init (no GPIO button polling). */
#ifndef HUD_USE_BUTTON_BACKLIGHT
#define HUD_USE_BUTTON_BACKLIGHT 1
#endif
#ifndef HUD_BL_BUTTON_PIN
#define HUD_BL_BUTTON_PIN 35
#endif
/* 0 = LilyGO default (released = HIGH, pressed = LOW). 1 = pressed reads HIGH. */
#ifndef HUD_BL_BUTTON_ACTIVE_HIGH
#define HUD_BL_BUTTON_ACTIVE_HIGH 0
#endif
/*
 * Arduino-ESP32 2.x: ledcWrite(channel, duty) after ledcAttachPin(gpio, channel).
 * Arduino-ESP32 3.x: ledcWrite(gpio, duty). Using the wrong form leaves TFT_BL stuck at one level.
 * Override with -D HUD_LEDC_WRITE_USES_GPIO_PIN=0|1 if auto-detect is wrong.
 */
#ifndef HUD_LEDC_WRITE_USES_GPIO_PIN
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
#define HUD_LEDC_WRITE_USES_GPIO_PIN 1
#else
#define HUD_LEDC_WRITE_USES_GPIO_PIN 0
#endif
#endif

/* Top bar visual max (real RPM can exceed; bar pegs at this value) */
#ifndef RPM_BAR_RANGE_MAX
#define RPM_BAR_RANGE_MAX 6000
#endif
#ifndef RPM_LABEL_MAX
#define RPM_LABEL_MAX 9999
#endif
/* 1 = speed as large center readout, RPM smaller bottom-left (swap vs default). */
#ifndef HUD_SWAP_RPM_SPEED_LAYOUT
#define HUD_SWAP_RPM_SPEED_LAYOUT 0
#endif

/* --- Display / LVGL --- */
static constexpr uint16_t TFTW = 240;
static constexpr uint16_t TFTH = 135;

TFT_eSPI tft;
BluetoothSerial SerialBT;

static lv_display_t *g_disp;
static lv_color_t g_buf1[TFTW * 28];
static lv_color_t g_buf2[TFTW * 28];

static lv_obj_t *g_rpm_bar;
static lv_obj_t *g_lbl_rpm;
static lv_obj_t *g_lbl_speed;
#if HUD_SWAP_RPM_SPEED_LAYOUT
static lv_obj_t *g_lbl_speed_kmh;
static lv_obj_t *g_lbl_rpm_unit;
#endif
static lv_obj_t *g_lbl_temp;
static lv_obj_t *g_lbl_volt;
static lv_obj_t *g_lbl_bt;

static bool g_startup_sweep_done = false;

/* --- OBD state --- */
static String lineBuffer;
static bool obdReady = false;
static uint32_t lastFast = 0;
static uint32_t lastMedium = 0;
static uint32_t lastSlow = 0;
static uint32_t lastVolt = 0;
static uint32_t lastReconnectTry = 0;

static int displayRpm = 0;
static int displaySpeed = 0;
static int displayTemp = 0;
static float displayVoltage = 0.f;
static bool haveData = false;
static bool haveVoltage = false;

/* FontAwesome trong Montserrat không có thermometer (f2c9); LV_SYMBOL_TINT (giọt) nằm trong glyph set */
#define HUD_SYM_COOLANT LV_SYMBOL_TINT

#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
/* Serial.printf can block a long time when the USB-UART TX buffer is full; loop() then never
 * finishes and the backlight button poll stops. Only emit when there is FIFO space. */
static bool hud_bl_dbg_emitf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0 || n >= (int)sizeof(buf))
    return false;
  if (Serial.availableForWrite() < n + 24)
    return false;
  Serial.write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(n));
  return true;
}
#endif

#ifdef TFT_BL
static constexpr uint8_t kBlPwmChannel = 5;
static constexpr uint32_t kBlPwmFreq = 12000;
static constexpr uint8_t kBlPwmBits = 8;
static uint8_t g_bl_duty_applied = 0xFF;

static void hud_ledc_write_bl(uint8_t duty)
{
#if HUD_LEDC_WRITE_USES_GPIO_PIN
  ledcWrite((uint8_t)TFT_BL, duty);
#else
  ledcWrite(kBlPwmChannel, duty);
#endif
}

static void backlight_pwm_init()
{
  ledcSetup(kBlPwmChannel, kBlPwmFreq, kBlPwmBits);
  ledcAttachPin(TFT_BL, kBlPwmChannel);
  g_bl_duty_applied = 0xFE; /* force first write */
  hud_ledc_write_bl(255);
  g_bl_duty_applied = 255;
}

/* LVGL / BT init can leave TFT_BL back in GPIO mode; re-run LEDC attach before polling the button. */
static void backlight_pwm_reassert(void)
{
  ledcSetup(kBlPwmChannel, kBlPwmFreq, kBlPwmBits);
  ledcAttachPin(TFT_BL, kBlPwmChannel);
  hud_ledc_write_bl(g_bl_duty_applied);
}

static void backlight_set_duty8(uint8_t duty)
{
  if (duty == g_bl_duty_applied)
    return;
  g_bl_duty_applied = duty;
  hud_ledc_write_bl(duty);
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
  (void)hud_bl_dbg_emitf("[BL] PWM duty=%u (ledc arg=%s TFT_BL=%d)\n", (unsigned)duty,
                         HUD_LEDC_WRITE_USES_GPIO_PIN ? "GPIO pin" : "channel", (int)TFT_BL);
#endif
}
#endif

#if HUD_USE_BUTTON_BACKLIGHT && defined(TFT_BL)
/* LilyGO: BUTTON1 = GPIO35, BUTTON2 = GPIO0 (README pin table). */
static constexpr uint8_t kHudBlDutySteps[] = {
    255,
    (uint8_t)(255U * 50U / 100U),
    (uint8_t)(255U * 20U / 100U),
};
static uint8_t g_hud_bl_step = 0;
static bool g_hud_btn_sample = true;
static bool g_hud_btn_stable_high = true;
static uint32_t g_hud_btn_last_change_ms = 0;
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
static uint32_t g_hud_bl_dbg_log_ms = 0;
#endif

static bool hud_bl_pin_raw_pressed(int r)
{
#if HUD_BL_BUTTON_ACTIVE_HIGH
  return (r == HIGH);
#else
  return (r == LOW);
#endif
}

/* Two consecutive reads agree: cheap glitch filter without delayMicroseconds in loop(). */
static bool hud_bl_button_pressed_filtered(int *first_raw_out)
{
  const int a = digitalRead(HUD_BL_BUTTON_PIN);
  if (first_raw_out)
    *first_raw_out = a;
  const int b = digitalRead(HUD_BL_BUTTON_PIN);
  return hud_bl_pin_raw_pressed(a) && hud_bl_pin_raw_pressed(b);
}

static void hud_backlight_button_poll()
{
  int pin_raw = 0;
  const bool pressed_f = hud_bl_button_pressed_filtered(&pin_raw);
  const bool sample_high = !pressed_f; /* released (not pressed) */
  const uint32_t now = millis();
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
  if (now - g_hud_bl_dbg_log_ms >= 400)
  {
    if (hud_bl_dbg_emitf("[BL] GPIO%d raw=%d released=%u step=%u duty=%u\n", HUD_BL_BUTTON_PIN, pin_raw,
                         (unsigned)sample_high, (unsigned)g_hud_bl_step, (unsigned)g_bl_duty_applied))
      g_hud_bl_dbg_log_ms = now;
  }
#endif
  if (sample_high != g_hud_btn_sample)
  {
    g_hud_btn_sample = sample_high;
    g_hud_btn_last_change_ms = now;
  }
  if (now - g_hud_btn_last_change_ms < 25)
    return;
  const bool stable_high = sample_high;
  if (!stable_high && g_hud_btn_stable_high)
  {
    g_hud_bl_step = (uint8_t)((g_hud_bl_step + 1) % (sizeof(kHudBlDutySteps) / sizeof(kHudBlDutySteps[0])));
    backlight_set_duty8(kHudBlDutySteps[g_hud_bl_step]);
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
    (void)hud_bl_dbg_emitf("[BL] step -> %u\n", (unsigned)g_hud_bl_step);
#endif
  }
  g_hud_btn_stable_high = stable_high;
}

/* SerialBT.connect() and initElm() delay() block loop() for tens of seconds; Arduino loop() runs on
 * core 1, so a small task on core 0 keeps backlight button polling alive during BT connect. */
static void hud_backlight_poll_task(void * /*arg*/)
{
  for (;;)
  {
    hud_backlight_button_poll();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void hud_backlight_poll_task_start()
{
  constexpr UBaseType_t kPrio = 1;
  constexpr uint32_t kStackWords = 3072;
  if (xTaskCreatePinnedToCore(hud_backlight_poll_task, "hud_bl", kStackWords, nullptr, kPrio, nullptr, 0) !=
      pdPASS)
  {
    /* Unlikely; fallback is loop()-only poll (backlight may freeze during BT connect). */
  }
}
#endif

static const char *battery_symbol_for_voltage(float v)
{
  if (v < 11.6f)
    return LV_SYMBOL_BATTERY_EMPTY;
  if (v < 12.2f)
    return LV_SYMBOL_BATTERY_1;
  if (v < 12.6f)
    return LV_SYMBOL_BATTERY_2;
  if (v < 13.0f)
    return LV_SYMBOL_BATTERY_3;
  return LV_SYMBOL_BATTERY_FULL;
}

/* --------- TFT flush (LVGL) --------- */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  const int32_t x = area->x1;
  const int32_t y = area->y1;
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  tft.startWrite();
  tft.pushImage(x, y, w, h, reinterpret_cast<uint16_t *>(px_map));
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t *e)
{
  lv_area_t *a = lv_event_get_invalidated_area(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

static uint32_t lv_tick_cb(void)
{
  return millis();
}

static void style_rpm_bar(lv_obj_t *bar)
{
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1c1c22), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

  lv_obj_set_style_bg_color(bar, lv_color_hex(0x22E0AA), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
}

static void set_rpm_ui(int value, lv_anim_enable_t bar_anim)
{
  if (value < 0)
    value = 0;
  int bar_v = value;
  if (bar_v > RPM_BAR_RANGE_MAX)
    bar_v = RPM_BAR_RANGE_MAX;
  lv_bar_set_value(g_rpm_bar, bar_v, bar_anim);

  int show = value;
  if (show > RPM_LABEL_MAX)
    show = RPM_LABEL_MAX;
  lv_label_set_text_fmt(g_lbl_rpm, "%d", show);
}

/* --------- Public UI API --------- */
void update_rpm(int value)
{
  set_rpm_ui(value, LV_ANIM_ON);
}

void update_speed(int value)
{
  if (value < 0)
    value = 0;
  if (value > 999)
    value = 999;
#if HUD_SWAP_RPM_SPEED_LAYOUT
  lv_label_set_text_fmt(g_lbl_speed, "%d", value);
#else
  lv_label_set_text_fmt(g_lbl_speed, "%d km/h", value);
#endif
}

void update_temp(int value)
{
  lv_label_set_text_fmt(g_lbl_temp, HUD_SYM_COOLANT "  %d C", value);
  if (value > 95)
    lv_obj_set_style_text_color(g_lbl_temp, lv_color_hex(0xFF2020), 0);
  else
    lv_obj_set_style_text_color(g_lbl_temp, lv_color_hex(0xE0E8F0), 0);
}

static void update_voltage_label()
{
  char b[28];
  if (!haveVoltage)
  {
    snprintf(b, sizeof(b), "%s  --.- V", LV_SYMBOL_BATTERY_EMPTY);
    lv_label_set_text(g_lbl_volt, b);
    lv_obj_set_style_text_color(g_lbl_volt, lv_color_hex(0x8899AA), 0);
    return;
  }
  snprintf(b, sizeof(b), "%s  %.1f V", battery_symbol_for_voltage(displayVoltage), (double)displayVoltage);
  lv_label_set_text(g_lbl_volt, b);
  lv_obj_set_style_text_color(g_lbl_volt, lv_color_hex(0xE0E8F0), 0);
}

void update_status(bool connected)
{
  /* lv_obj + LV_RADIUS_CIRCLE + remove_style_all() can revert to full-screen size → huge "dot".
   * Use a tiny label instead (no arc, no circle geometry). */
  lv_obj_set_style_text_color(g_lbl_bt, connected ? lv_color_hex(0x00FF66) : lv_color_hex(0xFF2020), 0);
  lv_label_set_text_static(g_lbl_bt, connected ? "BT" : "bt");
}

/* --------- Startup RPM sweep --------- */
static void sweep_anim_exec(void *, int32_t v)
{
  set_rpm_ui((int)v, LV_ANIM_OFF);
}

static void sweep_anim_done(lv_anim_t *)
{
  g_startup_sweep_done = true;
  update_rpm(0);
}

static void run_startup_sweep()
{
  g_startup_sweep_done = false;
  static int sweep_var;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &sweep_var);
  lv_anim_set_exec_cb(&a, sweep_anim_exec);
  lv_anim_set_duration(&a, 550);
  lv_anim_set_reverse_duration(&a, 550);
  lv_anim_set_reverse_delay(&a, 0);
  lv_anim_set_values(&a, 0, RPM_BAR_RANGE_MAX);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, sweep_anim_done);
  lv_anim_start(&a);
}

/* --------- Build LVGL UI --------- */
static void build_ui()
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x050508), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* Battery — top-left (icon + V); bar sits below this row */
  g_lbl_volt = lv_label_create(scr);
  lv_label_set_text_static(g_lbl_volt, LV_SYMBOL_BATTERY_EMPTY "  --.- V");
  lv_obj_set_style_text_font(g_lbl_volt, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g_lbl_volt, lv_color_hex(0x8899AA), 0);
  lv_obj_set_style_text_align(g_lbl_volt, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(g_lbl_volt, LV_ALIGN_TOP_LEFT, 6, 4);

  /* Coolant — top-right (icon + °C), no "CLT" text */
  g_lbl_temp = lv_label_create(scr);
  lv_label_set_text_static(g_lbl_temp, HUD_SYM_COOLANT "  -- C");
  lv_obj_set_style_text_font(g_lbl_temp, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g_lbl_temp, lv_color_hex(0xE0E8F0), 0);
  lv_obj_set_style_text_align(g_lbl_temp, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_lbl_temp, LV_ALIGN_TOP_RIGHT, -6, 4);

  /* Single RPM bar — full width below top row */
  g_rpm_bar = lv_bar_create(scr);
  lv_obj_set_size(g_rpm_bar, TFTW - 8, 10);
  lv_bar_set_range(g_rpm_bar, 0, RPM_BAR_RANGE_MAX);
  style_rpm_bar(g_rpm_bar);
  lv_obj_align(g_rpm_bar, LV_ALIGN_TOP_MID, 0, 26);
  lv_bar_set_value(g_rpm_bar, 0, LV_ANIM_OFF);

#if HUD_SWAP_RPM_SPEED_LAYOUT
  /* Speed — center: large value + small "km/h", whole block centered (flex row) */
  {
    lv_obj_t *speed_row = lv_obj_create(scr);
    lv_obj_remove_style_all(speed_row);
    lv_obj_set_style_bg_opa(speed_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(speed_row, 5, 0);
    lv_obj_set_layout(speed_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(speed_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speed_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(speed_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(speed_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(speed_row, LV_SIZE_CONTENT);
    lv_obj_set_height(speed_row, LV_SIZE_CONTENT);

    g_lbl_speed = lv_label_create(speed_row);
    lv_label_set_text_static(g_lbl_speed, "0");
    lv_obj_set_style_text_font(g_lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_lbl_speed, lv_color_hex(0xF5F8FF), 0);
    lv_obj_set_style_text_letter_space(g_lbl_speed, 1, 0);
    lv_obj_clear_flag(g_lbl_speed, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_lbl_speed, LV_OBJ_FLAG_CLICKABLE);

    g_lbl_speed_kmh = lv_label_create(speed_row);
    lv_label_set_text_static(g_lbl_speed_kmh, "km/h");
    lv_obj_set_style_text_font(g_lbl_speed_kmh, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_speed_kmh, lv_color_hex(0x98A8C0), 0);
    lv_obj_clear_flag(g_lbl_speed_kmh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_lbl_speed_kmh, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_align(speed_row, LV_ALIGN_CENTER, 0, 4);
  }

  /* RPM — bottom left: slightly larger value + small "RPM"; bar above still tracks RPM */
  {
    lv_obj_t *rpm_row = lv_obj_create(scr);
    lv_obj_remove_style_all(rpm_row);
    lv_obj_set_style_bg_opa(rpm_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_column(rpm_row, 4, 0);
    lv_obj_set_layout(rpm_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rpm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rpm_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rpm_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rpm_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(rpm_row, LV_SIZE_CONTENT);
    lv_obj_set_height(rpm_row, LV_SIZE_CONTENT);

    g_lbl_rpm = lv_label_create(rpm_row);
    lv_label_set_text_static(g_lbl_rpm, "0");
    lv_obj_set_style_text_font(g_lbl_rpm, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_lbl_rpm, lv_color_hex(0xB8C8E0), 0);
    lv_obj_clear_flag(g_lbl_rpm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_lbl_rpm, LV_OBJ_FLAG_CLICKABLE);

    g_lbl_rpm_unit = lv_label_create(rpm_row);
    lv_label_set_text_static(g_lbl_rpm_unit, "RPM");
    lv_obj_set_style_text_font(g_lbl_rpm_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_rpm_unit, lv_color_hex(0x8899AA), 0);
    lv_obj_clear_flag(g_lbl_rpm_unit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_lbl_rpm_unit, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_align(rpm_row, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  }
#else
  /* RPM numeric — center, largest font */
  g_lbl_rpm = lv_label_create(scr);
  lv_label_set_text_static(g_lbl_rpm, "0");
  lv_obj_set_style_text_font(g_lbl_rpm, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(g_lbl_rpm, lv_color_hex(0xF5F8FF), 0);
  lv_obj_set_style_text_letter_space(g_lbl_rpm, 1, 0);
  lv_obj_align(g_lbl_rpm, LV_ALIGN_CENTER, 0, 4);

  /* Speed — bottom left, medium font + unit */
  g_lbl_speed = lv_label_create(scr);
  lv_label_set_text_static(g_lbl_speed, "0 km/h");
  lv_obj_set_style_text_font(g_lbl_speed, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(g_lbl_speed, lv_color_hex(0xB8C8E0), 0);
  lv_obj_align(g_lbl_speed, LV_ALIGN_BOTTOM_LEFT, 6, -6);
#endif

  /* BT hint — text only (see update_status: avoid lv_obj circle) */
  g_lbl_bt = lv_label_create(scr);
  lv_obj_set_style_text_font(g_lbl_bt, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_bt, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  update_status(false);
}

static void init_lvgl()
{
  lv_init();
  lv_tick_set_cb(lv_tick_cb);

  g_disp = lv_display_create(TFTW, TFTH);
  lv_display_set_flush_cb(g_disp, flush_cb);
  lv_display_set_buffers(g_disp, g_buf1, g_buf2, sizeof(g_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_add_event_cb(g_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);

  build_ui();
  set_rpm_ui(0, LV_ANIM_OFF);
  run_startup_sweep();
  update_speed(0);
  update_temp(0);
  update_voltage_label();
}

/* --------- OBD (ELM327) --------- */
void sendElm(const char *cmd)
{
  SerialBT.print(cmd);
  SerialBT.print("\r");
}

static uint8_t hexNibble(char c)
{
  if (c >= '0' && c <= '9')
    return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F')
    return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f')
    return (uint8_t)(c - 'a' + 10);
  return 0;
}

static uint8_t hexByte(char hi, char lo)
{
  return (uint8_t)((hexNibble(hi) << 4) | hexNibble(lo));
}

static bool is_hex_digit(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

static void applyObdCompact(const String &s);

/* Scan line for every embedded "41 XX ..." block (many ELM327 lines are longer than 6/8 chars
 * or concatenate two responses — the old exact-length check dropped speed + RPM only). */
static void parseAllMode41Responses(const String &lineUpper)
{
  String s = lineUpper;
  s.replace(" ", "");
  const int n = s.length();
  for (int i = 0; i + 6 <= n; ++i)
  {
    if (s[i] != '4' || s[i + 1] != '1')
      continue;
    if (!is_hex_digit(s[i + 2]) || !is_hex_digit(s[i + 3]))
      continue;
    const uint8_t pid = hexByte(s[i + 2], s[i + 3]);
    if (pid == 0x0C)
    {
      if (i + 8 <= n)
        applyObdCompact(s.substring(i, i + 8));
    }
    else if (pid == 0x42)
    {
      if (i + 8 <= n)
        applyObdCompact(s.substring(i, i + 8));
      else if (i + 6 <= n)
        applyObdCompact(s.substring(i, i + 6));
    }
    else if (pid == 0x05 || pid == 0x0D)
    {
      if (i + 6 <= n)
        applyObdCompact(s.substring(i, i + 6));
    }
  }
}

static void applyObdCompact(const String &s)
{
  if (s.length() < 6 || !s.startsWith("41"))
    return;

  const uint8_t pid = hexByte(s[2], s[3]);
  switch (pid)
  {
  case 0x0D:
    if (s.length() >= 6)
    {
      displaySpeed = hexByte(s[4], s[5]);
      haveData = true;
    }
    break;
  case 0x0C:
    if (s.length() >= 8)
    {
      const uint8_t a = hexByte(s[4], s[5]);
      const uint8_t b = hexByte(s[6], s[7]);
      displayRpm = (int)(((uint16_t)a << 8) | b) / 4;
      haveData = true;
    }
    break;
  case 0x05:
    if (s.length() >= 6)
    {
      const uint8_t a = hexByte(s[4], s[5]);
      displayTemp = (int)a - 40;
      haveData = true;
    }
    break;
  case 0x42: /* Control module voltage */
    if (s.length() >= 8)
    {
      const uint16_t raw = (uint16_t)((hexByte(s[4], s[5]) << 8) | hexByte(s[6], s[7]));
      displayVoltage = raw / 1000.0f;
      haveVoltage = true;
    }
    else if (s.length() >= 6)
    {
      displayVoltage = hexByte(s[4], s[5]) / 10.0f;
      haveVoltage = true;
    }
    break;
  default:
    break;
  }
}

static void processElmLine(String line)
{
  line.trim();
  line.toUpperCase();
  if (line.isEmpty())
    return;
  if (line == "OK" || line.startsWith("AT") || line.startsWith("SEARCHING"))
    return;
  if (line.indexOf("UNABLE") >= 0 || line.indexOf("ERROR") >= 0 || line.indexOf("NODATA") >= 0 ||
      line.indexOf("NO DATA") >= 0)
    return;

  parseAllMode41Responses(line);
}

static void processElmIncoming()
{
  while (SerialBT.available())
  {
    const char c = (char)SerialBT.read();
    if (c == '\r' || c == '\n' || c == '>')
    {
      if (!lineBuffer.isEmpty())
      {
        processElmLine(lineBuffer);
        lineBuffer = "";
      }
      continue;
    }
    if ((uint8_t)c < 32 || (uint8_t)c > 126)
      continue;
    lineBuffer += c;
    if (lineBuffer.length() > 128)
      lineBuffer = "";
  }
}

static bool connectObdAdapter()
{
  if (SerialBT.connect(OBD_BT_NAME))
    return true;
  if (strlen(OBD_BT_MAC) > 0)
    return SerialBT.connect(OBD_BT_MAC);
  return false;
}

static void initElm()
{
  sendElm("ATZ");
  delay(800);
  sendElm("ATE0");
  delay(250);
  sendElm("ATL0");
  delay(250);
  sendElm("ATS0");
  delay(250);
  sendElm("ATSP6");
  delay(300);
  obdReady = true;
  lastFast = lastMedium = lastSlow = lastVolt = millis();
}

static void sync_ui_from_obd()
{
  if (!g_startup_sweep_done)
    return;
  update_speed(displaySpeed);
  update_rpm(displayRpm);
  update_temp(displayTemp);
  update_voltage_label();
}

void setup()
{
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
  Serial.setTxBufferSize(1024);
#endif
  Serial.begin(115200);
  delay(150);
#ifdef HUD_SERIAL_DEBUG_BACKLIGHT
  Serial.println();
  (void)hud_bl_dbg_emitf(
      "HUD_SERIAL_DEBUG_BACKLIGHT TFT_BL=%d btn GPIO%d active_high=%d ledc_writes_%s\n",
#ifdef TFT_BL
      (int)TFT_BL,
#else
      -1,
#endif
      HUD_BL_BUTTON_PIN, (int)HUD_BL_BUTTON_ACTIVE_HIGH,
      HUD_LEDC_WRITE_USES_GPIO_PIN ? "pin" : "channel");
#endif

  tft.init();
  tft.setRotation(1);
  /* LVGL draws RGB565 little-endian; ST7789 SPI expects swapped bytes — without this, colors/glyphs are wrong */
  tft.setSwapBytes(true);
#ifdef TFT_BL
  backlight_pwm_init();
#endif
#if HUD_USE_BUTTON_BACKLIGHT
  /* Before TFT/LVGL heavy init — same order as original V1.1 (late pinMode broke reads on some boards). */
  pinMode(HUD_BL_BUTTON_PIN, INPUT);
#endif
  tft.fillScreen(TFT_BLACK);

  init_lvgl();

#ifdef TFT_BL
  backlight_pwm_reassert();
#endif

  if (!SerialBT.begin("TTGO-OBD", true))
  {
    /* Minimal error: red flash via LVGL */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x550000), 0);
    lv_timer_handler();
    while (true)
      delay(500);
  }
#if HUD_USE_BUTTON_BACKLIGHT && defined(TFT_BL)
  hud_backlight_poll_task_start();
#endif
#ifdef TFT_BL
  backlight_pwm_reassert();
#endif
}

void loop()
{
  const bool btOk = SerialBT.connected();
  update_status(btOk);

  if (!btOk)
  {
    obdReady = false;
    haveData = false;
    haveVoltage = false;
    const uint32_t now = millis();
    if (now - lastReconnectTry > 2500)
    {
      lastReconnectTry = now;
      if (connectObdAdapter())
        initElm();
    }
    if (g_startup_sweep_done)
    {
      update_speed(0);
      update_rpm(0);
      update_temp(0);
      update_voltage_label();
    }
    lv_timer_handler();
    delay(20);
    return;
  }

  processElmIncoming();

  if (!obdReady)
  {
    initElm();
    lv_timer_handler();
    return;
  }

  const uint32_t now = millis();

  if (now - lastFast >= BRIDGE_RPM_MS)
  {
    lastFast = now;
    sendElm("010C");
  }
  if (now - lastMedium >= BRIDGE_SPEED_MS)
  {
    lastMedium = now;
    sendElm("010D");
  }

  if (now - lastSlow >= BRIDGE_TEMP_MS)
  {
    lastSlow = now;
    sendElm("0105");
  }

  if (now - lastVolt >= BRIDGE_VOLT_MS)
  {
    lastVolt = now;
    sendElm("0142");
  }

  sync_ui_from_obd();
  lv_timer_handler();
  delay(5);
}

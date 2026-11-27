/*
  StrobeTuner for ESP32-S3 / ESP32-S2 with a GC9A01 round display and a rotary encoder.
  Based on StrobeTuner (chazomaticus/strobetuner,
  Copyright 2013 Charles Lindsay).

  This is free software: you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version. See <http://www.gnu.org/licenses/>.

  Code layout
    strobetuner_esp32s2.ino   hardware: strobe timer, display, encoder, storage
    src/core/tuner.*          settings, the five tunings, note maths (plain C)
    src/core/strobe_math.h    LED flash schedule (tested on the host too)
    src/ui/ui.*               LVGL screens, shared with ../simulator
    src/hw/gc9a01.*           display driver (ESP-IDF esp_lcd, SPI + DMA)

  Library (Arduino Library Manager): "lvgl" 9.6.x.
  Copy ../lv_conf.h to <sketchbook>/libraries/lv_conf.h.

  Wiring (all 3.3 V): see docs/breadboard.md and the pin table below.
    ESP32-S3: LEDs 7/8, display SCK 12 MOSI 11 CS 10 DC 9 RST 14 BL 13,
              encoder A 4 B 5 SW 6, tone 15
    ESP32-S2: LEDs 7/8, display SCK 36 MOSI 35 CS 34 DC 38 RST 33 BL 21,
              encoder A 9 B 10 SW 11, tone 12
    LED1 red via 68R, LED2 green via 39R..68R, piezo via 100R, all to GND.
    BOOT button also works as "press".

  Controls
    Tuner:        turn = string, press = tuning list, hold = strobe on/off
    Tuning list:  turn = scroll, press = use it, hold on Open = set its strings
    Open editor:  turn = string/Reset/Done, press = edit (turn = semitone), hold = back
    Settings:     last item in the tuning list
*/

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <lvgl.h>
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "src/core/tuner.h"
#include "src/core/strobe_math.h"
#include "src/ui/ui.h"
#include "src/hw/gc9a01.h"

// ---- Pins -----------------------------------------------------------------
// Chosen per chip at compile time. See docs/breadboard.md for the wiring.
#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3-DevKitC-1 and clones (e.g. N16R8 boards with two USB-C ports).
// Everything is on the left header, GPIO 4..15. Avoids GPIO 35-37 (used by
// octal PSRAM on R8 modules), 19/20 (USB), 0/3/45/46 (strapping), 43/44 (UART).
static const gpio_num_t LED1_PIN = GPIO_NUM_7;
static const gpio_num_t LED2_PIN = GPIO_NUM_8;
static const gpio_num_t TONE_PIN = GPIO_NUM_15;   // reference square wave (optional)
static const int TFT_SCK = 12, TFT_MOSI = 11, TFT_CS = 10, TFT_DC = 9,
                 TFT_RST = 14, TFT_BL = 13;
static const int ENC_A = 4, ENC_B = 5, ENC_SW = 6, BOOT_BTN = 0;
#else
// ESP32-S2 (Saola-1, DevKitM-1, Lolin S2 Mini).
static const gpio_num_t LED1_PIN = GPIO_NUM_7;
static const gpio_num_t LED2_PIN = GPIO_NUM_8;
static const gpio_num_t TONE_PIN = GPIO_NUM_12;   // reference square wave (optional)
static const int TFT_SCK = 36, TFT_MOSI = 35, TFT_CS = 34, TFT_DC = 38,
                 TFT_RST = 33, TFT_BL = 21;
static const int ENC_A = 9, ENC_B = 10, ENC_SW = 11, BOOT_BTN = 0;
#endif
static const int ENC_COUNTS_PER_DETENT = 4;  // most encoders; try 2 if it skips
static const bool ENC_REVERSE = false;       // flip if CW goes the wrong way

static const uint32_t SPI_HZ = 40000000;     // GC9A01 is usually fine up to 80 MHz

// ---- Settings ---------------------------------------------------------------
static tuner_settings_t settings;
static Preferences prefs;
static const uint32_t SETTINGS_VERSION = 2;  // bump if tuner_settings_t changes
static uint32_t saveDueAt = 0;               // 0 = nothing to save

static void loadSettings() {
  tuner_defaults(&settings);
  prefs.begin("strobetuner", false);
  if (prefs.getUInt("ver", 0) == SETTINGS_VERSION &&
      prefs.getBytesLength("s") == sizeof(settings)) {
    prefs.getBytes("s", &settings, sizeof(settings));
  }
  tuner_sanitize(&settings);
}

static void saveSettings() {
  prefs.putUInt("ver", SETTINGS_VERSION);
  prefs.putBytes("s", &settings, sizeof(settings));
}

// ---- Strobe engine (hardware timer ISR; schedule in src/core/strobe_math.h) ----
static gptimer_handle_t gTimer = nullptr;
static strobe_plan_t gPlan;
static strobe_state_t gStrobe;
static bool gRunning = false;
static volatile bool gToneOn = false;

static bool IRAM_ATTR onAlarm(gptimer_handle_t timer,
                              const gptimer_alarm_event_data_t *, void *) {
  int led, level;
  uint64_t next = strobe_step(&gStrobe, &gPlan, &led, &level);
  gpio_set_level(led ? LED2_PIN : LED1_PIN, level);
  // Reference tone: high from the red flash to the green flash, low for the other
  // half -- a square wave at exactly the strobe frequency, from the same timer.
  if (gToneOn && level) gpio_set_level(TONE_PIN, led == 0);
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = next;
  gptimer_set_alarm_action(timer, &alarm);
  return false;
}

static void strobeStop() {
  if (gRunning) { gptimer_stop(gTimer); gRunning = false; }
  gpio_set_level(LED1_PIN, 0);
  gpio_set_level(LED2_PIN, 0);
  gpio_set_level(TONE_PIN, 0);
}

static void strobeStart(double freqHz, double flashUs, double trimPpm) {
  strobeStop();                                // timer stopped: safe to change plan
  gPlan = strobe_plan(freqHz, flashUs, trimPpm);
  gStrobe.base_q16 = (uint64_t)100 << 16;      // first flash 100 us after start
  gStrobe.state = 0;
  gptimer_set_raw_count(gTimer, 0);
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = gStrobe.base_q16 >> 16;
  gptimer_set_alarm_action(gTimer, &alarm);
  gptimer_start(gTimer);
  gRunning = true;
}

static void strobeInit() {
  for (gpio_num_t p : {LED1_PIN, LED2_PIN, TONE_PIN}) {
    gpio_reset_pin(p);
    gpio_set_direction(p, GPIO_MODE_INPUT_OUTPUT);   // input too, for the self-test
    gpio_set_drive_capability(p, GPIO_DRIVE_CAP_3);
    gpio_set_level(p, 0);
  }
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;       // APB 80 MHz from the crystal
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;                 // 1 tick = 1 us
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &gTimer));
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onAlarm;
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(gTimer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(gTimer));
}

// ---- Strobe self-test ----------------------------------------------------------
// Timestamps every edge on both LED pins (GPIO interrupt + esp_timer, 1 us) while
// the strobe runs, then compares what actually came out of the pins with what
// was asked for. This proves the firmware: no missed or late flashes, right
// period, flash length and red/green phase. It uses the same crystal as the
// strobe, so crystal error (~10-20 ppm) needs an outside reference to see.
struct EdgeStats {
  volatile uint32_t rises, widthN;
  volatile int64_t firstRise, lastRise, minP, maxP, widthSum;
};
static EdgeStats gEdge[2];
static volatile int64_t gPhaseSum;
static volatile uint32_t gPhaseN;

static void IRAM_ATTR onLedEdge(void *arg) {
  int i = (int)(intptr_t)arg;
  int64_t t = esp_timer_get_time();
  EdgeStats &e = gEdge[i];
  if (gpio_get_level(i ? LED2_PIN : LED1_PIN)) {             // rising edge
    if (e.rises) {
      int64_t p = t - e.lastRise;
      if (p < e.minP) e.minP = p;
      if (p > e.maxP) e.maxP = p;
    } else {
      e.firstRise = t;
    }
    e.lastRise = t;
    e.rises = e.rises + 1;
    if (i == 1 && gEdge[0].rises) { gPhaseSum = gPhaseSum + (t - gEdge[0].lastRise); gPhaseN = gPhaseN + 1; }
  } else if (e.rises) {                                       // falling edge
    e.widthSum = e.widthSum + (t - e.lastRise);
    e.widthN = e.widthN + 1;
  }
}

static void runSelfTest(uint32_t seconds) {
  if (!gRunning) { Serial.println("Strobe is off - turn it on first."); return; }
  double target = tuner_target_hz(&settings);
  double wantP = gPlan.period_q16 / 65536.0;   // what the engine was asked for
  double wantW = gPlan.flash_q16 / 65536.0;
  int m = tuner_target_midi(&settings);
  Serial.printf("\nSelf-test: %s%d %.3f Hz for %u s ...\n", tuner_note_letter(m),
                tuner_note_octave(m), target, seconds);

  memset((void *)gEdge, 0, sizeof(gEdge));
  for (auto &e : gEdge) e.minP = INT64_MAX;
  gPhaseSum = 0; gPhaseN = 0;
  gpio_install_isr_service(0);                 // already installed is fine
  const gpio_num_t pins[2] = {LED1_PIN, LED2_PIN};
  for (int i = 0; i < 2; i++) {
    gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(pins[i], onLedEdge, (void *)(intptr_t)i);
    gpio_intr_enable(pins[i]);
  }
  uint32_t end = millis() + seconds * 1000;
  while ((int32_t)(millis() - end) < 0) { lv_timer_handler(); delay(5); }
  for (int i = 0; i < 2; i++) {
    gpio_intr_disable(pins[i]);
    gpio_isr_handler_remove(pins[i]);
    gpio_set_intr_type(pins[i], GPIO_INTR_DISABLE);
  }

  bool ok = true;
  const char *names[2] = {"LED1 red  ", "LED2 green"};
  for (int i = 0; i < 2; i++) {
    EdgeStats &e = gEdge[i];
    if (e.rises < 3) { Serial.printf("%s  no flashes seen!\n", names[i]); ok = false; continue; }
    double P = (double)(e.lastRise - e.firstRise) / (e.rises - 1);
    double cents = 1200.0 * log2(wantP / P);
    double expectN = (e.lastRise - e.firstRise) / wantP + 1;
    double W = e.widthN ? (double)e.widthSum / e.widthN : 0;
    Serial.printf("%s  %lu flashes  period %.4f us (want %.4f)  error %+.4f cents  "
                  "jitter %lld..%lld us  flash %.1f us (want %.0f)\n",
                  names[i], (unsigned long)e.rises, P, wantP, cents,
                  (long long)e.minP, (long long)e.maxP, W, wantW);
    if (fabs(cents) > 0.02 || fabs(expectN - e.rises) > 1.5 || fabs(W - wantW) > 15) ok = false;
  }
  if (gPhaseN) {
    double ph = (double)gPhaseSum / gPhaseN;
    Serial.printf("Green after red: %.1f us (want %.1f = half a period)\n", ph, wantP / 2);
    if (fabs(ph - wantP / 2) > 10) ok = false;
  }
  Serial.println(ok ? "PASS: LEDs flash at the requested frequency."
                    : "FAIL: output does not match the request (see above).");
  Serial.println("Note: this checks the firmware against the board's own crystal. To check the\n"
                 "crystal itself, measure GPIO7 with a frequency counter or logic analyser.");
}

// ---- Apply settings (called by the UI after every change) --------------------
static void setBacklight(int percent) {
  ledcWrite(TFT_BL, (uint32_t)percent * 255 / 100);
}

extern "C" void platform_settings_changed(const tuner_settings_t *s) {
  gToneOn = s->ref_tone;
  if (!s->ref_tone) gpio_set_level(TONE_PIN, 0);
  if (s->strobe_on) {
    // Restart the timer only if the timing changed (not for brightness etc.).
    strobe_plan_t p = strobe_plan(tuner_target_hz(s), s->flash_us, s->trim_tenths_ppm / 10.0);
    if (!gRunning || memcmp(&p, &gPlan, sizeof p) != 0)
      strobeStart(tuner_target_hz(s), s->flash_us, s->trim_tenths_ppm / 10.0);
  } else {
    strobeStop();
  }
  setBacklight(s->brightness);
  saveDueAt = millis() + 2000;   // save once the knob has been still for 2 s
  if (saveDueAt == 0) saveDueAt = 1;
}

// ---- Display (GC9A01, drawn by LVGL) -----------------------------------------
static const int W = 240, H = 240, BUF_LINES = 24;
static uint16_t lvBuf1[W * BUF_LINES], lvBuf2[W * BUF_LINES];
static lv_display_t *disp;

static void IRAM_ATTR flushDone(void *) { lv_display_flush_ready(disp); }

static void flushCb(lv_display_t *, const lv_area_t *a, uint8_t *px) {
  lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));   // panel wants big-endian
  gc9a01_draw(a->x1, a->y1, a->x2, a->y2, px);       // DMA; flushDone() when sent
}

static uint32_t tickCb() { return millis(); }

static void displayInit() {
  ledcAttach(TFT_BL, 5000, 8);
  setBacklight(0);
  if (!gc9a01_init(TFT_SCK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST, SPI_HZ, flushDone, nullptr))
    Serial.println("GC9A01 init failed - check wiring/pins");

  lv_init();
  lv_tick_set_cb(tickCb);
  disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, lvBuf1, lvBuf2, sizeof(lvBuf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
}

// ---- Rotary encoder (hardware pulse counter) + buttons ------------------------
static pcnt_unit_handle_t encUnit = nullptr;

static void encoderInit() {
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -1000;
  ucfg.high_limit = 1000;
  ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &encUnit));
  pcnt_glitch_filter_config_t filt = {};
  filt.max_glitch_ns = 1000;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(encUnit, &filt));

  // Standard x4 quadrature decoding with two channels.
  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = ENC_A;
  ca.level_gpio_num = ENC_B;
  pcnt_channel_handle_t chA;
  ESP_ERROR_CHECK(pcnt_new_channel(encUnit, &ca, &chA));
  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = ENC_B;
  cb.level_gpio_num = ENC_A;
  pcnt_channel_handle_t chB;
  ESP_ERROR_CHECK(pcnt_new_channel(encUnit, &cb, &chB));
  pcnt_channel_set_edge_action(chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  gpio_pullup_en((gpio_num_t)ENC_A);
  gpio_pullup_en((gpio_num_t)ENC_B);

  ESP_ERROR_CHECK(pcnt_unit_enable(encUnit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(encUnit));
  ESP_ERROR_CHECK(pcnt_unit_start(encUnit));

  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(BOOT_BTN, INPUT_PULLUP);
}

static void pollEncoder() {
  static int last = 0;
  int count = 0;
  pcnt_unit_get_count(encUnit, &count);
  int diff = count - last;
  while (diff >= ENC_COUNTS_PER_DETENT) {
    ui_key(ENC_REVERSE ? UI_KEY_CCW : UI_KEY_CW);
    diff -= ENC_COUNTS_PER_DETENT; last += ENC_COUNTS_PER_DETENT;
  }
  while (diff <= -ENC_COUNTS_PER_DETENT) {
    ui_key(ENC_REVERSE ? UI_KEY_CW : UI_KEY_CCW);
    diff += ENC_COUNTS_PER_DETENT; last -= ENC_COUNTS_PER_DETENT;
  }
  if (abs(count) > 900) { pcnt_unit_clear_count(encUnit); last -= count; }
}

static void pollButton() {
  static bool down = false, longSent = false;
  static uint32_t downAt = 0, lastEdge = 0;
  bool pressed = digitalRead(ENC_SW) == LOW || digitalRead(BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (pressed != down && now - lastEdge > 25) {          // debounce
    lastEdge = now;
    down = pressed;
    if (down) { downAt = now; longSent = false; }
    else if (!longSent) ui_key(UI_KEY_PRESS);
  }
  if (down && !longSent && now - downAt > 600) {        // hold = long press
    longSent = true;
    ui_key(UI_KEY_LONG_PRESS);
  }
}

// ---- Serial commands (optional, 115200 baud) ----------------------------------
//   tuning 1..5      Standard, Drop D, Half step down, Full step down, Open
//   next / prev      string        on / off   strobe
//   a4=442  trim=-12.5  tone on|off
//   test [seconds]   measure the real LED output (default 10 s)
static void handleCommand(String cmd) {
  cmd.trim();
  String lc = cmd; lc.toLowerCase();
  if (lc.length() == 0) return;
  if (lc == "test" || lc.startsWith("test ")) {
    int sec = lc.length() > 5 ? lc.substring(5).toInt() : 10;
    runSelfTest(sec < 2 ? 2 : sec > 120 ? 120 : sec);
    return;
  }
  if (lc == "off" || lc == "on") settings.strobe_on = lc == "on";
  else if (lc == "next") tuner_step(&settings, 1);
  else if (lc == "prev") tuner_step(&settings, -1);
  else if (lc.startsWith("tuning ")) { settings.tuning = lc.substring(7).toInt() - 1; settings.string_idx = 0; }
  else if (lc.startsWith("a4=")) settings.a4_tenths = lroundf(lc.substring(3).toFloat() * 10);
  else if (lc.startsWith("trim=")) settings.trim_tenths_ppm = lroundf(lc.substring(5).toFloat() * 10);
  else if (lc == "tone on" || lc == "tone off") settings.ref_tone = lc == "tone on";
  else {
    Serial.println("?  try: tuning 2, next, prev, on, off, a4=442, trim=-10, tone on, test");
    return;
  }
  tuner_sanitize(&settings);
  platform_settings_changed(&settings);
  ui_refresh();
  int m = tuner_target_midi(&settings);
  Serial.printf("%s, string %d: %s%d  %.3f Hz  strobe %s  tone %s\n",
                tuner_tuning_name(settings.tuning), TUNER_STRINGS - settings.string_idx,
                tuner_note_letter(m), tuner_note_octave(m), tuner_target_hz(&settings),
                settings.strobe_on ? "on" : "off", settings.ref_tone ? "on" : "off");
}

static void pollSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(line); line = ""; }
    else if (line.length() < 32) line += c;
  }
}

// ---- Arduino entry points ---------------------------------------------------
void setup() {
  Serial.begin(115200);
  loadSettings();
  strobeInit();
  displayInit();
  encoderInit();
  ui_init(&settings);
  lv_timer_handler();                     // draw the first frame before backlight
  platform_settings_changed(&settings);  // start strobe, set backlight
  saveDueAt = 0;                          // nothing new to save yet
}

void loop() {
  pollEncoder();
  pollButton();
  pollSerial();
  if (saveDueAt && (int32_t)(millis() - saveDueAt) >= 0) { saveDueAt = 0; saveSettings(); }
  uint32_t wait = lv_timer_handler();
  delay(wait > 5 ? 5 : wait);
}

// SkiTracker — GPS-трекер для беговых лыж с BLE-пульсометром
//
// Кнопки:
//   правая коротко   — следующий экран (основной / статус / сигнал GPS)
//   правая долго     — старт / стоп записи
//   левая коротко    — выключить экран
//   левая 1.5 с и отпустить — режим выгрузки по Wi-Fi вкл/выкл
//   левая 4 с        — выключить трекер (трек сохраняется)
//   правая 2 с       — включить трекер
//   обе 4 с          — забыть запомненный пульсометр
//   при выключенном экране любое нажатие только включает его

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"
#include "button.h"
#include "gps.h"
#include "hr.h"
#include "track.h"
#include "ui.h"
#include "web.h"
#ifdef SELFTEST
#include "selftest.h"
#endif

static Button btnL(PIN_BTN_LEFT, true, BTN_OFF_MS);
static Button btnR(PIN_BTN_RIGHT, false);

static bool autoStartDone = false;
static bool comboDone = false;
static uint32_t lastUiMs = 0, lastPrintMs = 0, lastActivityMs = 0;
static uint32_t lastBatMs = 0, lastFreeMs = 0;
static float batV = 0;
static uint32_t freeKB = 0;
static uint8_t warnCount = 0, shutdownCount = 0;
static uint32_t lastWarnMs = 0;
static bool warned = false;

static float readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  return mv / 8.0f * 2 / 1000.0f * BAT_CAL;   // делитель 1:2
}

// Батарея есть и меряется (ниже 2.5 В — АЦП не работает/нет делителя: защиту не трогаем)
static bool onBattery(float v) { return v > 2.5f && v < BAT_USB_V; }

// Глубокий сон ESP32. Будит правая кнопка.
static void deepSleep() {
  // если правая кнопка ещё нажата — дождаться отпускания, иначе сразу проснёмся
  uint32_t t0 = millis();
  while (digitalRead(PIN_BTN_RIGHT) == LOW && millis() - t0 < 10000) delay(10);

  digitalWrite(PIN_ADC_EN, LOW);   // делитель не тратит батарею во сне
  digitalWrite(TFT_BL, LOW);
  gpio_hold_en((gpio_num_t)PIN_ADC_EN);
  gpio_hold_en((gpio_num_t)TFT_BL);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_RIGHT, 0);
  Serial.flush();
  esp_deep_sleep_start();
}

// Сохранить трек, усыпить GPS, дисплей и ESP32.
static void powerOff(const char* title, const char* text) {
  Track::stop();
  Web::stop();
  Serial.printf("Power off: %s (%.2fV)\n", text, batV);
  Ui::showMessage(title, text);
  delay(2500);
  Ui::sleep();
  Gps::sleep();
  deepSleep();
}

static void checkBattery() {
  if (!onBattery(batV)) {               // USB или нет замера — счётчики сбрасываем
    warnCount = shutdownCount = 0;
    warned = false;
    return;
  }
  shutdownCount = batV < BAT_SHUTDOWN_V ? shutdownCount + 1 : 0;
  warnCount = batV < BAT_WARN_V ? warnCount + 1 : 0;

  if (shutdownCount >= BAT_LOW_COUNT) powerOff("Battery empty", "Track saved. Charge me!");

  if (warnCount >= BAT_LOW_COUNT && (!warned || millis() - lastWarnMs > BAT_WARN_REPEAT_MS)) {
    warned = true;
    lastWarnMs = millis();
    Ui::setBacklight(true);
    lastActivityMs = millis();
    Ui::toast("LOW BATTERY!", 5000);
  }
}

static void toggleRecording() {
  if (Track::active()) {
    Track::stop();
    Ui::toast("Recording stopped");
  } else if (Web::active()) {
    Ui::toast("Exit WiFi mode first");
  } else if (Track::start(Gps::data())) {
    autoStartDone = true;
    Ui::toast("Recording started");
  } else {
    Ui::toast("Start failed (memory?)");
  }
}

static void toggleWifi() {
  if (Web::active()) {
    Web::stop();
    Ui::toast("WiFi off");
  } else if (Track::active()) {
    Ui::toast("Stop recording first");
  } else if (Web::start()) {
    Ui::toast("WiFi on");
  } else {
    Ui::toast("WiFi failed");
  }
}

static void handleButtons() {
  Button::Event l = btnL.update();
  Button::Event r = btnR.update();

  if (btnL.pressed() && btnR.pressed()) {
    btnL.suppress();
    btnR.suppress();
    if (!comboDone && btnL.heldMs() > BTN_COMBO_MS && btnR.heldMs() > BTN_COMBO_MS) {
      comboDone = true;
      Hr::forgetDevice();
      Ui::setBacklight(true);
      Ui::toast("HR strap forgotten");
    }
    lastActivityMs = millis();
    return;
  }
  if (!btnL.pressed() && !btnR.pressed()) comboDone = false;

  // выключение — работает и при погашенном экране
  if (l == Button::VeryLong) powerOff("Power off", "Hold RIGHT 2 s to turn on");

  // подсказка, пока держим левую кнопку
  uint32_t held = btnL.heldMs();
  if (held > BTN_LONG_MS && !btnL.suppressed()) {
    static char hint[32];
    snprintf(hint, sizeof(hint), "Release: WiFi  Hold: OFF %lu", (unsigned long)((BTN_OFF_MS - held) / 1000 + 1));
    Ui::setBacklight(true);
    Ui::toast(hint, 300);
    lastActivityMs = millis();
  }

  if (l == Button::None && r == Button::None) return;

  lastActivityMs = millis();
  if (!Ui::backlight()) {          // первое нажатие только будит экран
    Ui::setBacklight(true);
    return;
  }
  if (r == Button::Short) Ui::nextScreen();
  if (r == Button::Long) toggleRecording();
  if (l == Button::Short) Ui::setBacklight(false);
  if (l == Button::Long) toggleWifi();
}

static void printStatus() {
  const GpsData& g = Gps::data();
  HrData h = Hr::get();
  Serial.printf("GPS chars=%lu err=%lu fix=%d sats=%u hdop=%.1f lat=%.6f lon=%.6f alt=%.1f spd=%.1f | "
                "HR %s bpm=%u fresh=%d | REC=%d pts=%lu | bat=%.2fV\n",
                (unsigned long)g.charsProcessed, (unsigned long)g.failedChecksum, g.fix, g.sats, g.hdop,
                g.lat, g.lon, g.altM, g.speedKmh, Hr::stateName(h.state), h.bpm, Hr::fresh(),
                Track::active(), (unsigned long)Track::stats().points, batV);
  Serial.printf("SIG valid=%d heard=%u/%u max=%u top4=%u :", g.gsvValid, g.satHeard, g.satCount, g.snrMax, g.snrTop4);
  for (uint8_t i = 0; i < g.satCount; i++) Serial.printf(" %c%u=%u", g.sat[i].sys, g.sat[i].prn, g.sat[i].snr);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);   // экономия энергии; BLE и Wi-Fi работают на 80 МГц

  // Разбудили правой кнопкой — включаемся, только если её держат BTN_ON_MS.
  // Случайное касание (в сумке, в кармане) — молча засыпаем обратно; экран и GPS не трогаем.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    pinMode(PIN_BTN_RIGHT, INPUT);
    uint32_t t0 = millis();
    while (digitalRead(PIN_BTN_RIGHT) == LOW && millis() - t0 < BTN_ON_MS) delay(10);
    if (millis() - t0 < BTN_ON_MS) deepSleep();
  }

  // после глубокого сна пины были «заморожены» — отпускаем
  gpio_hold_dis((gpio_num_t)PIN_ADC_EN);
  gpio_hold_dis((gpio_num_t)TFT_BL);
  gpio_deep_sleep_hold_dis();

  pinMode(PIN_ADC_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, HIGH);
  btnL.begin();
  btnR.begin();

  Ui::begin();

  // Проснулись кнопкой, а батарея всё ещё пустая — сразу обратно в сон
  delay(50);
  batV = readBattery();
  if (onBattery(batV) && batV < BAT_SHUTDOWN_V + 0.05f) {
    Gps::begin();   // разбудить модуль, чтобы корректно усыпить снова
    powerOff("Battery low", "Please charge");
  }

  Gps::begin();
  bool fsOk = Track::begin();
  Hr::begin();

  batV = readBattery();
  Web::setBattery(batV);
  freeKB = Track::freeBytes() / 1024;
  lastActivityMs = millis();

  Serial.println();
  Serial.println("=== SkiTracker ===");
  Serial.printf("UBX config ack: %s\n", Gps::data().ubxAck ? "yes" : "NO (module off, clone or RX wiring)");
  Serial.printf("LittleFS: %s, free %lu KB\n", fsOk ? "ok" : "FAILED", (unsigned long)freeKB);
  if (!fsOk) Ui::toast("Filesystem error!", 5000);
#ifdef SELFTEST
  SelfTest::start();
#endif
}

void loop() {
  Gps::update();
  handleButtons();

  if (Gps::newFix()) {
    const GpsData& g = Gps::data();
#if AUTO_START_ON_FIX
    if (!autoStartDone && !Track::active() && !Web::active() && g.timeValid && g.sats >= 4) {
      autoStartDone = true;
      if (Track::start(g)) Ui::toast("Auto recording started");
    }
#endif
    if (Track::active()) Track::onFix(g, Hr::fresh() ? Hr::get().bpm : 0);
  }

  Web::loop();

  uint32_t now = millis();
  if (now - lastBatMs >= BAT_CHECK_MS) {
    lastBatMs = now;
    batV = readBattery();
    Web::setBattery(batV);
    checkBattery();
  }
  if (now - lastFreeMs >= 15000) { lastFreeMs = now; freeKB = Track::freeBytes() / 1024; }

  if (SCREEN_TIMEOUT_S > 0 && Ui::backlight() && !Web::active() &&
      now - lastActivityMs > SCREEN_TIMEOUT_S * 1000UL) {
    Ui::setBacklight(false);
  }

  if (now - lastUiMs >= 250 && Ui::backlight()) {
    lastUiMs = now;
    UiModel m;
    m.gps = Gps::data();
    m.hr = Hr::get();
    m.hrFresh = Hr::fresh();
    m.st = Track::stats();
    m.file = Track::fileName();
    m.freeKB = freeKB;
    m.batV = batV;
    m.wifi = Web::active();
    m.ip = Web::ip();
    Ui::draw(m);
  }

  if (now - lastPrintMs >= 1000) {
    lastPrintMs = now;
    printStatus();
  }
  delay(2);
}

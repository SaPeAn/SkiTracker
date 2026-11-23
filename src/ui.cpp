#include "ui.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <time.h>

// Встроенные шрифты TFT_eSPI — только латиница, поэтому надписи на экране английские.
static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool sprOk = false;
static bool blOn = true;
static uint8_t screen = 0;
static const uint8_t SCREEN_COUNT = 3;
static char toastText[32] = "";
static uint32_t toastUntil = 0;

static const int W = 240, H = 135;

static int batPercent(float v) {
  int p = (int)((v - BAT_SHUTDOWN_V) / (BAT_FULL_V - BAT_SHUTDOWN_V) * 100.0f);
  return constrain(p, 0, 100);
}

static void fmtDuration(uint32_t s, char* buf, size_t n) {
  snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
}

static void drawTopBar(TFT_eSPI& c, const UiModel& m) {
  char buf[24];
  c.setTextDatum(TL_DATUM);
  if (m.st.active) {
    c.fillCircle(7, 8, 5, TFT_RED);
    c.setTextColor(TFT_RED, TFT_BLACK);
    c.drawString("REC", 15, 1, 2);
  } else {
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.drawString("IDLE", 2, 1, 2);
  }

  uint16_t gpsCol = !m.gps.dataFlowing ? TFT_RED : (m.gps.fix ? TFT_GREEN : TFT_YELLOW);
  c.setTextColor(gpsCol, TFT_BLACK);
  snprintf(buf, sizeof(buf), "GPS %u", m.gps.sats);
  c.drawString(buf, 48, 1, 2);

  uint16_t hrCol = m.hrFresh ? TFT_GREEN : (m.hr.state == HrState::Connected ? TFT_YELLOW : TFT_DARKGREY);
  c.setTextColor(hrCol, TFT_BLACK);
  c.drawString("HR", 102, 1, 2);

  if (m.gps.timeValid) {
    time_t t = (time_t)m.gps.epoch + TZ_OFFSET_MIN * 60;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.drawString(buf, 132, 1, 2);
  }

  c.setTextDatum(TR_DATUM);
  if (m.batV > BAT_USB_V) {
    c.setTextColor(TFT_CYAN, TFT_BLACK);
    c.drawString("USB", W - 2, 1, 2);
  } else {
    int p = batPercent(m.batV);
    bool low = m.batV > 2.5f && m.batV < BAT_WARN_V;
    bool blink = low && (millis() / 500) % 2;          // при разряде индикатор мигает
    if (blink) c.fillRect(W - 40, 0, 40, 18, TFT_RED);
    uint16_t fg = low ? (blink ? TFT_WHITE : TFT_RED) : (p > 20 ? TFT_WHITE : TFT_ORANGE);
    c.setTextColor(fg, blink ? TFT_RED : TFT_BLACK);
    snprintf(buf, sizeof(buf), "%d%%", p);
    c.drawString(buf, W - 2, 1, 2);
  }
  c.drawFastHLine(0, 19, W, TFT_DARKGREY);
}

static void drawCell(TFT_eSPI& c, int cx, const char* label, const char* value) {
  c.setTextDatum(TC_DATUM);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.drawString(label, cx, 88, 2);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.drawString(value, cx, 106, 4);
}

static void drawMain(TFT_eSPI& c, const UiModel& m) {
  char buf[24];
  drawTopBar(c, m);

  // скорость — крупно
  c.setTextDatum(TR_DATUM);
  c.setTextColor(m.gps.fix ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%.1f", m.gps.fix ? m.gps.speedKmh : 0.0f);
  c.drawString(buf, 150, 28, 7);
  c.setTextDatum(TL_DATUM);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.drawString("km/h", 154, 60, 2);

  // пульс
  c.setTextDatum(TR_DATUM);
  c.setTextColor(m.hrFresh ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  if (m.hrFresh) snprintf(buf, sizeof(buf), "%u", m.hr.bpm);
  else strcpy(buf, "--");
  c.drawString(buf, W - 4, 28, 4);
  c.drawString("bpm", W - 4, 56, 2);

  c.drawFastHLine(0, 84, W, TFT_DARKGREY);

  snprintf(buf, sizeof(buf), "%.2f", m.st.distM / 1000.0);
  drawCell(c, 38, "KM", buf);
  fmtDuration(m.st.elapsedS, buf, sizeof(buf));
  drawCell(c, 122, "TIME", buf);
  snprintf(buf, sizeof(buf), "+%d", (int)m.st.ascentM);
  drawCell(c, 204, "ASC m", buf);
}

static void line(TFT_eSPI& c, int row, uint16_t col, const char* text) {
  c.setTextDatum(TL_DATUM);
  c.setTextColor(col, TFT_BLACK);
  c.drawString(text, 2, 22 + row * 14, 2);
}

static void drawStatus(TFT_eSPI& c, const UiModel& m) {
  char buf[64];
  drawTopBar(c, m);
  const GpsData& g = m.gps;

  if (!g.dataFlowing)
    line(c, 0, TFT_RED, "GPS: NO DATA - check TX/RX wiring");
  else {
    snprintf(buf, sizeof(buf), "GPS: chars %lu err %lu UBX %s", (unsigned long)g.charsProcessed,
             (unsigned long)g.failedChecksum, g.ubxAck ? "ok" : "no ack");
    line(c, 0, TFT_WHITE, buf);
  }
  snprintf(buf, sizeof(buf), "Fix: %s  sats %u  hdop %.1f", g.fix ? "yes" : "no", g.sats, g.hdop);
  line(c, 1, g.fix ? TFT_GREEN : TFT_YELLOW, buf);
  snprintf(buf, sizeof(buf), "%.6f, %.6f  %.0fm", g.lat, g.lon, g.altM);
  line(c, 2, TFT_WHITE, buf);

  snprintf(buf, sizeof(buf), "HR: %s  %u bpm%s", Hr::stateName(m.hr.state), m.hr.bpm,
           m.hr.contact ? "" : " (no contact)");
  line(c, 3, m.hrFresh ? TFT_GREEN : TFT_YELLOW, buf);
  if (m.hr.saved) {
    snprintf(buf, sizeof(buf), "%s  bat %d%%  %s", m.hr.name[0] ? m.hr.name : "strap", m.hr.battery,
             m.hr.addr + (strlen(m.hr.addr) > 8 ? strlen(m.hr.addr) - 8 : 0));
  } else {
    snprintf(buf, sizeof(buf), "no strap paired (RSSI > %d)", HR_MIN_RSSI);
  }
  line(c, 4, TFT_DARKGREY, buf);

  snprintf(buf, sizeof(buf), "Log: %s", m.st.active ? m.file : "-");
  line(c, 5, TFT_WHITE, buf);
  snprintf(buf, sizeof(buf), "Free %lu KB  pts %lu  bat %.2fV", (unsigned long)m.freeKB,
           (unsigned long)m.st.points, m.batV);
  line(c, 6, TFT_WHITE, buf);
  snprintf(buf, sizeof(buf), "Max %.1f km/h  HR avg %u max %u", m.st.maxSpeedKmh, m.st.hrAvg, m.st.hrMax);
  line(c, 7, TFT_DARKGREY, buf);
}

// Сигнал спутников: столбик = спутник, высота = C/N0 (дБГц), сильные слева
static void drawSignal(TFT_eSPI& c, const UiModel& m) {
  char buf[48];
  drawTopBar(c, m);
  const GpsData& g = m.gps;
  if (!g.dataFlowing) { line(c, 0, TFT_RED, "GPS: NO DATA - check power/TX"); return; }
  if (!g.gsvValid) { line(c, 0, TFT_YELLOW, "Waiting for signal data..."); return; }

  uint16_t col = g.snrTop4 >= 35 ? TFT_GREEN : (g.snrTop4 >= 25 ? TFT_YELLOW : TFT_RED);
  snprintf(buf, sizeof(buf), "Heard %u of %u   top4 %u   max %u dB", g.satHeard, g.satCount, g.snrTop4, g.snrMax);
  line(c, 0, g.satHeard >= 4 ? col : TFT_RED, buf);

  const int base = 121, maxH = 68, full = 50, colW = 17;   // 14 столбиков по ширине
  for (int lv = 10; lv <= 40; lv += 15) {                  // 10 / 25 / 40 дБГц
    int yy = base - lv * maxH / full;
    c.drawFastHLine(0, yy, W, 0x2104);
  }
  c.drawFastHLine(0, base, W, TFT_DARKGREY);
  c.setTextDatum(TC_DATUM);
  for (uint8_t i = 0; i < g.satCount && i < W / colW; i++) {
    const SatSignal& s = g.sat[i];
    int x = i * colW, h = (s.snr > full ? full : s.snr) * maxH / full;
    uint16_t bc = s.snr >= 35 ? TFT_GREEN : (s.snr >= 25 ? TFT_YELLOW : TFT_RED);
    c.fillRect(x + 2, base - h, colW - 4, h, bc);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    if (s.snr) {
      snprintf(buf, sizeof(buf), "%u", s.snr);
      c.drawString(buf, x + colW / 2, base - h - 10, 1);
    }
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%u", s.prn);
    c.drawString(buf, x + colW / 2, base + 4, 1);
  }
}

static void drawWifi(TFT_eSPI& c, const UiModel& m) {
  char buf[48];
  drawTopBar(c, m);
  c.setTextDatum(TL_DATUM);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.drawString("WiFi export", 4, 26, 4);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "SSID: %s", WIFI_AP_SSID);
  c.drawString(buf, 4, 58, 2);
  snprintf(buf, sizeof(buf), "Pass: %s", WIFI_AP_PASS);
  c.drawString(buf, 4, 76, 2);
  snprintf(buf, sizeof(buf), "Open: http://%s", m.ip);
  c.drawString(buf, 4, 94, 2);
  c.setTextColor(TFT_DARKGREY, TFT_BLACK);
  c.drawString("Hold LEFT to exit", 4, 116, 2);
}

void Ui::begin() {
  tft.init();
  tft.setRotation(1);            // горизонтально, кнопки справа
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  spr.setColorDepth(8);
  sprOk = spr.createSprite(W, H) != nullptr;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("SkiTracker", 60, 50, 4);
}

void Ui::draw(const UiModel& m) {
  TFT_eSPI& c = sprOk ? (TFT_eSPI&)spr : tft;
  // fillScreen() у спрайта берёт размеры экрана в исходной ориентации (135x240)
  // и стирает только квадрат 135x135 — поэтому очищаем буфер явно.
  if (sprOk) spr.fillSprite(TFT_BLACK);
  else tft.fillScreen(TFT_BLACK);
  if (m.wifi) drawWifi(c, m);
  else if (screen == 0) drawMain(c, m);
  else if (screen == 1) drawStatus(c, m);
  else drawSignal(c, m);

  if (toastText[0] && (int32_t)(toastUntil - millis()) > 0) {
    c.fillRoundRect(10, H - 30, W - 20, 26, 5, TFT_NAVY);
    c.setTextDatum(MC_DATUM);
    c.setTextColor(TFT_WHITE, TFT_NAVY);
    c.drawString(toastText, W / 2, H - 17, 2);
  }
  if (sprOk) spr.pushSprite(0, 0);
}

void Ui::nextScreen() { screen = (screen + 1) % SCREEN_COUNT; }

void Ui::setBacklight(bool on) {
  blOn = on;
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

bool Ui::backlight() { return blOn; }

void Ui::showMessage(const char* title, const char* text) {
  setBacklight(true);
  tft.writecommand(0x11);   // SLPOUT на случай, если дисплей спал
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString(title, W / 2, 50, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(text, W / 2, 90, 2);
}

void Ui::sleep() {
  setBacklight(false);
  tft.writecommand(0x28);   // DISPOFF
  delay(20);
  tft.writecommand(0x10);   // SLPIN
  delay(120);
}

void Ui::toast(const char* text, uint32_t ms) {
  strlcpy(toastText, text, sizeof(toastText));
  toastUntil = millis() + ms;
}

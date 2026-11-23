#pragma once
// Только для env:selftest — запросы к собственному веб-серверу по 192.168.4.1
#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "web.h"

namespace SelfTest {
static void req(const char* method, const char* path, const char* body = "") {
  HTTPClient h;
  h.setTimeout(8000);
  h.begin(String("http://192.168.4.1") + path);
  uint32_t t0 = millis();
  int code;
  if (!strcmp(method, "POST")) {
    h.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = h.POST(body);
  } else {
    code = h.GET();
  }
  String r = code > 0 ? h.getString() : String();
  Serial.printf("TEST %s %s -> %d, len=%u, %lu ms, heap=%u | %s\n", method, path, code, r.length(),
                (unsigned long)(millis() - t0), ESP.getFreeHeap(), r.substring(0, 70).c_str());
  h.end();
}

// Читает ответ потоком, не накапливая в памяти
static void stream(const char* path, const char* mark) {
  WiFiClient c;
  if (!c.connect("192.168.4.1", 80)) {
    Serial.println("TEST STREAM connect failed");
    return;
  }
  c.printf("GET %s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", path);
  uint32_t t0 = millis(), last = t0, total = 0, marks = 0;
  char tail[65] = "";
  size_t tn = 0, ml = strlen(mark);
  static uint8_t buf[512];
  while (millis() - last < 5000 && (c.connected() || c.available())) {
    int n = c.read(buf, sizeof(buf));
    if (n <= 0) { delay(2); continue; }
    last = millis();
    total += n;
    for (int i = 0; i < n; i++) {
      if (tn == 64) { memmove(tail, tail + 1, 63); tn = 63; }
      tail[tn++] = buf[i];
      tail[tn] = 0;
      if (tn >= ml && !memcmp(tail + tn - ml, mark, ml)) marks++;
    }
  }
  Serial.printf("TEST STREAM %s bytes=%lu marks=%lu gpxEnd=%d chunkEnd=%d %lu ms heap=%u\n", path,
                (unsigned long)total, (unsigned long)marks, strstr(tail, "</gpx>") != nullptr,
                tn >= 5 && !memcmp(tail + tn - 5, "0\r\n\r\n", 5), (unsigned long)(last - t0), ESP.getFreeHeap());
  c.stop();
}

// Прогон строки через разбор GSV; контрольную сумму считаем сами
static bool gsvFeed(GsvParser& p, const char* body, uint32_t ms, bool badSum = false) {
  uint8_t ck = badSum ? 1 : 0;
  for (const char* q = body; *q; q++) ck ^= *q;
  char s[110];
  snprintf(s, sizeof(s), "$%s*%02X\r\n", body, ck);
  bool r = false;
  for (const char* q = s; *q; q++) r |= p.encode(*q, ms);
  return r;
}

static void gsvTest() {
  static GsvParser p;
  SatSignal s[24];
  int fails = 0;
  auto check = [&](bool ok, const char* what) { if (!ok) { fails++; Serial.printf("TEST GSV FAIL: %s\n", what); } };
  check(!gsvFeed(p, "GPGSV,2,1,06,02,50,120,38,05,10,040,,12,70,200,44,25,30,300,21", 1000), "part 1 is not a full list");
  check(gsvFeed(p, "GPGSV,2,2,06,29,15,090,00,31,05,010,33", 1000), "part 2 completes");
  uint8_t n = p.list(s, 24, 1000);
  check(n == 6 && s[0].prn == 12 && s[0].snr == 44 && s[1].snr == 38 && s[2].snr == 33 && s[3].snr == 21 && s[4].snr == 0, "sorted");
  check(gsvFeed(p, "GLGSV,1,1,01,70,40,100,40", 1500), "glonass");
  n = p.list(s, 24, 1500);
  check(n == 7 && s[1].sys == 'L' && s[1].prn == 70, "merged");
  check(!gsvFeed(p, "GPGSV,1,1,01,09,40,100,49", 2000, true), "bad checksum rejected");
  check(!gsvFeed(p, "GPGSV,3,2,09,09,40,100,49", 2000), "lost first part rejected");
  check(p.list(s, 24, 2000) == 7, "old list kept");
  check(gsvFeed(p, "GPGSV,1,1,00", 2500) && p.list(s, 24, 2500) == 1, "empty GPS list");
  check(p.list(s, 24, 20000) == 0, "stale dropped");
  check(!gsvFeed(p, "GPRMC,,V,,,,,,,,,,N", 3000), "other sentence ignored");
  for (int i = 0; i < 300; i++) p.encode('x', 3000);
  check(gsvFeed(p, "GPGSV,1,1,01,09,40,100,41", 3000), "recovers after garbage");
  check(p.list(s, 3, 3000) <= 3, "limit");
  Serial.printf("TEST GSV %s\n", fails ? "FAILED" : "OK");
}

static void task(void*) {
  vTaskDelay(pdMS_TO_TICKS(3000));
  gsvTest();
  vTaskDelay(pdMS_TO_TICKS(15000));   // дать BLE подключиться
  File f = LittleFS.open("/tracks/selftest.csv", FILE_WRITE);
  f.println("time_utc,lat,lon,alt_m,speed_kmh,hr_bpm,sats,hdop");
  for (int i = 0; i < 2000; i++)
    f.printf("%lu,%.6f,%.6f,%.1f,%.2f,%d,9,1.1\n", 1763800000UL + i, 55.0 + i * 1e-5, 37.0, 150.0, 12.0, 140);
  f.close();
  bool ok = Web::start();
  Serial.printf("TEST wifi start: %d ip=%s heap=%u\n", ok, Web::ip(), ESP.getFreeHeap());
  vTaskDelay(pdMS_TO_TICKS(2000));
  req("GET", "/api/info");
  req("GET", "/api/tracks");
  for (int i = 0; i < 3; i++) {
    stream("/csv?f=selftest.csv", "\n17638");   // ожидается 2000 строк
    req("GET", "/api/info");
    stream("/gpx?f=selftest.csv", "<trkpt");    // ожидается 2000 точек и </gpx>
    req("GET", "/csv?f=../hr.csv");
  }
  req("GET", "/csv?f=missing.csv");
  req("POST", "/api/delete", "f=selftest.csv");
  req("POST", "/api/delete", "f=selftest.csv");
  req("GET", "/api/tracks");
  Web::stop();
  Serial.println("TEST DONE");
  vTaskDelete(nullptr);
}

static void start() { xTaskCreatePinnedToCore(task, "selftest", 8192, nullptr, 1, nullptr, 0); }
}

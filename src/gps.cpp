#include "gps.h"
#include "config.h"
#include <TinyGPSPlus.h>

static HardwareSerial& ser = Serial2;
static TinyGPSPlus tg;
static GpsData d = {};
static uint32_t lastCharMs = 0;
static bool fixFlag = false;
static GsvParser gsv;
static uint32_t gsvMs = 0;
static bool needConfig = false;
static uint32_t passedAtLoss = 0;

// ---------- UBX (бинарный протокол u-blox) ----------
static void ubxSend(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
  uint8_t hdr[6] = {0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; i++) { ckA += hdr[i]; ckB += ckA; }
  for (uint16_t i = 0; i < len; i++) { ckA += payload[i]; ckB += ckA; }
  ser.write(hdr, 6);
  if (len) ser.write(payload, len);
  ser.write(ckA);
  ser.write(ckB);
}

static bool ubxWaitAck(uint8_t cls, uint8_t id, uint32_t timeoutMs) {
  const uint8_t ack[] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, cls, id};
  uint8_t pos = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (ser.available()) {
      uint8_t b = ser.read();
      if (b == ack[pos]) {
        if (++pos == sizeof(ack)) return true;
      } else {
        pos = (b == ack[0]) ? 1 : 0;
      }
    }
    delay(1);
  }
  return false;
}

static bool ubxSetMsgRate(uint8_t nmeaId, uint8_t rate) {
  uint8_t p[3] = {0xF0, nmeaId, rate};   // класс 0xF0 = стандартные NMEA
  ubxSend(0x06, 0x01, p, sizeof(p));
  return ubxWaitAck(0x06, 0x01, 300);
}

// ---------- время ----------
static int32_t daysFromCivil(int y, unsigned m, unsigned dd) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + dd - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// Настройки живут в памяти модуля до отключения питания,
// поэтому вызывается при старте и после каждого его пропадания.
static void configure() {
  // Нужны GGA (высота, спутники, HDOP), RMC (дата, скорость, курс) и GSV (сигнал).
  // Лишнее отключаем: меньше трафика — можно поднять частоту без смены скорости порта.
  bool ok = true;
  ok &= ubxSetMsgRate(0x01, 0);  // GLL
  ok &= ubxSetMsgRate(0x02, 0);  // GSA
  ok &= ubxSetMsgRate(0x05, 0);  // VTG
  // GSV длинный: при 1 Гц — каждую секунду, при большей частоте — раз в 5 с
  ok &= ubxSetMsgRate(0x03, GPS_RATE_HZ <= 1 ? 1 : GPS_RATE_HZ * 5);

  // CFG-RATE: период измерений
  uint16_t ms = 1000 / GPS_RATE_HZ;
  uint8_t rate[6] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8), 1, 0, 1, 0};
  ubxSend(0x06, 0x08, rate, sizeof(rate));
  ok &= ubxWaitAck(0x06, 0x08, 500);

  // CFG-NAV5: динамическая модель (меняем только её — mask = 0x0001)
  uint8_t nav5[36] = {0};
  nav5[0] = 0x01;
  nav5[2] = GPS_DYN_MODEL;
  ubxSend(0x06, 0x24, nav5, sizeof(nav5));
  ok &= ubxWaitAck(0x06, 0x24, 500);

  d.ubxAck = ok ? 1 : 0;
}

void Gps::begin() {
  ser.setRxBufferSize(2048);
  ser.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  // Если модуль был усыплён (Gps::sleep), его будит любая активность на RX.
  // Первые байты при этом теряются — шлём «пустышку» и ждём.
  for (int i = 0; i < 8; i++) ser.write(0xFF);
  delay(500);
  configure();
  needConfig = !d.ubxAck;      // модуль молчал — настроим, когда появятся данные
}

void Gps::update() {
  bool gsvNew = false;
  while (ser.available()) {
    char c = ser.read();
    tg.encode(c);
    gsvNew |= gsv.encode(c, millis());
    lastCharMs = millis();
  }
  d.charsProcessed = tg.charsProcessed();
  d.failedChecksum = tg.failedChecksum();
  d.dataFlowing = d.charsProcessed > 0 && (millis() - lastCharMs) < 2000;

  // Питание модуля пропадало (или его подключили позже) — настраиваем заново,
  // как только пришла хотя бы одна целая строка.
  if (millis() - lastCharMs > 3000) { needConfig = true; passedAtLoss = tg.passedChecksum(); }
  if (needConfig && d.dataFlowing && tg.passedChecksum() > passedAtLoss) {
    needConfig = false;
    configure();
  }

  if (gsvNew) gsvMs = millis();
  d.gsvValid = gsvMs && millis() - gsvMs < 12000;
  if (gsvNew || !d.gsvValid) {
    SatSignal s[GpsData::MAX_SATS];
    d.satCount = d.gsvValid ? gsv.list(s, GpsData::MAX_SATS, millis()) : 0;
    d.satHeard = 0;
    uint16_t sum = 0;
    for (uint8_t i = 0; i < d.satCount; i++) {
      d.sat[i] = s[i];
      if (!s[i].snr) continue;
      if (d.satHeard < 4) sum += s[i].snr;
      d.satHeard++;
    }
    d.snrMax = d.satCount ? d.sat[0].snr : 0;
    d.snrTop4 = d.satHeard ? sum / (d.satHeard < 4 ? d.satHeard : 4) : 0;
  }

  // u-blox шлёт RMC перед GGA, поэтому приход GGA с фиксом (обновление высоты)
  // означает, что вся эпоха уже разобрана.
  if (tg.altitude.isUpdated()) {
    d.altM = tg.altitude.meters();
    d.lat = tg.location.lat();
    d.lon = tg.location.lng();
    d.speedKmh = tg.speed.isValid() ? tg.speed.kmph() : 0;
    d.courseDeg = tg.course.isValid() ? tg.course.deg() : 0;
    d.sats = tg.satellites.value();
    d.hdop = tg.hdop.isValid() ? tg.hdop.hdop() : 99.9f;
    if (d.speedKmh < 0.5f) d.speedKmh = 0;   // шум на стоянке
    fixFlag = true;
  }
  if (tg.satellites.isUpdated()) d.sats = tg.satellites.value();

  d.fix = tg.location.isValid() && tg.location.age() < 3000;

  if (tg.date.isValid() && tg.time.isValid() && tg.date.year() >= 2020) {
    int32_t days = daysFromCivil(tg.date.year(), tg.date.month(), tg.date.day());
    d.epoch = (uint32_t)days * 86400UL + tg.time.hour() * 3600UL + tg.time.minute() * 60UL + tg.time.second();
    d.timeValid = true;
  }
}

const GpsData& Gps::data() { return d; }

void Gps::sleep() {
  // UBX-RXM-PMREQ: duration = 0 (бесконечно), flags = 0x02 (backup)
  uint8_t p[8] = {0, 0, 0, 0, 0x02, 0, 0, 0};
  ubxSend(0x02, 0x41, p, sizeof(p));
  ser.flush();
  delay(100);
}

bool Gps::newFix() {
  bool f = fixFlag;
  fixFlag = false;
  return f;
}

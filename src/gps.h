#pragma once
#include <Arduino.h>
#include "gsv.h"

struct GpsData {
  bool     dataFlowing;     // от модуля идут байты (проверка проводки)
  bool     fix;             // есть свежее решение координат
  double   lat, lon;
  float    altM;
  float    speedKmh;
  float    courseDeg;
  float    hdop;
  uint8_t  sats;
  bool     timeValid;
  uint32_t epoch;           // UTC, unix time
  uint32_t charsProcessed;
  uint32_t failedChecksum;
  int8_t   ubxAck;          // 1 — модуль принял настройки UBX, 0 — нет (клон?)

  // сигнал спутников (NMEA GSV)
  static const uint8_t MAX_SATS = 24;
  bool      gsvValid;       // данные о сигнале свежие
  uint8_t   satCount;       // модуль знает о спутнике (видим или ожидается)
  uint8_t   satHeard;       // из них сигнал реально принимается
  uint8_t   snrMax;         // самый сильный, дБГц
  uint8_t   snrTop4;        // средний по четырём лучшим, дБГц
  SatSignal sat[MAX_SATS];  // сильные первыми
};

namespace Gps {
  void begin();
  void update();              // вызывать как можно чаще из loop()
  const GpsData& data();
  bool newFix();              // true один раз на каждую новую эпоху с фиксом
  void sleep();               // перевести модуль в режим backup (~единицы мА вместо ~45)
}

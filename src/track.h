#pragma once
#include <Arduino.h>
#include "gps.h"

struct SessionStats {
  bool     active;
  uint32_t elapsedS;
  double   distM;
  float    ascentM;
  float    descentM;
  float    maxSpeedKmh;
  uint8_t  hrMax;
  uint8_t  hrAvg;
  uint32_t points;
};

namespace Track {
  bool begin();                                  // монтирует LittleFS
  bool start(const GpsData& g);                  // новый файл трека
  void stop();
  bool active();
  void onFix(const GpsData& g, uint8_t hrBpm);   // hrBpm = 0, если пульса нет
  SessionStats stats();
  const char* fileName();                        // имя без пути
  uint32_t freeBytes();
}

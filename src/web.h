#pragma once
#include <Arduino.h>

// Точка доступа Wi-Fi + веб-страница со списком треков (скачать GPX/CSV, удалить)
namespace Web {
  bool start();
  void stop();
  void loop();
  bool active();
  const char* ip();
  void setBattery(float v);   // для /api/info
}

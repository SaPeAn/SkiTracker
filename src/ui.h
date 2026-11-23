#pragma once
#include <Arduino.h>
#include "gps.h"
#include "hr.h"
#include "track.h"

struct UiModel {
  GpsData      gps;
  HrData       hr;
  bool         hrFresh;
  SessionStats st;
  const char*  file;
  uint32_t     freeKB;
  float        batV;
  bool         wifi;
  const char*  ip;
};

namespace Ui {
  void begin();
  void draw(const UiModel& m);
  void nextScreen();
  void setBacklight(bool on);
  bool backlight();
  void toast(const char* text, uint32_t ms = 2000);
  void showMessage(const char* title, const char* text);   // сразу, на весь экран
  void sleep();                                            // погасить и усыпить дисплей
}

#pragma once
#include <Arduino.h>

enum class HrState : uint8_t { Off, Scanning, Connecting, Connected };

struct HrData {
  HrState  state;
  uint8_t  bpm;
  bool     contact;       // датчик касается кожи (если ремень это сообщает)
  uint32_t lastMs;        // millis() последнего пакета
  int8_t   battery;       // % заряда ремня, -1 — неизвестно
  bool     saved;         // ремень запомнен в памяти
  char     name[24];
  char     addr[18];
};

namespace Hr {
  void begin();             // запускает отдельную задачу BLE
  HrData get();
  bool fresh();             // есть свежий пульс
  void forgetDevice();      // забыть запомненный ремень
  const char* stateName(HrState s);
}

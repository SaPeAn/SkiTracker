#pragma once
#include <Arduino.h>
#include "config.h"

// Кнопка с антидребезгом.
// veryLongMs == 0: Long срабатывает сразу при удержании BTN_LONG_MS.
// veryLongMs  > 0: Long — при отпускании после BTN_LONG_MS, VeryLong — при удержании veryLongMs.
class Button {
 public:
  enum Event { None, Short, Long, VeryLong };

  Button(uint8_t pin, bool internalPullup, uint32_t veryLongMs = 0)
      : pin_(pin), pullup_(internalPullup), veryLongMs_(veryLongMs) {}

  void begin() { pinMode(pin_, pullup_ ? INPUT_PULLUP : INPUT); }   // GPIO35 — только внешняя подтяжка

  Event update() {
    bool raw = digitalRead(pin_) == LOW;
    uint32_t now = millis();
    if (raw != lastRaw_) { lastRaw_ = raw; changeMs_ = now; }
    if (now - changeMs_ < 30) return None;

    if (raw && !down_) {
      down_ = true; downMs_ = now; fired_ = false;
      return None;
    }
    if (!raw && down_) {                       // отпустили
      down_ = false;
      bool sup = suppress_;
      suppress_ = false;
      if (fired_ || sup) return None;
      uint32_t held = now - downMs_;
      if (veryLongMs_ && held >= BTN_LONG_MS) return Long;
      return Short;
    }
    if (raw && down_ && !fired_) {             // удерживаем
      uint32_t held = now - downMs_;
      if (!veryLongMs_ && held >= BTN_LONG_MS) {
        fired_ = true;
        if (!suppress_) return Long;
      } else if (veryLongMs_ && held >= veryLongMs_) {
        fired_ = true;
        if (!suppress_) return VeryLong;
      }
    }
    return None;
  }

  bool pressed() const { return down_; }
  bool suppressed() const { return suppress_; }
  uint32_t heldMs() const { return down_ ? millis() - downMs_ : 0; }
  void suppress() { suppress_ = true; }   // нажатие — часть комбинации, событий не давать

 private:
  uint8_t pin_;
  bool pullup_;
  uint32_t veryLongMs_;
  bool lastRaw_ = false, down_ = false, fired_ = false, suppress_ = false;
  uint32_t changeMs_ = 0, downMs_ = 0;
};

#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Разбор NMEA GSV: какие спутники видны и с каким сигналом (C/N0, дБГц).
struct SatSignal {
  char    sys;    // 'P' GPS/SBAS, 'L' ГЛОНАСС, 'A' Galileo, 'B' BeiDou
  uint8_t prn;
  uint8_t snr;    // 0 — спутник ожидается, но сигнала нет
};

class GsvParser {
 public:
  static const uint8_t MAX_PER_SYS = 16;

  // Возвращает true, когда список одной из систем обновлён
  bool encode(char c, uint32_t nowMs) {
    if (c == '$') { len_ = 0; overflow_ = false; }
    if (c == '\r' || c == '\n') {
      buf_[len_] = 0;
      bool r = len_ > 0 && !overflow_ && line(nowMs);
      len_ = 0;
      return r;
    }
    if (len_ >= sizeof(buf_) - 1) { overflow_ = true; return false; }
    buf_[len_++] = c;
    return false;
  }

  // Все спутники, сильные первыми; устаревшие системы пропускаются
  uint8_t list(SatSignal* out, uint8_t max, uint32_t nowMs) const {
    uint8_t n = 0;
    for (const Sys& s : sys_) {
      if (!s.valid || nowMs - s.ms > STALE_MS) continue;
      for (uint8_t i = 0; i < s.n && n < max; i++) {
        uint8_t j = n++;
        while (j > 0 && out[j - 1].snr < s.sat[i].snr) { out[j] = out[j - 1]; j--; }
        out[j] = s.sat[i];
      }
    }
    return n;
  }

 private:
  static const uint32_t STALE_MS = 12000;
  struct Sys {
    bool      valid = false;
    uint32_t  ms = 0;
    uint8_t   n = 0, tmpN = 0, expect = 1;
    SatSignal sat[MAX_PER_SYS], tmp[MAX_PER_SYS];
  };
  Sys     sys_[4];
  char    buf_[96];
  uint8_t len_ = 0;
  bool    overflow_ = false;

  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }

  bool line(uint32_t nowMs) {
    if (len_ < 10 || buf_[0] != '$' || buf_[1] != 'G' || memcmp(buf_ + 3, "GSV,", 4)) return false;
    char* star = strchr(buf_, '*');
    if (!star || star[1] == 0 || hex(star[1]) < 0 || hex(star[2]) < 0) return false;
    uint8_t ck = 0;
    for (char* p = buf_ + 1; p < star; p++) ck ^= (uint8_t)*p;
    if (ck != hex(star[1]) * 16 + hex(star[2])) return false;
    *star = 0;

    const char* order = "PLAB";
    const char* pos = strchr(order, buf_[2]);
    if (!pos || !buf_[2]) return false;
    Sys& s = sys_[pos - order];

    char* f[24];
    uint8_t nf = 0;
    for (char* p = buf_; p && nf < 24;) {
      f[nf++] = p;
      p = strchr(p, ',');
      if (p) *p++ = 0;
    }
    if (nf < 4) return false;
    int total = atoi(f[1]), num = atoi(f[2]);
    if (num == 1) { s.tmpN = 0; s.expect = 1; }
    if (num != s.expect) { s.expect = 1; return false; }   // потеряли часть — ждём следующий цикл
    for (uint8_t i = 4; i + 3 < nf && s.tmpN < MAX_PER_SYS; i += 4) {
      if (!f[i][0]) continue;
      s.tmp[s.tmpN++] = {buf_[2], (uint8_t)atoi(f[i]), (uint8_t)atoi(f[i + 3])};
    }
    if (num < total) { s.expect = num + 1; return false; }
    memcpy(s.sat, s.tmp, sizeof(SatSignal) * s.tmpN);
    s.n = s.tmpN;
    s.ms = nowMs;
    s.valid = true;
    s.expect = 1;
    return true;
  }
};

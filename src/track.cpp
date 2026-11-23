#include "track.h"
#include "config.h"
#include <LittleFS.h>
#include <TinyGPSPlus.h>
#include <time.h>

static bool fsOk = false;
static File f;
static SessionStats st = {};
static uint32_t startMs = 0;
static char path[40] = "";
static uint32_t lastLogMs = 0;
static bool loggedAny = false;
static uint16_t linesSinceFlush = 0;
static uint16_t linesSinceSpaceCheck = 0;
static uint32_t hrSum = 0, hrCount = 0;

static bool anchorValid = false;
static double anchorLat = 0, anchorLon = 0;
static bool altRefValid = false;
static float altRef = 0;

static const char* CSV_HEADER = "time_utc,lat,lon,alt_m,speed_kmh,hr_bpm,sats,hdop";

bool Track::begin() {
  fsOk = LittleFS.begin(true);     // при первом запуске раздел будет отформатирован
  if (fsOk && !LittleFS.exists("/tracks")) LittleFS.mkdir("/tracks");
  return fsOk;
}

uint32_t Track::freeBytes() {
  if (!fsOk) return 0;
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return used < total ? (uint32_t)(total - used) : 0;
}

bool Track::start(const GpsData& g) {
  if (!fsOk || st.active) return false;
  if (freeBytes() < MIN_FREE_BYTES) return false;

  if (g.timeValid) {
    time_t t = g.epoch;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(path, sizeof(path), "/tracks/%Y%m%d_%H%M%S.csv", &tmv);
  } else {
    int n = 0;
    do snprintf(path, sizeof(path), "/tracks/trk_%03d.csv", n++);
    while (LittleFS.exists(path) && n < 1000);
  }

  f = LittleFS.open(path, FILE_WRITE);
  if (!f) return false;
  f.println(CSV_HEADER);

  st = {};
  st.active = true;
  startMs = millis();
  loggedAny = false;
  linesSinceFlush = linesSinceSpaceCheck = 0;
  hrSum = hrCount = 0;
  anchorValid = altRefValid = false;
  return true;
}

void Track::stop() {
  if (!st.active) return;
  st.elapsedS = (millis() - startMs) / 1000;
  st.active = false;
  f.flush();
  f.close();
}

bool Track::active() { return st.active; }

void Track::onFix(const GpsData& g, uint8_t hr) {
  if (!st.active) return;
  bool good = g.hdop <= MAX_HDOP_FOR_STATS;

  // --- статистика
  if (good && g.speedKmh > st.maxSpeedKmh) st.maxSpeedKmh = g.speedKmh;
  if (hr) {
    hrSum += hr; hrCount++;
    if (hr > st.hrMax) st.hrMax = hr;
    st.hrAvg = hrSum / hrCount;
  }
  if (good) {
    if (!anchorValid) {
      anchorLat = g.lat; anchorLon = g.lon; anchorValid = true;
    } else if (g.speedKmh >= DIST_MIN_SPEED_KMH) {
      double dd = TinyGPSPlus::distanceBetween(anchorLat, anchorLon, g.lat, g.lon);
      if (dd >= DIST_MIN_STEP_M) {
        st.distM += dd;
        anchorLat = g.lat; anchorLon = g.lon;
      }
    }
    if (!altRefValid) {
      altRef = g.altM; altRefValid = true;
    } else if (g.altM - altRef >= ASCENT_HYST_M) {
      st.ascentM += g.altM - altRef; altRef = g.altM;
    } else if (altRef - g.altM >= ASCENT_HYST_M) {
      st.descentM += altRef - g.altM; altRef = g.altM;
    }
  }

  // --- запись в файл
  uint32_t now = millis();
  if (loggedAny && now - lastLogMs + 50 < LOG_INTERVAL_MS) return;
  lastLogMs = now;
  loggedAny = true;

  f.printf("%lu,%.6f,%.6f,%.1f,%.2f,%u,%u,%.1f\n",
           (unsigned long)g.epoch, g.lat, g.lon, g.altM, g.speedKmh,
           (unsigned)hr, (unsigned)g.sats, g.hdop);
  st.points++;

  if (++linesSinceFlush >= 10) {           // сброс на флеш раз в ~10 с —
    f.flush();                             // при обрыве питания теряем максимум 10 точек
    linesSinceFlush = 0;
  }
  if (++linesSinceSpaceCheck >= 60) {
    linesSinceSpaceCheck = 0;
    if (freeBytes() < MIN_FREE_BYTES / 2) stop();
  }
}

SessionStats Track::stats() {
  SessionStats s = st;
  if (s.active) s.elapsedS = (millis() - startMs) / 1000;
  return s;
}

const char* Track::fileName() {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

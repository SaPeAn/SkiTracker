#include "web.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <time.h>

static WebServer server(80);
static bool running = false;
static bool routesSet = false;
static char ipStr[16] = "-";
static float batteryV = 0;

// Имена файлов создаём сами, но параметр приходит из браузера — проверяем строго.
static bool validName(const String& n) {
  if (n.length() == 0 || n.length() > 32 || n.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
  }
  return n.endsWith(".csv");
}

static String baseName(const String& p) {
  int s = p.lastIndexOf('/');
  return s >= 0 ? p.substring(s + 1) : p;
}

static void handleRoot() {
  String h;
  h.reserve(4096);
  h += F("<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>SkiTracker</title><style>body{font-family:sans-serif;margin:16px}"
         "td{padding:6px 8px;border-bottom:1px solid #ddd}a,button{margin-right:10px}</style>"
         "</head><body><h2>SkiTracker — треки</h2><table>");
  int count = 0;
  File dir = LittleFS.open("/tracks");
  if (dir && dir.isDirectory()) {
    File e = dir.openNextFile();
    while (e) {
      if (!e.isDirectory()) {
        String n = baseName(e.name());
        h += "<tr><td>" + n + "</td><td>" + String(e.size() / 1024.0, 1) + " KB</td><td>"
             "<a href='/gpx?f=" + n + "'>GPX</a><a href='/csv?f=" + n + "'>CSV</a>"
             "<form method='post' action='/del' style='display:inline' "
             "onsubmit=\"return confirm('Удалить " + n + "?')\">"
             "<input type='hidden' name='f' value='" + n + "'><button>удалить</button></form>"
             "</td></tr>";
        count++;
      }
      e.close();
      e = dir.openNextFile();
    }
  }
  if (!count) h += F("<tr><td>Треков пока нет</td></tr>");
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  h += "</table><p>Занято " + String(used / 1024) + " из " + String(total / 1024) + " KB</p></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

static void handleCsv() {
  String n = server.arg("f");
  if (!validName(n)) { server.send(400, "text/plain", "bad name"); return; }
  File f = LittleFS.open("/tracks/" + n, FILE_READ);
  if (!f) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + n + "\"");
  server.streamFile(f, "text/csv");
  f.close();
}

// CSV -> GPX на лету. Пульс — в расширении Garmin TrackPointExtension
// (его понимают Strava, Garmin Connect, GoldenCheetah и др.).
static void handleGpx() {
  String n = server.arg("f");
  if (!validName(n)) { server.send(400, "text/plain", "bad name"); return; }
  File f = LittleFS.open("/tracks/" + n, FILE_READ);
  if (!f) { server.send(404, "text/plain", "not found"); return; }

  String gpxName = n.substring(0, n.length() - 4) + ".gpx";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + gpxName + "\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/gpx+xml", "");

  String out;
  out.reserve(2048);
  out += F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<gpx version=\"1.1\" creator=\"SkiTracker\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
           "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n"
           "<trk><name>");
  out += gpxName;
  out += F("</name><type>nordic_ski</type><trkseg>\n");

  char pt[320];
  while (f.available()) {
    String ln = f.readStringUntil('\n');
    unsigned long t;
    double lat, lon;
    float alt, spd, hdop;
    unsigned hr, sats;
    if (sscanf(ln.c_str(), "%lu,%lf,%lf,%f,%f,%u,%u,%f", &t, &lat, &lon, &alt, &spd, &hr, &sats, &hdop) != 8)
      continue;   // заголовок или битая строка
    time_t tt = (time_t)t;
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    char ts[24];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    if (hr)
      snprintf(pt, sizeof(pt),
               "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%s</time>"
               "<extensions><gpxtpx:TrackPointExtension><gpxtpx:hr>%u</gpxtpx:hr>"
               "</gpxtpx:TrackPointExtension></extensions></trkpt>\n",
               lat, lon, alt, ts, hr);
    else
      snprintf(pt, sizeof(pt), "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%s</time></trkpt>\n",
               lat, lon, alt, ts);
    out += pt;
    if (out.length() > 1500) {
      server.sendContent(out);
      out = "";
    }
  }
  out += F("</trkseg></trk></gpx>\n");
  server.sendContent(out);
  server.sendContent("");   // конец chunked-ответа
  f.close();
}

static void handleDelete() {
  String n = server.arg("f");
  if (validName(n)) LittleFS.remove("/tracks/" + n);
  server.sendHeader("Location", "/");
  server.send(303);
}

// ---------- JSON API для приложения ----------
static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void handleApiInfo() {
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  sendJson(200, "{\"device\":\"SkiTracker\",\"api\":1,\"total\":" + String(total) +
                ",\"free\":" + String(total > used ? total - used : 0) +
                ",\"bat\":" + String(batteryV, 2) + "}");
}

static void handleApiTracks() {
  String j = "[";
  File dir = LittleFS.open("/tracks");
  if (dir && dir.isDirectory()) {
    File e = dir.openNextFile();
    while (e) {
      String n = baseName(e.name());
      if (!e.isDirectory() && validName(n)) {
        if (j.length() > 1) j += ",";
        j += "{\"name\":\"" + n + "\",\"size\":" + String(e.size()) + "}";
      }
      e.close();
      e = dir.openNextFile();
    }
  }
  j += "]";
  sendJson(200, j);
}

static void handleApiDelete() {
  String n = server.arg("f");
  if (!validName(n)) { sendJson(400, "{\"ok\":false}"); return; }
  bool ok = LittleFS.remove("/tracks/" + n);
  sendJson(ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

bool Web::start() {
  if (running) return true;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS)) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  strlcpy(ipStr, WiFi.softAPIP().toString().c_str(), sizeof(ipStr));
  if (!routesSet) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/csv", HTTP_GET, handleCsv);
    server.on("/gpx", HTTP_GET, handleGpx);
    server.on("/del", HTTP_POST, handleDelete);
    server.on("/api/info", HTTP_GET, handleApiInfo);
    server.on("/api/tracks", HTTP_GET, handleApiTracks);
    server.on("/api/delete", HTTP_POST, handleApiDelete);
    routesSet = true;
  }
  server.begin();
  running = true;
  return true;
}

void Web::stop() {
  if (!running) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}

void Web::loop() {
  if (running) server.handleClient();
}

bool Web::active() { return running; }
const char* Web::ip() { return ipStr; }
void Web::setBattery(float v) { batteryV = v; }

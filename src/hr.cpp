#include "hr.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string>

// Стандартный BLE-профиль пульса — его поддерживает Coospo H6M и большинство ремней
static const NimBLEUUID HR_SVC((uint16_t)0x180D);
static const NimBLEUUID HR_CHR((uint16_t)0x2A37);
static const NimBLEUUID BAT_SVC((uint16_t)0x180F);
static const NimBLEUUID BAT_CHR((uint16_t)0x2A19);

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static HrData hd = {};

static Preferences prefs;
static std::string savedAddr;          // запомненный ремень ("" — любой рядом)
static volatile bool candidateFound = false;
static NimBLEAddress candidate;
static char candName[24] = "";
static volatile bool forgetReq = false;

static void setState(HrState s) {
  portENTER_CRITICAL(&mux);
  hd.state = s;
  portEXIT_CRITICAL(&mux);
}

// ---------- сканирование ----------
class AdvCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (candidateFound) return;
    if (!dev->isAdvertisingService(HR_SVC)) return;
    if (!savedAddr.empty()) {
      if (dev->getAddress().toString() != savedAddr) return;   // только свой ремень
    } else if (dev->getRSSI() < HR_MIN_RSSI) {
      return;                                                  // далеко — вероятно, чужой
    }
    candidate = dev->getAddress();
    strlcpy(candName, dev->haveName() ? dev->getName().c_str() : "HR strap", sizeof(candName));
    candidateFound = true;
    NimBLEDevice::getScan()->stop();
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*) override { setState(HrState::Scanning); }
};

static AdvCallbacks advCb;
static ClientCallbacks clientCb;

// ---------- данные пульса ----------
static void onHrNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (len < 2) return;
  uint8_t flags = data[0];
  uint16_t bpm = data[1];
  if ((flags & 0x01) && len >= 3) bpm = data[1] | (data[2] << 8);   // 16-битный формат
  bool contactSupported = flags & 0x04;
  bool contact = contactSupported ? (flags & 0x02) : true;
  portENTER_CRITICAL(&mux);
  hd.bpm = bpm > 255 ? 255 : (uint8_t)bpm;
  hd.contact = contact;
  hd.lastMs = millis();
  portEXIT_CRITICAL(&mux);
}

static bool subscribe(NimBLEClient* c) {
  NimBLERemoteService* s = c->getService(HR_SVC);
  if (!s) return false;
  NimBLERemoteCharacteristic* ch = s->getCharacteristic(HR_CHR);
  if (!ch || !ch->canNotify()) return false;
  if (!ch->subscribe(true, onHrNotify)) return false;

  int8_t bat = -1;
  NimBLERemoteService* bs = c->getService(BAT_SVC);
  if (bs) {
    NimBLERemoteCharacteristic* bc = bs->getCharacteristic(BAT_CHR);
    if (bc && bc->canRead()) bat = (int8_t)bc->readValue<uint8_t>();
  }
  portENTER_CRITICAL(&mux);
  hd.battery = bat;
  portEXIT_CRITICAL(&mux);
  return true;
}

// Вся работа с BLE — в отдельной задаче, чтобы подключение (несколько секунд)
// не тормозило разбор GPS в loop().
static void hrTask(void*) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  NimBLEClient* client = nullptr;

  for (;;) {
    if (forgetReq) {
      forgetReq = false;
      scan->stop();
      savedAddr.clear();
      prefs.remove("addr");
      portENTER_CRITICAL(&mux);
      hd.saved = false;
      hd.name[0] = 0;
      hd.addr[0] = 0;
      portEXIT_CRITICAL(&mux);
      if (client && client->isConnected()) client->disconnect();
      candidateFound = false;
    }

    bool connected = client && client->isConnected();
    if (!connected) {
      if (candidateFound) {
        setState(HrState::Connecting);
        if (!client) {
          client = NimBLEDevice::createClient();
          client->setClientCallbacks(&clientCb, false);
          client->setConnectTimeout(5);
        }
        bool ok = client->connect(candidate) && subscribe(client);
        if (ok) {
          std::string a = candidate.toString();
          if (savedAddr.empty()) {
            savedAddr = a;
            prefs.putString("addr", a.c_str());
          }
          portENTER_CRITICAL(&mux);
          hd.saved = true;
          strlcpy(hd.name, candName, sizeof(hd.name));
          strlcpy(hd.addr, a.c_str(), sizeof(hd.addr));
          hd.state = HrState::Connected;
          portEXIT_CRITICAL(&mux);
        } else {
          if (client->isConnected()) client->disconnect();
          setState(HrState::Scanning);
          vTaskDelay(pdMS_TO_TICKS(2000));    // ремень занят часами/телефоном? пауза
        }
        candidateFound = false;
      } else if (!scan->isScanning()) {
        setState(HrState::Scanning);
        scan->start(0, nullptr, false);       // бесконечно, до находки ремня
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void Hr::begin() {
  hd.battery = -1;
  prefs.begin("hr", false);
  savedAddr = prefs.getString("addr", "").c_str();
  if (!savedAddr.empty()) {
    hd.saved = true;
    strlcpy(hd.addr, savedAddr.c_str(), sizeof(hd.addr));
  }

  NimBLEDevice::init("SkiTracker");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&advCb, false);
  scan->setActiveScan(true);
  scan->setInterval(160);   // мс; окно 40 мс => 25% времени радио слушает
  scan->setWindow(40);
  scan->setMaxResults(0);   // не копить результаты в памяти

  xTaskCreatePinnedToCore(hrTask, "hr", 6144, nullptr, 1, nullptr, 0);
}

HrData Hr::get() {
  portENTER_CRITICAL(&mux);
  HrData c = hd;
  portEXIT_CRITICAL(&mux);
  return c;
}

bool Hr::fresh() {
  HrData c = get();
  return c.state == HrState::Connected && c.bpm > 0 && (millis() - c.lastMs) < HR_STALE_MS;
}

void Hr::forgetDevice() { forgetReq = true; }

const char* Hr::stateName(HrState s) {
  switch (s) {
    case HrState::Scanning:   return "scanning";
    case HrState::Connecting: return "connecting";
    case HrState::Connected:  return "connected";
    default:                  return "off";
  }
}

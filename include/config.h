#pragma once
// ============ Настройки SkiTracker ============

// ---- Пины (T-Display). Пины экрана 4,5,16,18,19,23 не использовать!
#define PIN_GPS_RX     25   // RX ESP32  <-  TX модуля GPS
#define PIN_GPS_TX     26   // TX ESP32  ->  RX модуля GPS
#define PIN_BTN_LEFT   0    // левая кнопка (BOOT)
#define PIN_BTN_RIGHT  35   // правая кнопка
#define PIN_BAT_ADC    34   // делитель напряжения батареи 1:2
#define PIN_ADC_EN     14   // включение делителя на некоторых ревизиях платы
#define PIN_I2C_SDA    21   // на будущее: барометр
#define PIN_I2C_SCL    22

// ---- GPS
#define GPS_BAUD       9600
#define GPS_RATE_HZ    1    // 1..5 для NEO-6M (NEO-M8N до 10)
#define GPS_DYN_MODEL  0    // 0 portable, 3 pedestrian, 4 automotive

// ---- Запись трека
#define LOG_INTERVAL_MS    1000  // как часто писать точку в файл
#define AUTO_START_ON_FIX  1     // начать запись автоматически при первом фиксе
#define MIN_FREE_BYTES     (24 * 1024)

// ---- Фильтры статистики
#define DIST_MIN_STEP_M    3.0   // шаг накопления дистанции (подавление дрожания)
#define DIST_MIN_SPEED_KMH 1.0   // стоим на месте — дистанцию не считаем
#define MAX_HDOP_FOR_STATS 4.0
#define ASCENT_HYST_M      5.0   // гистерезис набора высоты (для GPS-высоты; с барометром ~2)

// ---- Пульсометр
#define HR_MIN_RSSI   -70   // при первом сопряжении — только ремень рядом (не чужой на старте)
#define HR_STALE_MS   5000  // через сколько пульс считается устаревшим

// ---- Батарея (одна банка LiPo)
// Калибровка АЦП этой платы: (мультиметр на батарее) / (показания без калибровки).
// 22.11.2026: 4.002 В по мультиметру, 4.29 В на экране
#define BAT_CAL          (4.002f / 4.29f)
#define BAT_FULL_V       4.15f  // 100% на экране
#define BAT_WARN_V       3.55f  // предупреждение (~10-15% осталось)
#define BAT_SHUTDOWN_V   3.40f  // сохранить трек и уснуть
#define BAT_USB_V        4.25f  // выше — питание от USB без батареи
#define BAT_CHECK_MS     5000   // период замера
#define BAT_LOW_COUNT    3      // столько замеров подряд ниже порога (просадки от радио не в счёт)
#define BAT_WARN_REPEAT_MS (5UL * 60 * 1000)

// ---- Экран / кнопки
#define TZ_OFFSET_MIN     180    // часовой пояс для часов на экране, минуты (+3 = 180)
#define SCREEN_TIMEOUT_S  60     // автоотключение подсветки, 0 = никогда
#define BTN_LONG_MS       1500
#define BTN_COMBO_MS      4000   // обе кнопки — забыть ремень
#define BTN_OFF_MS        4000   // левая — выключить
#define BTN_ON_MS         2000   // правая — включить (короткое нажатие не будит)

// ---- Wi-Fi выгрузка треков
#define WIFI_AP_SSID  "SkiTracker"
#define WIFI_AP_PASS  "skitrack123"   // минимум 8 символов

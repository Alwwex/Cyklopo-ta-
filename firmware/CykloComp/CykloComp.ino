// ============================================================================
// CykloComp v2 - chytry cyklocomputer
// Waveshare ESP32-C3-Zero + ST7789 240x320 + GPS (ATGM336H / NEO-6M) + NimBLE
//
// Arduino IDE:
//   - v tomto adresari smi byt jen tento jeden .ino soubor
//   - Deska: "ESP32C3 Dev Module"
//   - Tools -> USB CDC On Boot -> Enabled        (USB konzole / ladeni)
//   - Tools -> Partition Scheme -> "Default 4MB with spiffs"  (ulozene jizdy)
//   - Knihovny: Adafruit GFX, Adafruit ST7735 and ST7789, TinyGPSPlus,
//               NimBLE-Arduino 2.x
//
// Novinky ve v2 (detaily v README):
//   - "palubni deska" ve stylu DS/Game Boy: barevny oblouk rychlosti,
//     velke 7-segmentove cislice, dlazdice 2x2, 3 barevna temata
//   - mereni baterie, rizeni podsviceni, automaticke vypnuti (deep sleep)
//   - autopauza, stoupani, sklon, prumer
//   - jizdy se ukladaji do flash (LittleFS) a appka si je stahne (SYNC)
//   - volitelne BLE senzory: hrudni pas (tep) a senzor kadence
//   - simulace GPS jizdy (SIM:1) pro testovani bez signalu
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <TinyGPSPlus.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <math.h>

#define FW_VERSION "2.0.0"

// ============================================================================
// DESKA - vyber zapojeni
//   BOARD_BREADBOARD   = puvodni zapojeni na nepajivem poli / perfboardu
//   BOARD_CYKLOPCB_V1  = plosny spoj z hardware/pcb (mereni baterie,
//                        podsviceni, vypinani GPS, probouzeni tlacitkem)
// ============================================================================
#define BOARD_BREADBOARD  0
#define BOARD_CYKLOPCB_V1 1

#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_BREADBOARD
#endif

#if BOARD_PROFILE == BOARD_CYKLOPCB_V1
  #define BOARD_NAME        "CykloPCB v1"
  #define BTN1_PIN          5    // MODE / zapnout (umi probudit z deep sleep)
  #define BTN2_PIN          8    // START / STOP
  #define BTN3_PIN          9    // PAUZA / ZPET (zaroven BOOT tlacitko)
  #define BAT_ADC_PIN       0    // delic 100k/100k z napajeci vetve
  #define TFT_BL_PIN        7    // P-MOSFET, aktivni v LOW
  #define TFT_BL_ACTIVE_LOW 1
  #define GPS_EN_PIN        10   // P-MOSFET, aktivni v LOW
  #define GPS_EN_ACTIVE_LOW 1
  #define WAKE_BTN_PIN      5
#else
  #define BOARD_NAME        "Breadboard"
  #define BTN1_PIN          8
  #define BTN2_PIN          9
  #define BTN3_PIN          10
  // Mereni baterie: pridej delic 2x100k z TP4056 OUT+ na GPIO0 a dej sem 0.
  #define BAT_ADC_PIN       -1
  #define TFT_BL_PIN        -1   // displej bez BLK pinu - podsviceni stale zapnute
  #define TFT_BL_ACTIVE_LOW 0
  #define GPS_EN_PIN        -1
  #define GPS_EN_ACTIVE_LOW 0
  #define WAKE_BTN_PIN      -1   // GPIO8-10 neumi probudit C3 z deep sleep
#endif

#define BAT_DIVIDER 2.0f

// ============================================================================
// PINY (spolecne)
// ============================================================================
#define TFT_CS    3
#define TFT_DC    2
#define TFT_RST   1
#define TFT_MOSI  6
#define TFT_SCLK  4

#define GPS_RX_PIN 20   // do ESP (GPS TX -> sem)
#define GPS_TX_PIN 21   // z ESP (do GPS RX)
#define GPS_BAUD   9600

// USB (nativni USB-CDC) - stejny textovy protokol jako BLE, pro testovani z PC.
#define USB_SERIAL_BAUD 115200

// ============================================================================
// BLE UUID + PROTOKOL
// ============================================================================
#define SERVICE_UUID      "a5c40001-2f0b-4f6e-9d3a-8c1e2b7d9f10"
#define TELEMETRY_UUID    "a5c40002-2f0b-4f6e-9d3a-8c1e2b7d9f10"  // READ+NOTIFY 1x/s
#define COMMAND_UUID      "a5c40003-2f0b-4f6e-9d3a-8c1e2b7d9f10"  // WRITE(+NR)
#define STATUS_UUID       "a5c40004-2f0b-4f6e-9d3a-8c1e2b7d9f10"  // READ+NOTIFY 1x/5s
#define BULK_UUID         "a5c40005-2f0b-4f6e-9d3a-8c1e2b7d9f10"  // READ (odpovedi SYNC)

#define MAX_ROUTE_POINTS  300
#define MAX_STREET_POINTS 600
#define BREADCRUMB_SIZE   200
#define CMD_MAX_LEN       256
#define CMD_QUEUE_LEN     48
#define MAX_TRACK_POINTS  1500
#define MAX_STORED_RIDES  40
#define TRK_PAGE_POINTS   24

#define NUM_SCREENS 4   // 0=DASH 1=STATISTIKY 2=MAPA 3=SYSTEM (sedi se SCREEN:n)
#define NUM_FIELDS  14
#define NUM_THEMES  3

#define SCR_DASH  0
#define SCR_STATS 1
#define SCR_MAP   2
#define SCR_SYS   3

// ============================================================================
// BARVY A TEMATA (RGB565)
// ============================================================================
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((uint8_t)(b)>>3)))

// ============================================================================
// STRUCTY (MUSI BYT NAD VSEMI FUNKCEMI - Arduino IDE generuje prototypy!)
// ============================================================================
struct Theme {
  const char* name;
  uint16_t bg, panel, line, text, muted, accent, ok, warn, danger, ble;
  uint16_t segOff, gaugeTrack, gaugeLo, gaugeMid, gaugeHi;
  uint16_t mapBg, mapGrid, mapStreet, route, routeEdge, track, posFill, posArrow;
};

struct GeoPoint {
  float lat;
  float lng;
};

struct RoutePoint {
  float lat;
  float lng;
  float cumDistM; // kumulativni vzdalenost od startu trasy [m]
};

struct DataField {
  int16_t x, y;          // y = baseline; x = levy okraj / stred / pravy okraj podle align
  const GFXfont* font;
  uint16_t color;
  uint8_t align;         // 0 = vlevo, 1 = na stred, 2 = vpravo
  char text[28];
  int16_t lastX;         // kde se text naposledy kreslil (kvuli presnemu smazani)
  uint16_t lastColor;
  bool valid;
};

struct ButtonState {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  bool longFired;
  uint16_t longMs;
};

struct MapTransform {
  double centerLat, centerLng;
  double metersPerPixel;
  double lngScale; // cos(centerLat)
  int16_t originX, originY;
};

// jedna 7-segmentova cislice (prekresluji se jen zmenene segmenty)
struct SegDigit {
  int16_t x, y, w, h, t;
  uint8_t mask;
  uint16_t onCol, offCol;
  bool valid;
};

// cislo "88.8" ze tri cislic + tecky
struct SegNumber {
  SegDigit d[3];
  int16_t dotX, dotY, dotS;
  uint16_t dotCol;
  bool dotValid;
};

// jednotny zdroj polohy (GPS nebo simulace)
struct Fix {
  bool valid;      // mame cerstvou polohu
  bool updated;    // v tomto pruchodu prisla nova poloha
  double lat, lng;
  float kmh;
  float alt;
  bool altValid;
  float course;
  int sats;
  float hdop;
};

struct CmdMsg {
  char s[CMD_MAX_LEN];
};

// ============================================================================
// TEMATA
// ============================================================================
// GB = puvodni Game Boy (DMG) paleta
#define GB0 RGB565(155,188,15)
#define GB1 RGB565(139,172,15)
#define GB2 RGB565(48,98,48)
#define GB3 RGB565(15,56,15)

const Theme THEMES[NUM_THEMES] = {
  // 0: NOC - tmava "DS" palubni deska
  { "NOC",
    RGB565(4,8,16), RGB565(18,26,38), RGB565(40,54,74), RGB565(245,248,252), RGB565(130,146,168),
    RGB565(255,122,26), RGB565(60,210,100), RGB565(255,200,40), RGB565(240,60,60), RGB565(60,140,255),
    RGB565(20,28,40), RGB565(30,40,56), RGB565(40,220,90), RGB565(255,210,40), RGB565(240,50,50),
    RGB565(16,22,32), RGB565(26,34,48), RGB565(78,92,112), RGB565(50,150,255), RGB565(10,40,90),
    RGB565(255,122,26), RGB565(255,255,255), RGB565(30,120,255) },
  // 1: RETRO - zelene LCD jako Game Boy
  { "RETRO",
    GB0, RGB565(147,180,15), GB2, GB3, GB2,
    GB3, GB3, GB2, GB3, GB3,
    RGB565(146,179,20), GB1, GB3, GB3, GB3,
    GB0, RGB565(147,180,15), GB2, GB3, GB1,
    GB2, GB3, GB0 },
  // 2: DEN - svetle, maximalni kontrast na slunci
  { "DEN",
    RGB565(255,255,255), RGB565(238,241,245), RGB565(205,210,218), RGB565(10,12,16), RGB565(95,104,118),
    RGB565(230,95,0), RGB565(0,155,70), RGB565(215,150,0), RGB565(215,35,35), RGB565(0,95,230),
    RGB565(232,236,241), RGB565(222,226,232), RGB565(0,175,80), RGB565(240,170,0), RGB565(220,40,40),
    RGB565(246,243,234), RGB565(228,223,208), RGB565(150,146,138), RGB565(20,90,220), RGB565(255,255,255),
    RGB565(240,110,0), RGB565(255,255,255), RGB565(20,90,220) },
};

const Theme* T = &THEMES[0];

// segmenty a..g (bit 0 = a)
const uint8_t SEG_DIGITS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// ============================================================================
// GLOBALNI PROMENNE
// ============================================================================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
Preferences prefs;

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* telemetryChar = nullptr;
NimBLECharacteristic* commandChar = nullptr;
NimBLECharacteristic* statusChar = nullptr;
NimBLECharacteristic* bulkChar = nullptr;
volatile bool bleConnected = false;
QueueHandle_t cmdQueue = nullptr;

Fix fx = {false, false, 0, 0, 0, 0, false, 0, 0, 99};

// obrazovky
uint8_t currentScreen = SCR_DASH;
uint8_t lastDrawnScreen = 255;

// nastaveni (NVS)
uint8_t themeIdx = 0;
uint8_t brightnessPct = 80;
bool autoPauseEnabled = true;
bool sensorsEnabled = false;
uint8_t autoOffMin = 15;
float gaugeMaxKmh = 60.0f;
int timezoneOffsetHours = 2;
uint8_t fieldConfig[4] = {0, 2, 3, 8}; // TRIP, PRUMER, MAX, STOUPANI

// jizda
bool recording = false;
bool manualPaused = false;
bool autoPaused = false;
unsigned long stillMs = 0;

double tripDistanceM = 0;
double odometerM = 0;
float  maxSpeedKmh = 0;
unsigned long rideTimeMs = 0;
float ascentM = 0;

float speedEmaRaw = 0;
float filteredSpeedKmh = 0;
static const float SPEED_EMA_ALPHA = 0.35f;
static const float SPEED_STOP_THRESHOLD_KMH = 2.5f;

bool haveRefPoint = false;
double refLat = 0, refLng = 0;

// vyska / stoupani / sklon
bool haveAltFilt = false;
float altFilt = 0, altRef = 0;
float gradePct = 0;
bool gradeValid = false;
double gradeDistRef = 0;
float gradeAltRef = 0;

// cas z telefonu (kdyz GPS jeste nema cas)
uint32_t phoneEpochBase = 0;
unsigned long phoneEpochMs = 0;

// baterie
float batMv = 0;
int batPct = -1;
bool usbPower = false;
unsigned long lastBatMs = 0;
int lowBatSeconds = 0;
bool lowBatWarned = false;

// podsviceni + aktivita
unsigned long lastActivityMs = 0;
int appliedBacklight = -1;

// trasa / ulice / stopa pro mapu
RoutePoint routePoints[MAX_ROUTE_POINTS];
int routePointCount = 0;
bool routeValid = false;
bool receivingRoute = false;

GeoPoint streetPoints[MAX_STREET_POINTS];
int streetPointCount = 0;
bool receivingStreets = false;

GeoPoint breadcrumb[BREADCRUMB_SIZE];
int breadcrumbCount = 0;
int breadcrumbHead = 0;
bool haveBreadcrumbRef = false;
double lastBreadcrumbLat = 0, lastBreadcrumbLng = 0;

bool mapStaticDrawn = false;
bool wasTrackingMode = false;
unsigned long lastMapDrawMs = 0;
double lastMapDrawLat = 0, lastMapDrawLng = 0;
bool haveLastMapDraw = false;
int lastDrawnBreadcrumbCount = -1;
MapTransform mapT;
int16_t mapAreaX = 2, mapAreaY = 90, mapAreaW = 236, mapAreaH = 228;

// zaznam jizdy (LittleFS)
bool fsOk = false;
uint16_t curRideId = 0;
uint16_t rideSeq = 0;
uint32_t rideStartEpoch = 0;
int32_t trkLat[MAX_TRACK_POINTS];
int32_t trkLng[MAX_TRACK_POINTS];
int trkCount = 0;
float trkSpacingM = 20.0f;
bool trkHaveLast = false;
double trkLastLat = 0, trkLastLng = 0;
unsigned long lastRideSaveMs = 0;

// BLE senzory (tep, kadence) - zapisuje je task senzoru
volatile int hrBpm = 0;
volatile unsigned long hrLastMs = 0;
volatile int cadRpm = 0;
volatile unsigned long cadLastMs = 0;
volatile bool hrConnected = false;
volatile bool cscConnected = false;
uint16_t cscPrevRevs = 0, cscPrevTime = 0;
bool cscHavePrev = false;

// simulace
bool simEnabled = false;
double simLat = 50.0755, simLng = 14.4378;
float simHeading = 45, simAlt = 300, simT = 0;
int simIdx = 0;
unsigned long simLastMs = 0;

// casovace
unsigned long lastLoopMillis = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastStatusMs = 0;
unsigned long lastNvsSaveMs = 0;
unsigned long lastRenderMs = 0;

char serialCmdBuf[CMD_MAX_LEN];
int serialCmdLen = 0;

ButtonState btn1 = {BTN1_PIN, false, false, 0, 0, false, 2000};
ButtonState btn2 = {BTN2_PIN, false, false, 0, 0, false, 2000};
ButtonState btn3 = {BTN3_PIN, false, false, 0, 0, false, 1500};

// overlay (souhrn jizdy, hlasky)
bool overlayActive = false;
unsigned long overlayUntil = 0;

// ---------------- UI prvky ----------------
// status bar
DataField fldClock = {4, 17, &FreeSansBold9pt7b, 0, 0, "", 0, 0, false};
DataField fldSats  = {146, 17, &FreeSans9pt7b, 0, 0, "", 0, 0, false};
DataField fldBat   = {205, 17, &FreeSans9pt7b, 0, 2, "", 0, 0, false};
int8_t sbRec = -1, sbBle = -1, sbDots = -1, sbBatLevel = -2, sbBatChg = -1;

// DASH
#define G_CX 120
#define G_CY 122
#define G_ROUT 96
#define G_RIN 82
#define G_SEGS 45
int gaugeLit = -1;
int gaugeAvgSeg = -1, gaugeMaxSeg = -1;
SegNumber dashSpeed;
DataField fldDashMsg  = {120, 82, &FreeSansBold9pt7b, 0, 1, "", 0, 0, false};
DataField fldDashTime = {120, 192, &FreeSansBold12pt7b, 0, 1, "", 0, 0, false};
DataField fldTileVal[4];
DataField fldTileLbl[4];
DataField fldTileUnit[4];
const int16_t TILE_X[4] = {4, 122, 4, 122};
const int16_t TILE_Y[4] = {200, 200, 262, 262};
#define TILE_W 114
#define TILE_H 56

// STATISTIKY
#define STAT_ROWS 8
DataField fldStatVal[STAT_ROWS];
DataField fldStatOdo = {230, 308, &FreeSansBold18pt7b, 0, 2, "", 0, 0, false};

// MAPA
SegNumber mapSpeed;
DataField fldDestVal = {134, 72, &FreeSansBold12pt7b, 0, 0, "", 0, 0, false};
DataField fldMapMsg  = {120, 200, &FreeSansBold9pt7b, 0, 1, "", 0, 0, false};
int mapBarLit = -1;

// SYSTEM
#define SYS_ROWS 11
DataField fldSysVal[SYS_ROWS];

// ============================================================================
// POMOCNE FUNKCE - OBECNE
// ============================================================================
const char* fieldLabel(uint8_t idx) {
  switch (idx) {
    case 0: return "TRIP";
    case 1: return "CAS";
    case 2: return "PRUMER";
    case 3: return "MAX";
    case 4: return "VYSKA";
    case 5: return "ODOMETR";
    case 6: return "SATELITY";
    case 7: return "HODINY";
    case 8: return "STOUPANI";
    case 9: return "SKLON";
    case 10: return "TEP";
    case 11: return "KADENCE";
    case 12: return "BATERIE";
    case 13: return "DO CILE";
    default: return "?";
  }
}

const char* fieldUnit(uint8_t idx) {
  switch (idx) {
    case 0: return "km";
    case 2: return "km/h";
    case 3: return "km/h";
    case 4: return "m";
    case 5: return "km";
    case 8: return "m";
    case 9: return "%";
    case 10: return "bpm";
    case 11: return "rpm";
    case 12: return "%";
    case 13: return "km";
    default: return "";
  }
}

void formatTime(unsigned long ms, char* out, size_t outLen) {
  unsigned long s = ms / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long ss = s % 60;
  if (h > 0) snprintf(out, outLen, "%lu:%02lu:%02lu", h, m, ss);
  else snprintf(out, outLen, "%lu:%02lu", m, ss);
}

float avgSpeedKmh() {
  float hours = rideTimeMs / 3600000.0f;
  return (hours > 0.005f) ? (float)(tripDistanceM / 1000.0) / hours : 0.0f;
}

bool hrFresh() { return hrConnected && hrBpm > 0 && (millis() - hrLastMs) < 5000; }
bool cadFresh() { return cscConnected && (millis() - cadLastMs) < 4000; }

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// unix cas (UTC); 0 = neznamy
uint32_t nowEpoch() {
  if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2024) {
    int64_t days = daysFromCivil(gps.date.year(), gps.date.month(), gps.date.day());
    return (uint32_t)(days * 86400 + gps.time.hour() * 3600 + gps.time.minute() * 60 + gps.time.second());
  }
  if (phoneEpochBase) return phoneEpochBase + (millis() - phoneEpochMs) / 1000;
  return 0;
}

bool localClock(int &hh, int &mm) {
  if (gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024) {
    hh = ((int)gps.time.hour() + timezoneOffsetHours + 24) % 24;
    mm = gps.time.minute();
    return true;
  }
  if (phoneEpochBase) {
    uint32_t e = nowEpoch() + timezoneOffsetHours * 3600;
    hh = (e / 3600) % 24;
    mm = (e / 60) % 60;
    return true;
  }
  return false;
}

double distanceToDestinationAlongRoute();

void formatFieldValue(uint8_t idx, char* out, size_t outLen) {
  switch (idx) {
    case 0: snprintf(out, outLen, "%.2f", tripDistanceM / 1000.0); break;
    case 1: formatTime(rideTimeMs, out, outLen); break;
    case 2: snprintf(out, outLen, "%.1f", avgSpeedKmh()); break;
    case 3: snprintf(out, outLen, "%.1f", maxSpeedKmh); break;
    case 4:
      if (fx.altValid) snprintf(out, outLen, "%.0f", fx.alt);
      else snprintf(out, outLen, "--");
      break;
    case 5: snprintf(out, outLen, "%.1f", odometerM / 1000.0); break;
    case 6: snprintf(out, outLen, "%d", fx.sats); break;
    case 7: {
      int hh, mm;
      if (localClock(hh, mm)) snprintf(out, outLen, "%02d:%02d", hh, mm);
      else snprintf(out, outLen, "--:--");
      break;
    }
    case 8: snprintf(out, outLen, "%.0f", ascentM); break;
    case 9:
      if (gradeValid) snprintf(out, outLen, "%+.1f", gradePct);
      else snprintf(out, outLen, "--");
      break;
    case 10:
      if (hrFresh()) snprintf(out, outLen, "%d", (int)hrBpm);
      else snprintf(out, outLen, "--");
      break;
    case 11:
      if (cadFresh()) snprintf(out, outLen, "%d", (int)cadRpm);
      else snprintf(out, outLen, "--");
      break;
    case 12:
      if (usbPower) snprintf(out, outLen, "USB");
      else if (batPct >= 0) snprintf(out, outLen, "%d", batPct);
      else snprintf(out, outLen, "--");
      break;
    case 13: {
      double km = distanceToDestinationAlongRoute();
      if (km >= 0) snprintf(out, outLen, "%.1f", km);
      else snprintf(out, outLen, "--");
      break;
    }
    default: snprintf(out, outLen, "-"); break;
  }
}

uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t + 0.5f);
  int g = ag + (int)((bg - ag) * t + 0.5f);
  int bl = ab + (int)((bb - ab) * t + 0.5f);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// ============================================================================
// ANTI-FLICKER KRESLENI TEXTU
// ============================================================================
void invalidateField(DataField &f) { f.valid = false; }

void initField(DataField &f, int16_t x, int16_t y, const GFXfont* font, uint16_t color, uint8_t align) {
  f.x = x; f.y = y; f.font = font; f.color = color; f.align = align;
  f.text[0] = 0; f.lastX = x; f.lastColor = color; f.valid = false;
}

int16_t fieldTextX(DataField &f, const char* s) {
  if (f.align == 0) return f.x;
  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(s, 0, f.y, &bx, &by, &bw, &bh);
  int16_t w = bx + (int16_t)bw;
  return (f.align == 1) ? (int16_t)(f.x - w / 2) : (int16_t)(f.x - w);
}

// Smaze presne plochu stareho i noveho textu (sjednoceni bounding boxu) -
// jinak pri zmene sirky znaku zustavaji na displeji zbytky ("rozbity" text).
void drawField(DataField &f, const char* newText, uint16_t bg) {
  if (f.valid && f.lastColor == f.color && strcmp(f.text, newText) == 0) return;

  tft.setFont(f.font);
  int16_t nx = fieldTextX(f, newText);

  int16_t bx, by;
  uint16_t bw, bh;
  int16_t l = 32767, t = 32767, r = -32768, b = -32768;
  if (newText[0]) {
    tft.getTextBounds(newText, nx, f.y, &bx, &by, &bw, &bh);
    l = bx; t = by; r = bx + bw; b = by + bh;
  }
  if (f.valid && f.text[0]) {
    tft.getTextBounds(f.text, f.lastX, f.y, &bx, &by, &bw, &bh);
    l = min(l, bx); t = min(t, by);
    r = max(r, (int16_t)(bx + bw)); b = max(b, (int16_t)(by + bh));
  }
  if (r > l && b > t) tft.fillRect(l - 1, t - 1, (r - l) + 2, (b - t) + 2, bg);

  if (newText[0]) {
    tft.setTextColor(f.color);
    tft.setCursor(nx, f.y);
    tft.print(newText);
  }
  strncpy(f.text, newText, sizeof(f.text) - 1);
  f.text[sizeof(f.text) - 1] = 0;
  f.lastX = nx;
  f.lastColor = f.color;
  f.valid = true;
}

// hodnota + mensi jednotka hned za ni (jednotka se posouva podle sirky hodnoty)
void drawValueUnit(DataField &val, DataField &unit, const char* v, const char* u, uint16_t bg) {
  tft.setFont(val.font);
  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(v, val.x, val.y, &bx, &by, &bw, &bh);
  int16_t ux = (v[0] ? bx + (int16_t)bw : val.x) + 5;
  if (unit.valid && (unit.x != ux || strcmp(unit.text, u) != 0)) drawField(unit, "", bg); // smaz starou
  drawField(val, v, bg);
  if (unit.x != ux) { unit.x = ux; unit.valid = false; }
  drawField(unit, u, bg);
}

void drawStaticText(const char* s, int16_t x, int16_t y, const GFXfont* font, uint16_t color, uint8_t align) {
  DataField f;
  initField(f, x, y, font, color, align);
  tft.setFont(font);
  int16_t nx = fieldTextX(f, s);
  tft.setTextColor(color);
  tft.setCursor(nx, y);
  tft.print(s);
}

void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h) {
  tft.fillRoundRect(x, y, w, h, 6, T->panel);
  tft.drawRoundRect(x, y, w, h, 6, T->line);
}

// ============================================================================
// 7-SEGMENTOVE CISLICE
// ============================================================================
void hexH(int16_t x1, int16_t x2, int16_t yc, int16_t t, uint16_t c) {
  int16_t h = t / 2;
  if (x2 - x1 < 2 * h) return;
  tft.fillRect(x1 + h, yc - h, (x2 - x1) - 2 * h + 1, t, c);
  tft.fillTriangle(x1, yc, x1 + h, yc - h, x1 + h, yc + h, c);
  tft.fillTriangle(x2, yc, x2 - h, yc - h, x2 - h, yc + h, c);
}

void hexV(int16_t xc, int16_t y1, int16_t y2, int16_t t, uint16_t c) {
  int16_t h = t / 2;
  if (y2 - y1 < 2 * h) return;
  tft.fillRect(xc - h, y1 + h, t, (y2 - y1) - 2 * h + 1, c);
  tft.fillTriangle(xc, y1, xc - h, y1 + h, xc + h, y1 + h, c);
  tft.fillTriangle(xc, y2, xc - h, y2 - h, xc + h, y2 - h, c);
}

void segDrawOne(const SegDigit &d, uint8_t seg, uint16_t c) {
  int16_t h = d.t / 2;
  int16_t gp = 1;
  int16_t xl = d.x + h, xr = d.x + d.w - 1 - h;
  int16_t yt = d.y + h, ym = d.y + d.h / 2, yb = d.y + d.h - 1 - h;
  switch (seg) {
    case 0: hexH(xl + gp, xr - gp, yt, d.t, c); break;       // a
    case 1: hexV(xr, yt + gp, ym - gp, d.t, c); break;       // b
    case 2: hexV(xr, ym + gp, yb - gp, d.t, c); break;       // c
    case 3: hexH(xl + gp, xr - gp, yb, d.t, c); break;       // d
    case 4: hexV(xl, ym + gp, yb - gp, d.t, c); break;       // e
    case 5: hexV(xl, yt + gp, ym - gp, d.t, c); break;       // f
    case 6: hexH(xl + gp, xr - gp, ym, d.t, c); break;       // g
  }
}

void segInit(SegDigit &d, int16_t x, int16_t y, int16_t w, int16_t h, int16_t t) {
  d.x = x; d.y = y; d.w = w; d.h = h; d.t = t;
  d.mask = 0; d.onCol = 0; d.offCol = 0; d.valid = false;
}

// val: 0-9, nebo -1 = prazdna (jen "duchove" segmentu)
void segDraw(SegDigit &d, int val, uint16_t on, uint16_t off) {
  uint8_t mask = (val >= 0 && val <= 9) ? SEG_DIGITS[val] : 0;
  bool full = !d.valid || d.offCol != off;
  bool colorChange = d.onCol != on;
  for (uint8_t s = 0; s < 7; s++) {
    bool isOn = (mask >> s) & 1;
    bool wasOn = (d.mask >> s) & 1;
    if (full || isOn != wasOn || (isOn && colorChange)) segDrawOne(d, s, isOn ? on : off);
  }
  d.mask = mask; d.onCol = on; d.offCol = off; d.valid = true;
}

void segNumberInit(SegNumber &n, int16_t x, int16_t y, int16_t bigW, int16_t bigH, int16_t bigT,
                   int16_t smallW, int16_t smallH, int16_t smallT, int16_t gap) {
  segInit(n.d[0], x, y, bigW, bigH, bigT);
  segInit(n.d[1], x + bigW + gap, y, bigW, bigH, bigT);
  int16_t dotS = max((int16_t)4, (int16_t)(bigT - 1));
  n.dotS = dotS;
  n.dotX = x + 2 * bigW + gap + 3;
  n.dotY = y + bigH - dotS;
  segInit(n.d[2], n.dotX + dotS + 3, y + bigH - smallH, smallW, smallH, smallT);
  n.dotValid = false;
}

void segNumberDraw(SegNumber &n, float v, uint16_t on, uint16_t off) {
  if (v < 0) v = 0;
  if (v > 99.9f) v = 99.9f;
  int tenths = (int)lroundf(v * 10.0f);
  int whole = tenths / 10;
  int frac = tenths % 10;
  segDraw(n.d[0], whole >= 10 ? whole / 10 : -1, on, off);
  segDraw(n.d[1], whole % 10, on, off);
  segDraw(n.d[2], frac, on, off);
  if (!n.dotValid || n.dotCol != on) {
    tft.fillRect(n.dotX, n.dotY, n.dotS, n.dotS, on);
    n.dotCol = on;
    n.dotValid = true;
  }
}

void segNumberInvalidate(SegNumber &n) {
  for (int i = 0; i < 3; i++) n.d[i].valid = false;
  n.dotValid = false;
}

// ============================================================================
// STATUSOVA LISTA
// ============================================================================
void statusBarReset() {
  sbRec = -1; sbBle = -1; sbDots = -1; sbBatLevel = -2; sbBatChg = -1;
  invalidateField(fldClock); invalidateField(fldSats); invalidateField(fldBat);
}

void drawRecIndicator() {
  int8_t st = 0;
  if (recording && manualPaused) st = 2;
  else if (recording && autoPaused) st = 3;
  else if (recording) st = 1;
  if (st == sbRec) return;
  tft.fillRect(56, 3, 16, 17, T->bg);
  if (st == 1) tft.fillCircle(63, 11, 6, T->danger);
  else if (st == 2) { tft.fillRect(58, 5, 4, 12, T->warn); tft.fillRect(65, 5, 4, 12, T->warn); }
  else if (st == 3) { tft.drawRect(58, 5, 4, 12, T->muted); tft.drawRect(65, 5, 4, 12, T->muted); }
  sbRec = st;
}

void drawScreenDots() {
  if (sbDots == (int8_t)currentScreen) return;
  tft.fillRect(76, 4, 50, 15, T->bg);
  for (int i = 0; i < NUM_SCREENS; i++) {
    int cx = 84 + i * 11;
    if (i == currentScreen) tft.fillCircle(cx, 11, 3, T->accent);
    else tft.drawCircle(cx, 11, 3, T->muted);
  }
  sbDots = currentScreen;
}

void drawBleIcon() {
  int8_t c = bleConnected ? 1 : 0;
  if (c == sbBle) return;
  tft.fillRect(166, 2, 12, 19, T->bg);
  uint16_t col = c ? T->ble : T->line;
  int x = 171;
  tft.drawLine(x, 3, x, 19, col);
  tft.drawLine(x, 3, x + 4, 7, col);
  tft.drawLine(x + 4, 7, x - 4, 15, col);
  tft.drawLine(x - 4, 7, x + 4, 15, col);
  tft.drawLine(x + 4, 15, x, 19, col);
  sbBle = c;
}

void drawSatIcon(uint16_t col) {
  // mala "parabola"
  tft.fillRect(130, 4, 14, 14, T->bg);
  tft.fillCircle(136, 12, 3, col);
  tft.drawCircle(136, 12, 6, col);
  tft.fillRect(130, 12, 14, 7, T->bg);
  tft.drawLine(136, 12, 141, 6, col);
}

void drawBatteryIcon() {
  if (BAT_ADC_PIN < 0) return;
  int8_t lvl = usbPower ? 10 : (batPct < 0 ? -1 : (int8_t)((batPct + 5) / 10));
  int8_t chg = usbPower ? 1 : 0;
  if (lvl == sbBatLevel && chg == sbBatChg) return;
  int x = 210, y = 6, w = 24, h = 12;
  tft.fillRect(x - 1, y - 1, w + 5, h + 2, T->bg);
  tft.drawRect(x, y, w, h, T->text);
  tft.fillRect(x + w, y + 3, 3, h - 6, T->text);
  if (lvl >= 0) {
    uint16_t col = (lvl <= 1) ? T->danger : (lvl <= 3 ? T->warn : T->ok);
    if (chg) col = T->ble;
    int fw = (w - 4) * lvl / 10;
    if (fw > 0) tft.fillRect(x + 2, y + 2, fw, h - 4, col);
  }
  if (chg) {
    // blesk
    tft.fillTriangle(x + 13, y + 1, x + 8, y + 7, x + 12, y + 7, T->warn);
    tft.fillTriangle(x + 11, y + 5, x + 15, y + 5, x + 10, y + 11, T->warn);
  }
  sbBatLevel = lvl;
  sbBatChg = chg;
}

void updateStatusBar() {
  char buf[16];
  int hh, mm;
  if (localClock(hh, mm)) snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  else snprintf(buf, sizeof(buf), "--:--");
  fldClock.color = T->text;
  drawField(fldClock, buf, T->bg);

  drawRecIndicator();
  drawScreenDots();

  static int8_t lastSatCol = -1;
  int8_t satCol = fx.valid ? 1 : 0;
  if (satCol != lastSatCol || !fldSats.valid) {
    drawSatIcon(fx.valid ? T->ok : T->muted);
    lastSatCol = satCol;
  }
  snprintf(buf, sizeof(buf), "%d", fx.sats);
  fldSats.color = fx.valid ? T->text : T->muted;
  drawField(fldSats, buf, T->bg);

  drawBleIcon();

  if (BAT_ADC_PIN >= 0) {
    if (usbPower) snprintf(buf, sizeof(buf), "USB");
    else if (batPct >= 0) snprintf(buf, sizeof(buf), "%d%%", batPct);
    else snprintf(buf, sizeof(buf), "--");
    fldBat.color = (!usbPower && batPct >= 0 && batPct <= 15) ? T->danger : T->text;
    drawField(fldBat, buf, T->bg);
    drawBatteryIcon();
  }
}

// ============================================================================
// BREADCRUMB (projeta stopa pro mapu)
// ============================================================================
void breadcrumbClear() {
  breadcrumbCount = 0;
  breadcrumbHead = 0;
  haveBreadcrumbRef = false;
  lastDrawnBreadcrumbCount = -1;
}

void breadcrumbPush(double lat, double lng) {
  int idx = (breadcrumbHead + breadcrumbCount) % BREADCRUMB_SIZE;
  if (breadcrumbCount < BREADCRUMB_SIZE) breadcrumbCount++;
  else breadcrumbHead = (breadcrumbHead + 1) % BREADCRUMB_SIZE;
  breadcrumb[idx].lat = lat;
  breadcrumb[idx].lng = lng;
}

void maybeAddBreadcrumb(double lat, double lng) {
  if (!haveBreadcrumbRef) {
    lastBreadcrumbLat = lat; lastBreadcrumbLng = lng;
    haveBreadcrumbRef = true;
    breadcrumbPush(lat, lng);
    return;
  }
  double d = TinyGPSPlus::distanceBetween(lastBreadcrumbLat, lastBreadcrumbLng, lat, lng);
  if (d >= 15.0) {
    breadcrumbPush(lat, lng);
    lastBreadcrumbLat = lat; lastBreadcrumbLng = lng;
  }
}

// ============================================================================
// ZAZNAM JIZDY (LittleFS)  /rides/<id>.sum (JSON) + /rides/<id>.trk (int32 lat,lng *1e6)
// ============================================================================
void ridePath(uint16_t id, const char* ext, char* out, size_t n) {
  snprintf(out, n, "/rides/%u.%s", (unsigned)id, ext);
}

void trackReset() {
  trkCount = 0;
  trkSpacingM = 20.0f;
  trkHaveLast = false;
}

void trackAdd(double lat, double lng) {
  if (trkHaveLast && TinyGPSPlus::distanceBetween(trkLastLat, trkLastLng, lat, lng) < trkSpacingM) return;
  if (trkCount >= MAX_TRACK_POINTS) {
    // plno: nech kazdy druhy bod a zdvojnasob rozestup -> stopa pokryje libovolne dlouhou jizdu
    int j = 0;
    for (int i = 0; i < trkCount; i += 2) { trkLat[j] = trkLat[i]; trkLng[j] = trkLng[i]; j++; }
    trkCount = j;
    trkSpacingM *= 2.0f;
  }
  trkLat[trkCount] = (int32_t)lround(lat * 1e6);
  trkLng[trkCount] = (int32_t)lround(lng * 1e6);
  trkCount++;
  trkLastLat = lat; trkLastLng = lng; trkHaveLast = true;
}

void buildSummaryJson(char* out, size_t n, bool done) {
  snprintf(out, n,
    "{\"id\":%u,\"ts\":%lu,\"dst\":%.3f,\"tim\":%lu,\"max\":%.1f,\"avg\":%.1f,\"asc\":%.0f,\"pts\":%d,\"done\":%d}",
    (unsigned)curRideId, (unsigned long)rideStartEpoch, tripDistanceM / 1000.0, rideTimeMs / 1000,
    maxSpeedKmh, avgSpeedKmh(), ascentM, trkCount, done ? 1 : 0);
}

void rideSave(bool done) {
  if (!fsOk || curRideId == 0) return;
  char path[32];
  ridePath(curRideId, "trk", path, sizeof(path));
  File f = LittleFS.open(path, "w");
  if (f) {
    int32_t buf[64];
    int bi = 0;
    for (int i = 0; i < trkCount; i++) {
      buf[bi++] = trkLat[i];
      buf[bi++] = trkLng[i];
      if (bi == 64) { f.write((uint8_t*)buf, sizeof(buf)); bi = 0; }
    }
    if (bi) f.write((uint8_t*)buf, bi * sizeof(int32_t));
    f.close();
  }
  char json[200];
  buildSummaryJson(json, sizeof(json), done);
  ridePath(curRideId, "sum", path, sizeof(path));
  f = LittleFS.open(path, "w");
  if (f) { f.print(json); f.close(); }
  prefs.putFloat("trkSp", trkSpacingM);
}

void rideDelete(uint16_t id) {
  if (!fsOk) return;
  char path[32];
  ridePath(id, "trk", path, sizeof(path)); LittleFS.remove(path);
  ridePath(id, "sum", path, sizeof(path)); LittleFS.remove(path);
}

bool rideIsDone(uint16_t id) {
  char path[32];
  ridePath(id, "sum", path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  char buf[200];
  int n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n > 0 ? n : 0] = 0;
  return strstr(buf, "\"done\":1") != nullptr;
}

// vrati serazena ID ulozenych jizd (jen dokoncene, pokud onlyDone)
int listRideIds(uint16_t* ids, int maxIds, bool onlyDone) {
  if (!fsOk) return 0;
  int n = 0;
  File dir = LittleFS.open("/rides");
  if (!dir || !dir.isDirectory()) return 0;
  File f = dir.openNextFile();
  while (f && n < maxIds) {
    const char* name = f.name();
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    const char* dot = strrchr(name, '.');
    if (dot && !strcmp(dot, ".sum")) {
      uint16_t id = (uint16_t)atoi(name);
      f.close();
      if (id && (!onlyDone || rideIsDone(id))) ids[n++] = id;
    } else {
      f.close();
    }
    f = dir.openNextFile();
  }
  dir.close();
  for (int i = 1; i < n; i++) {           // insertion sort
    uint16_t v = ids[i]; int j = i - 1;
    while (j >= 0 && ids[j] > v) { ids[j + 1] = ids[j]; j--; }
    ids[j + 1] = v;
  }
  return n;
}

void ridePrune() {
  uint16_t ids[64];
  int n = listRideIds(ids, 64, false);
  for (int i = 0; n - i >= MAX_STORED_RIDES; i++) rideDelete(ids[i]);
}

void rideRestore() {
  if (!fsOk || curRideId == 0) return;
  char path[32];
  ridePath(curRideId, "trk", path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return;
  trkCount = 0;
  int32_t pair[2];
  while (trkCount < MAX_TRACK_POINTS && f.read((uint8_t*)pair, sizeof(pair)) == sizeof(pair)) {
    trkLat[trkCount] = pair[0];
    trkLng[trkCount] = pair[1];
    trkCount++;
  }
  f.close();
  trkSpacingM = prefs.getFloat("trkSp", 20.0f);
  if (trkCount > 0) {
    trkLastLat = trkLat[trkCount - 1] / 1e6;
    trkLastLng = trkLng[trkCount - 1] / 1e6;
    trkHaveLast = true;
  }
}

// ============================================================================
// OVERLAY (souhrn jizdy, hlasky)
// ============================================================================
void showOverlay(const char* title, const char* l1, const char* l2, const char* l3, unsigned long ms) {
  int16_t x = 14, y = 92, w = 212, h = 130;
  tft.fillRoundRect(x + 3, y + 3, w, h, 10, T->line);
  tft.fillRoundRect(x, y, w, h, 10, T->panel);
  tft.drawRoundRect(x, y, w, h, 10, T->accent);
  drawStaticText(title, 120, y + 30, &FreeSansBold12pt7b, T->accent, 1);
  if (l1) drawStaticText(l1, 120, y + 62, &FreeSansBold9pt7b, T->text, 1);
  if (l2) drawStaticText(l2, 120, y + 86, &FreeSans9pt7b, T->text, 1);
  if (l3) drawStaticText(l3, 120, y + 110, &FreeSans9pt7b, T->muted, 1);
  overlayActive = true;
  overlayUntil = millis() + ms;
}

void forceFullRedraw() {
  tft.fillScreen(T->bg);
  lastDrawnScreen = 255;
  statusBarReset();
}

void showRideSummary() {
  char l1[40], l2[40], l3[40], t[16];
  formatTime(rideTimeMs, t, sizeof(t));
  snprintf(l1, sizeof(l1), "%.2f km   %s", tripDistanceM / 1000.0, t);
  snprintf(l2, sizeof(l2), "prumer %.1f  max %.1f", avgSpeedKmh(), maxSpeedKmh);
  snprintf(l3, sizeof(l3), "stoupani %.0f m", ascentM);
  showOverlay("JIZDA ULOZENA", l1, l2, l3, 10000);
}

// ============================================================================
// RIDE STATE
// ============================================================================
void resetTrip() {
  tripDistanceM = 0;
  rideTimeMs = 0;
  maxSpeedKmh = 0;
  ascentM = 0;
  haveRefPoint = false;
  haveAltFilt = false;
  gradeValid = false;
  breadcrumbClear();
  gaugeAvgSeg = -1;
  gaugeMaxSeg = -1;
}

void startRide() {
  if (recording) return;
  resetTrip();
  recording = true;
  manualPaused = false;
  autoPaused = false;
  mapStaticDrawn = false;
  trackReset();
  rideStartEpoch = nowEpoch();
  if (fsOk) {
    ridePrune();
    curRideId = ++rideSeq;
    prefs.putUShort("rseq", rideSeq);
    prefs.putUShort("rid", curRideId);
  }
  lastRideSaveMs = millis();
}

void stopRide() {
  if (!recording) return;
  recording = false;
  manualPaused = false;
  autoPaused = false;
  bool meaningful = tripDistanceM >= 100.0 || rideTimeMs >= 120000UL;
  if (meaningful) {
    rideSave(true);
    showRideSummary();
  } else if (curRideId) {
    rideDelete(curRideId);   // omylem zmacknuty start - nic neukladat
  }
  curRideId = 0;
  prefs.putUShort("rid", 0);
  prefs.putBool("rec", false);
}

void pauseRide() { if (recording) manualPaused = true; }
void resumeRide() { if (recording) manualPaused = false; }

// ============================================================================
// GPS + SIMULACE + FILTRACE
// ============================================================================
void readGpsSerial() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

void simStep() {
  unsigned long now = millis();
  fx.updated = false;
  if (simLastMs && now - simLastMs < 1000) return;
  float dt = simLastMs ? (now - simLastMs) / 1000.0f : 1.0f;
  simLastMs = now;
  simT += dt;

  // kazde 2 minuty kratke zastaveni (ukaze autopauzu)
  bool stopped = fmodf(simT, 120.0f) > 112.0f;
  float kmh = stopped ? 0.0f : 24.0f + 9.0f * sinf(simT / 23.0f) + 3.0f * sinf(simT / 5.0f);
  double stepM = kmh / 3.6 * dt;

  if (routeValid && routePointCount > 1) {
    if (simIdx >= routePointCount) simIdx = 0;
    double tLat = routePoints[simIdx].lat, tLng = routePoints[simIdx].lng;
    double d = TinyGPSPlus::distanceBetween(simLat, simLng, tLat, tLng);
    if (d < stepM + 1.0) {
      simIdx = (simIdx + 1) % routePointCount;
    }
    simHeading = TinyGPSPlus::courseTo(simLat, simLng, tLat, tLng);
  } else {
    simHeading += 6.0f * sinf(simT / 40.0f) * dt;
  }
  double h = simHeading * DEG_TO_RAD;
  simLat += stepM * cos(h) / 111320.0;
  simLng += stepM * sin(h) / (111320.0 * cos(simLat * DEG_TO_RAD));
  simAlt = 300.0f + 45.0f * sinf(simT / 90.0f) + 10.0f * sinf(simT / 17.0f);

  fx.valid = true;
  fx.updated = true;
  fx.lat = simLat; fx.lng = simLng;
  fx.kmh = kmh;
  fx.alt = simAlt; fx.altValid = true;
  fx.course = simHeading;
  fx.sats = 9;
  fx.hdop = 0.9f;
}

void updateFix() {
  if (simEnabled) { simStep(); return; }
  fx.updated = false;
  bool fresh = gps.location.isValid() && gps.location.age() < 3000;
  fx.valid = fresh;
  if (gps.location.isUpdated()) {
    fx.lat = gps.location.lat();
    fx.lng = gps.location.lng();
    fx.updated = fresh;
  }
  fx.kmh = (gps.speed.isValid() && gps.speed.age() < 3000 && fresh) ? (float)gps.speed.kmph() : 0.0f;
  if (gps.altitude.isValid() && gps.altitude.age() < 5000) {
    fx.alt = gps.altitude.meters();
    fx.altValid = true;
  } else {
    fx.altValid = false;
  }
  if (gps.course.isValid()) fx.course = gps.course.deg();
  fx.sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  fx.hdop = gps.hdop.isValid() ? (float)gps.hdop.hdop() : 99.0f;
}

void updateFilteredSpeed() {
  if (!simEnabled && !fx.updated && fx.valid) return; // filtruj jen pri nove zprave
  float raw = fx.valid ? fx.kmh : 0.0f;
  speedEmaRaw = SPEED_EMA_ALPHA * raw + (1.0f - SPEED_EMA_ALPHA) * speedEmaRaw;
  filteredSpeedKmh = (speedEmaRaw < SPEED_STOP_THRESHOLD_KMH) ? 0.0f : speedEmaRaw;
  if (!fx.valid) { speedEmaRaw = 0; filteredSpeedKmh = 0; }
}

void updateAltitude(double stepM) {
  if (!fx.altValid) return;
  if (!haveAltFilt) {
    altFilt = fx.alt; altRef = fx.alt; haveAltFilt = true;
    gradeDistRef = tripDistanceM; gradeAltRef = fx.alt;
    return;
  }
  altFilt = 0.2f * fx.alt + 0.8f * altFilt;
  float diff = altFilt - altRef;
  // hystereze 4 m - GPS vyska je hlucna, bez ni by stoupani nesmyslne rostlo
  if (diff >= 4.0f) {
    if (filteredSpeedKmh > 0 && (!recording || (!manualPaused && !autoPaused))) ascentM += diff;
    altRef = altFilt;
  } else if (diff <= -4.0f) {
    altRef = altFilt;
  }
  double dd = tripDistanceM - gradeDistRef;
  if (dd >= 60.0) {
    float g = (altFilt - gradeAltRef) / (float)dd * 100.0f;
    if (g > 25) g = 25;
    if (g < -25) g = -25;
    gradePct = gradeValid ? 0.5f * g + 0.5f * gradePct : g;
    gradeValid = true;
    gradeDistRef = tripDistanceM;
    gradeAltRef = altFilt;
  } else if (dd < 0) {
    gradeDistRef = tripDistanceM;
  }
  (void)stepM;
}

void updateDistanceAndStats() {
  if (!fx.updated || !fx.valid) return;
  double lat = fx.lat, lng = fx.lng;
  double stepM = 0;

  if (!haveRefPoint) {
    refLat = lat; refLng = lng; haveRefPoint = true;
  } else {
    stepM = TinyGPSPlus::distanceBetween(refLat, refLng, lat, lng);
    bool moving = filteredSpeedKmh >= 3.0f;
    bool goodFix = fx.hdop < 6.0f;
    if (stepM > 250.0) {
      // skok polohy (GPS glitch / ztrata signalu) - nepricitat
      refLat = lat; refLng = lng;
      stepM = 0;
    } else if (stepM >= 2.0 && goodFix) {
      if (moving && !(recording && manualPaused)) {
        tripDistanceM += stepM;
        odometerM += stepM;
      }
      refLat = lat; refLng = lng;
    } else {
      stepM = 0;
    }
  }

  if (filteredSpeedKmh > maxSpeedKmh) maxSpeedKmh = filteredSpeedKmh;
  updateAltitude(stepM);
  maybeAddBreadcrumb(lat, lng);
  if (recording && !manualPaused) trackAdd(lat, lng);
}

void updateRideClock() {
  unsigned long now = millis();
  unsigned long dt = now - lastLoopMillis;
  lastLoopMillis = now;
  bool isMoving = filteredSpeedKmh > 0.0f;

  if (recording && !manualPaused && autoPauseEnabled) {
    if (isMoving) { stillMs = 0; autoPaused = false; }
    else { stillMs += dt; if (stillMs > 3000) autoPaused = true; }
  } else {
    autoPaused = false;
    stillMs = 0;
  }

  bool runningTime = recording ? (!manualPaused && !autoPaused) : isMoving;
  if (runningTime) rideTimeMs += dt;
  if (isMoving) lastActivityMs = now;
}

// ============================================================================
// BATERIE + PODSVICENI + SPANEK
// ============================================================================
int lipoPercent(float mv) {
  static const uint16_t V[] = {3400, 3550, 3670, 3710, 3750, 3780, 3810, 3860, 3920, 4000, 4100, 4180};
  static const uint8_t  P[] = {0,    5,    10,   20,   30,   40,   50,   60,   70,   80,   90,   100};
  const int N = sizeof(V) / sizeof(V[0]);
  if (mv <= V[0]) return 0;
  if (mv >= V[N - 1]) return 100;
  for (int i = 1; i < N; i++) {
    if (mv < V[i]) {
      float t = (mv - V[i - 1]) / (float)(V[i] - V[i - 1]);
      return (int)(P[i - 1] + t * (P[i] - P[i - 1]) + 0.5f);
    }
  }
  return 100;
}

void updateBattery() {
  if (BAT_ADC_PIN < 0) return;
  unsigned long now = millis();
  if (now - lastBatMs < 1000) return;
  lastBatMs = now;
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  float mv = (sum / 8.0f) * BAT_DIVIDER;
  batMv = (batMv < 100) ? mv : 0.8f * batMv + 0.2f * mv;
  // napajeci vetev ~4.6 V = jede z USB (load-sharing), baterie se nabiji
  usbPower = batMv > 4400;
  if (!usbPower) batPct = lipoPercent(batMv);

  if (!usbPower && batMv > 2500 && batMv < 3350) lowBatSeconds++;
  else lowBatSeconds = 0;
}

void setBacklight(int pct) {
#if TFT_BL_PIN >= 0
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (pct == appliedBacklight) return;
  uint32_t duty = (uint32_t)pct * 255 / 100;
  if (TFT_BL_ACTIVE_LOW) duty = 255 - duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL_PIN, duty);
#else
  ledcWrite(0, duty);
#endif
  appliedBacklight = pct;
#else
  (void)pct;
#endif
}

void initBacklight() {
#if TFT_BL_PIN >= 0
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL_PIN, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL_PIN, 0);
#endif
  appliedBacklight = -1;
  setBacklight(brightnessPct);
#endif
}

void updateBacklight() {
  unsigned long idle = millis() - lastActivityMs;
  int target = brightnessPct;
  // stoji se a nikdo nemacka - ztlumit (setri baterii), pohyb/tlacitko rozsviti
  if (idle > 60000UL && filteredSpeedKmh == 0) target = max(5, brightnessPct / 4);
  setBacklight(target);
}

void gpsPower(bool on) {
#if GPS_EN_PIN >= 0
  pinMode(GPS_EN_PIN, OUTPUT);
  digitalWrite(GPS_EN_PIN, (on ^ (GPS_EN_ACTIVE_LOW != 0)) ? HIGH : LOW);
#else
  (void)on;
#endif
}

void savePreferencesPeriodic();

void powerOff(const char* reason) {
#if WAKE_BTN_PIN >= 0
  showOverlay("VYPINAM", reason, "zapnes tlacitkem MODE", nullptr, 3000);
  if (recording) rideSave(false);
  savePreferencesPeriodic();
  delay(1500);
  // pockej na uvolneni tlacitka, jinak by se hned probudil
  unsigned long t0 = millis();
  while (digitalRead(WAKE_BTN_PIN) == LOW && millis() - t0 < 5000) delay(10);
  delay(50);
  setBacklight(0);
  tft.enableDisplay(false);
  tft.enableSleep(true);
  gpsPower(false);
  NimBLEDevice::deinit(true);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BTN_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
  esp_deep_sleep_start();
#else
  showOverlay("VYPNUTI", "Tato deska neumi spat.", "Vypni vypinacem.", reason, 3000);
#endif
}

void updatePowerManagement() {
  unsigned long now = millis();
  if (BAT_ADC_PIN >= 0 && !usbPower && batPct >= 0 && batPct <= 10 && !lowBatWarned) {
    lowBatWarned = true;
    showOverlay("SLABA BATERIE", "Zbyva malo energie", "nabij pres USB-C", nullptr, 5000);
  }
  if (batPct > 20 || usbPower) lowBatWarned = false;

#if WAKE_BTN_PIN >= 0
  if (lowBatSeconds > 30) powerOff("Baterie je vybita");
  bool idle = !recording && !bleConnected && !usbPower && filteredSpeedKmh == 0;
  if (autoOffMin > 0 && idle && (now - lastActivityMs) > (unsigned long)autoOffMin * 60000UL) {
    powerOff("Neaktivita");
  }
#else
  (void)now;
#endif
}

// ============================================================================
// TLACITKA (debounce 30ms, dlouhy stisk podle longMs)
// ============================================================================
int pollButton(ButtonState &b) {
  bool reading = (digitalRead(b.pin) == LOW);
  unsigned long now = millis();
  int result = 0;

  if (reading != b.lastReading) {
    b.lastChangeMs = now;
    b.lastReading = reading;
  }
  if ((now - b.lastChangeMs) > 30) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState) {
        b.pressStartMs = now;
        b.longFired = false;
      } else if (!b.longFired) {
        result = 1; // kratky stisk pri pusteni
      }
    } else if (b.stableState && !b.longFired && (now - b.pressStartMs) >= b.longMs) {
      b.longFired = true;
      result = 2; // dlouhy stisk
    }
  }
  return result;
}

void applyTheme(uint8_t idx) {
  if (idx >= NUM_THEMES) idx = 0;
  themeIdx = idx;
  T = &THEMES[themeIdx];
  forceFullRedraw();
}

void handleButtons() {
  int e1 = pollButton(btn1);
  int e2 = pollButton(btn2);
  int e3 = pollButton(btn3);
  if (e1 || e2 || e3) {
    bool wasDim = appliedBacklight >= 0 && appliedBacklight < brightnessPct;
    lastActivityMs = millis();
    if (wasDim) { updateBacklight(); return; } // prvni stisk jen rozsviti displej
  }

  if (overlayActive && (e1 == 1 || e2 == 1 || e3 == 1)) {
    overlayActive = false;
    forceFullRedraw();
    return;
  }

  if (e1 == 1) currentScreen = (currentScreen + 1) % NUM_SCREENS;
  else if (e1 == 2) powerOff("Tlacitko MODE");

  if (e2 == 1) { if (!recording) startRide(); else stopRide(); }
  else if (e2 == 2) { resetTrip(); }

  if (e3 == 1) {
    if (recording) { if (manualPaused) resumeRide(); else pauseRide(); }
    else currentScreen = (currentScreen + NUM_SCREENS - 1) % NUM_SCREENS;
  } else if (e3 == 2) {
    applyTheme((themeIdx + 1) % NUM_THEMES);
    prefs.putUChar("theme", themeIdx);
  }
}

// ============================================================================
// DASH - oblouk rychlosti (jako na DS), 7-seg cislice, dlazdice 2x2
// ============================================================================
uint16_t gaugeSegColor(int i) {
  float t = (float)i / (G_SEGS - 1);
  if (t < 0.5f) return lerp565(T->gaugeLo, T->gaugeMid, t * 2.0f);
  return lerp565(T->gaugeMid, T->gaugeHi, (t - 0.5f) * 2.0f);
}

void gaugeSeg(int i, uint16_t col) {
  float a0 = (135.0f + i * 6.0f + 0.6f) * DEG_TO_RAD;
  float a1 = (135.0f + i * 6.0f + 5.4f) * DEG_TO_RAD;
  int16_t x0 = G_CX + lroundf(cosf(a0) * G_RIN), y0 = G_CY + lroundf(sinf(a0) * G_RIN);
  int16_t x1 = G_CX + lroundf(cosf(a0) * G_ROUT), y1 = G_CY + lroundf(sinf(a0) * G_ROUT);
  int16_t x2 = G_CX + lroundf(cosf(a1) * G_ROUT), y2 = G_CY + lroundf(sinf(a1) * G_ROUT);
  int16_t x3 = G_CX + lroundf(cosf(a1) * G_RIN), y3 = G_CY + lroundf(sinf(a1) * G_RIN);
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  tft.fillTriangle(x0, y0, x2, y2, x3, y3, col);
}

int speedToSeg(float kmh) {
  int n = (int)lroundf(kmh / gaugeMaxKmh * G_SEGS);
  if (n < 0) n = 0;
  if (n > G_SEGS) n = G_SEGS;
  return n;
}

void gaugeTickDots() {
  int step = gaugeMaxKmh > 45 ? 10 : 5;
  for (int v = 0; v <= (int)gaugeMaxKmh; v += step) {
    float a = (135.0f + 270.0f * v / gaugeMaxKmh) * DEG_TO_RAD;
    tft.fillCircle(G_CX + lroundf(cosf(a) * 76), G_CY + lroundf(sinf(a) * 76), 1, T->muted);
  }
}

void gaugeMarker(int seg, uint16_t col) {
  if (seg < 0) return;
  float a = (135.0f + seg * 6.0f) * DEG_TO_RAD;
  float ca = cosf(a), sa = sinf(a);
  // trojuhelnik mirici ven na oblouk
  int16_t tx = G_CX + lroundf(ca * 72), ty = G_CY + lroundf(sa * 72);
  int16_t bx = G_CX + lroundf(ca * 64), by = G_CY + lroundf(sa * 64);
  int16_t px = lroundf(-sa * 4), py = lroundf(ca * 4);
  tft.fillTriangle(tx, ty, bx + px, by + py, bx - px, by - py, col);
}

void updateGauge(float kmh) {
  int n = speedToSeg(kmh);
  if (gaugeLit < 0) {
    for (int i = 0; i < G_SEGS; i++) gaugeSeg(i, i < n ? gaugeSegColor(i) : T->gaugeTrack);
  } else if (n > gaugeLit) {
    for (int i = gaugeLit; i < n; i++) gaugeSeg(i, gaugeSegColor(i));
  } else if (n < gaugeLit) {
    for (int i = n; i < gaugeLit; i++) gaugeSeg(i, T->gaugeTrack);
  }
  gaugeLit = n;

  int avgSeg = (recording || tripDistanceM > 100) ? speedToSeg(avgSpeedKmh()) : -1;
  int maxSeg = maxSpeedKmh > 1 ? speedToSeg(maxSpeedKmh) : -1;
  if (avgSeg != gaugeAvgSeg || maxSeg != gaugeMaxSeg) {
    gaugeMarker(gaugeAvgSeg, T->bg);
    gaugeMarker(gaugeMaxSeg, T->bg);
    gaugeTickDots();
    gaugeMarker(maxSeg, T->danger);
    gaugeMarker(avgSeg, T->accent);
    gaugeAvgSeg = avgSeg;
    gaugeMaxSeg = maxSeg;
  }
}

void layoutDash() {
  tft.fillRect(0, 23, 240, 297, T->bg);
  gaugeLit = -1;
  gaugeAvgSeg = -1;
  gaugeMaxSeg = -1;
  gaugeTickDots();
  segNumberInit(dashSpeed, 69, 90, 32, 56, 7, 22, 38, 5, 5);
  drawStaticText("km/h", 120, 166, &FreeSans9pt7b, T->muted, 1);
  fldDashMsg.color = T->warn;
  invalidateField(fldDashMsg);
  invalidateField(fldDashTime);

  for (int i = 0; i < 4; i++) {
    int16_t x = TILE_X[i], y = TILE_Y[i];
    drawPanel(x, y, TILE_W, TILE_H);
    initField(fldTileLbl[i], x + 7, y + 17, &FreeSans9pt7b, T->muted, 0);
    initField(fldTileUnit[i], x + 8, y + 47, &FreeSans9pt7b, T->muted, 0);
    initField(fldTileVal[i], x + 8, y + 47, &FreeSansBold12pt7b, T->text, 0);
  }
}

void updateDash() {
  bool fix = fx.valid;
  updateGauge(fix ? filteredSpeedKmh : 0.0f);
  segNumberDraw(dashSpeed, fix ? filteredSpeedKmh : 0.0f, fix ? T->text : T->muted, T->segOff);

  const char* msg = "";
  uint16_t mc = T->warn;
  if (!fix) msg = simEnabled ? "SIMULACE" : "BEZ GPS";
  else if (recording && manualPaused) msg = "PAUZA";
  else if (recording && autoPaused) { msg = "AUTOPAUZA"; mc = T->muted; }
  else if (simEnabled) { msg = "SIMULACE"; mc = T->muted; }
  fldDashMsg.color = mc;
  drawField(fldDashMsg, msg, T->bg);

  char t[16];
  formatTime(rideTimeMs, t, sizeof(t));
  fldDashTime.color = recording ? (manualPaused || autoPaused ? T->warn : T->accent) : T->muted;
  drawField(fldDashTime, t, T->bg);

  for (int i = 0; i < 4; i++) {
    uint8_t id = fieldConfig[i];
    drawField(fldTileLbl[i], fieldLabel(id), T->panel);
    char v[24];
    formatFieldValue(id, v, sizeof(v));
    drawValueUnit(fldTileVal[i], fldTileUnit[i], v, fieldUnit(id), T->panel);
  }
}

// ============================================================================
// STATISTIKY
// ============================================================================
const char* STAT_LABELS[STAT_ROWS] = {"CAS JIZDY", "VZDALENOST", "PRUMER", "MAX", "STOUPANI", "VYSKA", "SKLON", "TEP / KADENCE"};

void layoutStats() {
  tft.fillRect(0, 23, 240, 297, T->bg);
  for (int i = 0; i < STAT_ROWS; i++) {
    int16_t y = 48 + i * 27;
    drawStaticText(STAT_LABELS[i], 8, y, &FreeSans9pt7b, T->muted, 0);
    tft.drawFastHLine(8, y + 8, 224, T->line);
    initField(fldStatVal[i], 232, y, &FreeSansBold12pt7b, T->text, 2);
  }
  drawPanel(4, 266, 232, 50);
  drawStaticText("ODOMETR", 12, 286, &FreeSans9pt7b, T->muted, 0);
  drawStaticText("km", 12, 306, &FreeSans9pt7b, T->muted, 0);
  fldStatOdo.color = T->accent;
  invalidateField(fldStatOdo);
}

void updateStats() {
  char b[28], t[16];
  formatTime(rideTimeMs, t, sizeof(t));
  drawField(fldStatVal[0], t, T->bg);
  snprintf(b, sizeof(b), "%.2f km", tripDistanceM / 1000.0); drawField(fldStatVal[1], b, T->bg);
  snprintf(b, sizeof(b), "%.1f km/h", avgSpeedKmh()); drawField(fldStatVal[2], b, T->bg);
  snprintf(b, sizeof(b), "%.1f km/h", maxSpeedKmh); drawField(fldStatVal[3], b, T->bg);
  snprintf(b, sizeof(b), "%.0f m", ascentM); drawField(fldStatVal[4], b, T->bg);
  if (fx.altValid) snprintf(b, sizeof(b), "%.0f m", fx.alt); else snprintf(b, sizeof(b), "--");
  drawField(fldStatVal[5], b, T->bg);
  if (gradeValid) snprintf(b, sizeof(b), "%+.1f %%", gradePct); else snprintf(b, sizeof(b), "--");
  drawField(fldStatVal[6], b, T->bg);
  char hr[8], cad[8];
  if (hrFresh()) snprintf(hr, sizeof(hr), "%d", (int)hrBpm); else snprintf(hr, sizeof(hr), "--");
  if (cadFresh()) snprintf(cad, sizeof(cad), "%d", (int)cadRpm); else snprintf(cad, sizeof(cad), "--");
  snprintf(b, sizeof(b), "%s / %s", hr, cad);
  drawField(fldStatVal[7], b, T->bg);
  snprintf(b, sizeof(b), "%.1f", odometerM / 1000.0);
  drawField(fldStatOdo, b, T->panel);
}

// ============================================================================
// SYSTEM (GPS, baterie, BLE, senzory)
// ============================================================================
const char* SYS_LABELS[SYS_ROWS] = {"GPS", "SIRKA", "DELKA", "VYSKA", "HDOP", "BATERIE",
                                    "TELEFON", "SENZOR TEPU", "SENZOR KAD.", "TEMA", "FIRMWARE"};

void layoutSys() {
  tft.fillRect(0, 23, 240, 297, T->bg);
  for (int i = 0; i < SYS_ROWS; i++) {
    int16_t y = 46 + i * 25;
    drawStaticText(SYS_LABELS[i], 8, y, &FreeSans9pt7b, T->muted, 0);
    initField(fldSysVal[i], 232, y, &FreeSansBold9pt7b, T->text, 2);
  }
}

void updateSys() {
  char b[28];
  if (simEnabled) snprintf(b, sizeof(b), "SIMULACE");
  else snprintf(b, sizeof(b), "%s %d sat", fx.valid ? "FIX" : "hledam", fx.sats);
  fldSysVal[0].color = fx.valid ? T->ok : T->warn;
  drawField(fldSysVal[0], b, T->bg);
  if (fx.valid) {
    snprintf(b, sizeof(b), "%.6f", fx.lat); drawField(fldSysVal[1], b, T->bg);
    snprintf(b, sizeof(b), "%.6f", fx.lng); drawField(fldSysVal[2], b, T->bg);
  } else {
    drawField(fldSysVal[1], "---", T->bg);
    drawField(fldSysVal[2], "---", T->bg);
  }
  if (fx.altValid) snprintf(b, sizeof(b), "%.0f m", fx.alt); else snprintf(b, sizeof(b), "--");
  drawField(fldSysVal[3], b, T->bg);
  if (fx.hdop < 50) snprintf(b, sizeof(b), "%.1f", fx.hdop); else snprintf(b, sizeof(b), "--");
  drawField(fldSysVal[4], b, T->bg);
  if (BAT_ADC_PIN < 0) snprintf(b, sizeof(b), "nemeri se");
  else if (usbPower) snprintf(b, sizeof(b), "USB %.2f V", batMv / 1000.0f);
  else snprintf(b, sizeof(b), "%d%% %.2f V", batPct, batMv / 1000.0f);
  drawField(fldSysVal[5], b, T->bg);
  fldSysVal[6].color = bleConnected ? T->ble : T->muted;
  drawField(fldSysVal[6], bleConnected ? "pripojen" : "odpojen", T->bg);
  if (!sensorsEnabled) snprintf(b, sizeof(b), "vypnuto");
  else if (hrConnected) snprintf(b, sizeof(b), "%d bpm", hrFresh() ? (int)hrBpm : 0);
  else snprintf(b, sizeof(b), "hledam...");
  drawField(fldSysVal[7], b, T->bg);
  if (!sensorsEnabled) snprintf(b, sizeof(b), "vypnuto");
  else if (cscConnected) snprintf(b, sizeof(b), "%d rpm", cadFresh() ? (int)cadRpm : 0);
  else snprintf(b, sizeof(b), "hledam...");
  drawField(fldSysVal[8], b, T->bg);
  drawField(fldSysVal[9], T->name, T->bg);
  snprintf(b, sizeof(b), "%s %s", FW_VERSION, BOARD_PROFILE == BOARD_CYKLOPCB_V1 ? "PCB" : "BB");
  drawField(fldSysVal[10], b, T->bg);
}

// ============================================================================
// MAPA
// ============================================================================
double niceGridStepM(double desiredM) {
  static const double steps[] = {50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000};
  for (double s : steps) if (s >= desiredM) return s;
  return 10000;
}

void llToScreen(double lat, double lng, int16_t &sx, int16_t &sy) {
  double dLatM = (lat - mapT.centerLat) * 111320.0;
  double dLngM = (lng - mapT.centerLng) * 111320.0 * mapT.lngScale;
  sx = mapT.originX + (int16_t)lround(dLngM / mapT.metersPerPixel);
  sy = mapT.originY - (int16_t)lround(dLatM / mapT.metersPerPixel);
}

bool computeOverviewTransform() {
  double minLat = 1000, maxLat = -1000, minLng = 1000, maxLng = -1000;
  bool any = false;
  for (int i = 0; i < routePointCount; i++) {
    minLat = min(minLat, (double)routePoints[i].lat); maxLat = max(maxLat, (double)routePoints[i].lat);
    minLng = min(minLng, (double)routePoints[i].lng); maxLng = max(maxLng, (double)routePoints[i].lng);
    any = true;
  }
  for (int i = 0; i < breadcrumbCount; i++) {
    int idx = (breadcrumbHead + i) % BREADCRUMB_SIZE;
    minLat = min(minLat, (double)breadcrumb[idx].lat); maxLat = max(maxLat, (double)breadcrumb[idx].lat);
    minLng = min(minLng, (double)breadcrumb[idx].lng); maxLng = max(maxLng, (double)breadcrumb[idx].lng);
    any = true;
  }
  if (fx.valid) {
    minLat = min(minLat, fx.lat); maxLat = max(maxLat, fx.lat);
    minLng = min(minLng, fx.lng); maxLng = max(maxLng, fx.lng);
    any = true;
  }
  if (!any) return false;

  double centerLat = (minLat + maxLat) / 2.0;
  double centerLng = (minLng + maxLng) / 2.0;
  double lngScale = cos(centerLat * DEG_TO_RAD);
  double spanLatM = max((maxLat - minLat) * 111320.0 * 1.2, 300.0);
  double spanLngM = max((maxLng - minLng) * 111320.0 * lngScale * 1.2, 300.0);
  double mpp = max(spanLatM / mapAreaH, spanLngM / mapAreaW);

  mapT.centerLat = centerLat; mapT.centerLng = centerLng;
  mapT.lngScale = lngScale; mapT.metersPerPixel = mpp;
  mapT.originX = mapAreaX + mapAreaW / 2;
  mapT.originY = mapAreaY + mapAreaH / 2;
  return true;
}

void computeTrackingTransform(double lat, double lng) {
  const double WINDOW_M = 600.0;
  mapT.centerLat = lat; mapT.centerLng = lng;
  mapT.lngScale = cos(lat * DEG_TO_RAD);
  mapT.metersPerPixel = WINDOW_M / min(mapAreaW, mapAreaH);
  mapT.originX = mapAreaX + mapAreaW / 2;
  mapT.originY = mapAreaY + mapAreaH / 2 + 30; // jedeme "nahoru", vic vidime dopredu
}

// Cohen-Sutherland - cara oriznuta na plochu mapy (ne jen "pricvaknuta" na okraj)
uint8_t outCode(int32_t x, int32_t y) {
  uint8_t c = 0;
  if (x < mapAreaX) c |= 1; else if (x > mapAreaX + mapAreaW - 1) c |= 2;
  if (y < mapAreaY) c |= 4; else if (y > mapAreaY + mapAreaH - 1) c |= 8;
  return c;
}

void mapLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t col) {
  uint8_t c0 = outCode(x0, y0), c1 = outCode(x1, y1);
  for (int iter = 0; iter < 8; iter++) {
    if (!(c0 | c1)) { tft.drawLine(x0, y0, x1, y1, col); return; }
    if (c0 & c1) return;
    uint8_t co = c0 ? c0 : c1;
    int32_t x = 0, y = 0;
    int32_t xmin = mapAreaX, xmax = mapAreaX + mapAreaW - 1, ymin = mapAreaY, ymax = mapAreaY + mapAreaH - 1;
    if (co & 8) { x = x0 + (int64_t)(x1 - x0) * (ymax - y0) / (y1 - y0); y = ymax; }
    else if (co & 4) { x = x0 + (int64_t)(x1 - x0) * (ymin - y0) / (y1 - y0); y = ymin; }
    else if (co & 2) { y = y0 + (int64_t)(y1 - y0) * (xmax - x0) / (x1 - x0); x = xmax; }
    else { y = y0 + (int64_t)(y1 - y0) * (xmin - x0) / (x1 - x0); x = xmin; }
    if (co == c0) { x0 = x; y0 = y; c0 = outCode(x0, y0); }
    else { x1 = x; y1 = y; c1 = outCode(x1, y1); }
  }
}

void drawMapGridScaleNorth() {
  double gridM = niceGridStepM(mapT.metersPerPixel * 60.0);
  double gridPx = gridM / mapT.metersPerPixel;
  for (double x = mapT.originX; x < mapAreaX + mapAreaW; x += gridPx) tft.drawFastVLine((int16_t)x, mapAreaY, mapAreaH, T->mapGrid);
  for (double x = mapT.originX - gridPx; x > mapAreaX; x -= gridPx) tft.drawFastVLine((int16_t)x, mapAreaY, mapAreaH, T->mapGrid);
  for (double y = mapT.originY; y < mapAreaY + mapAreaH; y += gridPx) tft.drawFastHLine(mapAreaX, (int16_t)y, mapAreaW, T->mapGrid);
  for (double y = mapT.originY - gridPx; y > mapAreaY; y -= gridPx) tft.drawFastHLine(mapAreaX, (int16_t)y, mapAreaW, T->mapGrid);
}

void drawMapScaleNorth() {
  double gridM = niceGridStepM(mapT.metersPerPixel * 60.0);
  double gridPx = gridM / mapT.metersPerPixel;
  int16_t sy = mapAreaY + mapAreaH - 6;
  int16_t sx0 = mapAreaX + 6;
  tft.fillRect(sx0, sy - 1, (int16_t)gridPx, 3, T->text);
  char lbl[16];
  if (gridM >= 1000) snprintf(lbl, sizeof(lbl), "%.0f km", gridM / 1000.0);
  else snprintf(lbl, sizeof(lbl), "%.0f m", gridM);
  drawStaticText(lbl, sx0, sy - 6, &FreeSans9pt7b, T->text, 0);
  int16_t nx = mapAreaX + mapAreaW - 14, ny = mapAreaY + 18;
  tft.fillTriangle(nx, ny - 12, nx - 6, ny, nx + 6, ny, T->text);
  drawStaticText("N", nx, ny + 15, &FreeSansBold9pt7b, T->text, 1);
}

void drawStreetsLayer() {
  int16_t px = 0, py = 0;
  bool have = false;
  for (int i = 0; i < streetPointCount; i++) {
    if (streetPoints[i].lat > 90.0f) { have = false; continue; }
    int16_t sx, sy;
    llToScreen(streetPoints[i].lat, streetPoints[i].lng, sx, sy);
    if (have) mapLine(px, py, sx, sy, T->mapStreet);
    px = sx; py = sy; have = true;
  }
}

void drawRouteLayer() {
  if (routePointCount < 1) return;
  for (int pass = 0; pass < 2; pass++) {
    int16_t px = 0, py = 0;
    for (int i = 0; i < routePointCount; i++) {
      int16_t sx, sy;
      llToScreen(routePoints[i].lat, routePoints[i].lng, sx, sy);
      if (i > 0) {
        if (pass == 0) {
          for (int o = -2; o <= 2; o++) { mapLine(px + o, py, sx + o, sy, T->routeEdge); mapLine(px, py + o, sx, sy + o, T->routeEdge); }
        } else {
          for (int o = -1; o <= 1; o++) { mapLine(px + o, py, sx + o, sy, T->route); mapLine(px, py + o, sx, sy + o, T->route); }
        }
      }
      px = sx; py = sy;
    }
  }
  int16_t sx, sy;
  llToScreen(routePoints[0].lat, routePoints[0].lng, sx, sy);
  if (!outCode(sx, sy)) { tft.drawCircle(sx, sy, 6, T->ok); tft.drawCircle(sx, sy, 5, T->ok); }
  llToScreen(routePoints[routePointCount - 1].lat, routePoints[routePointCount - 1].lng, sx, sy);
  if (!outCode(sx - 5, sy - 5) && !outCode(sx + 5, sy + 5)) {
    // sachovnicova cilova vlajka
    tft.fillRect(sx - 5, sy - 5, 5, 5, T->text); tft.fillRect(sx, sy, 5, 5, T->text);
    tft.fillRect(sx, sy - 5, 5, 5, T->posFill); tft.fillRect(sx - 5, sy, 5, 5, T->posFill);
  }
}

void drawBreadcrumbLayerFull() {
  int16_t px = 0, py = 0;
  bool have = false;
  for (int i = 0; i < breadcrumbCount; i++) {
    int idx = (breadcrumbHead + i) % BREADCRUMB_SIZE;
    int16_t sx, sy;
    llToScreen(breadcrumb[idx].lat, breadcrumb[idx].lng, sx, sy);
    if (have) { mapLine(px, py, sx, sy, T->track); mapLine(px + 1, py, sx + 1, sy, T->track); }
    px = sx; py = sy; have = true;
  }
  lastDrawnBreadcrumbCount = breadcrumbCount;
}

void drawPositionArrow(double lat, double lng) {
  int16_t cx, cy;
  llToScreen(lat, lng, cx, cy);
  if (outCode(cx - 12, cy - 12) || outCode(cx + 12, cy + 12)) return;
  tft.fillCircle(cx, cy, 12, T->posArrow);
  tft.fillCircle(cx, cy, 10, T->posFill);
  float rad = fx.course * DEG_TO_RAD;
  float tipX = cx + sinf(rad) * 9, tipY = cy - cosf(rad) * 9;
  float b1x = cx + sinf(rad + 2.5f) * 7, b1y = cy - cosf(rad + 2.5f) * 7;
  float b2x = cx + sinf(rad - 2.5f) * 7, b2y = cy - cosf(rad - 2.5f) * 7;
  tft.fillTriangle((int16_t)tipX, (int16_t)tipY, (int16_t)b1x, (int16_t)b1y, cx, cy, T->posArrow);
  tft.fillTriangle((int16_t)tipX, (int16_t)tipY, (int16_t)b2x, (int16_t)b2y, cx, cy, T->posArrow);
}

double distanceToDestinationAlongRoute() {
  if (!routeValid || routePointCount < 2 || !fx.valid) return -1;
  double best = 1e18; int bestIdx = 0;
  for (int i = 0; i < routePointCount; i++) {
    double d = TinyGPSPlus::distanceBetween(fx.lat, fx.lng, routePoints[i].lat, routePoints[i].lng);
    if (d < best) { best = d; bestIdx = i; }
  }
  double remain = routePoints[routePointCount - 1].cumDistM - routePoints[bestIdx].cumDistM;
  if (remain < 0) remain = 0;
  return (remain + best) / 1000.0;
}

bool mapReceivingActive() { return receivingRoute || receivingStreets; }

void drawMapMessage(const char* msg) {
  if (!fldMapMsg.valid || strcmp(fldMapMsg.text, msg) != 0) {
    tft.fillRect(mapAreaX, mapAreaY, mapAreaW, mapAreaH, T->bg);
    fldMapMsg.valid = false;
  }
  fldMapMsg.color = T->warn;
  drawField(fldMapMsg, msg, T->bg);
  mapStaticDrawn = false;
}

void layoutMap() {
  tft.fillRect(0, 23, 240, 297, T->bg);
  drawPanel(2, 26, 236, 60);
  segNumberInit(mapSpeed, 10, 32, 21, 38, 5, 15, 26, 3, 4);
  drawStaticText("km/h", 88, 70, &FreeSans9pt7b, T->muted, 0);
  drawStaticText("DO CILE", 134, 46, &FreeSans9pt7b, T->muted, 0);
  initField(fldDestVal, 134, 74, &FreeSansBold12pt7b, T->text, 0);
  mapBarLit = -1;
  mapStaticDrawn = false;
  haveLastMapDraw = false;
  invalidateField(fldMapMsg);
}

void updateMapHeader() {
  bool fix = fx.valid;
  float v = fix ? filteredSpeedKmh : 0.0f;
  segNumberDraw(mapSpeed, v, fix ? T->text : T->muted, T->segOff);

  // "speed bar" jako na Game Boyi: 10 policek
  int lit = (int)lroundf(v / gaugeMaxKmh * 10.0f);
  if (lit > 10) lit = 10;
  if (lit != mapBarLit) {
    for (int i = 0; i < 10; i++) {
      bool on = i < lit;
      bool was = i < mapBarLit;
      if (mapBarLit < 0 || on != was) tft.fillRect(10 + i * 11, 76, 9, 5, on ? T->accent : T->segOff);
    }
    mapBarLit = lit;
  }

  double destKm = distanceToDestinationAlongRoute();
  char buf[20];
  if (destKm >= 0) snprintf(buf, sizeof(buf), "%.1f km", destKm);
  else snprintf(buf, sizeof(buf), "--");
  drawField(fldDestVal, buf, T->panel);
}

void redrawMapFull(bool tracking) {
  tft.fillRect(mapAreaX, mapAreaY, mapAreaW, mapAreaH, T->mapBg);
  drawMapGridScaleNorth();
  drawStreetsLayer();
  drawRouteLayer();
  drawBreadcrumbLayerFull();
  if (fx.valid) drawPositionArrow(fx.lat, fx.lng);
  drawMapScaleNorth();
  (void)tracking;
  invalidateField(fldMapMsg);
}

void updateMap() {
  updateMapHeader();
  if (mapReceivingActive()) { drawMapMessage("Prijimam mapu..."); return; }

  unsigned long now = millis();
  bool trackingMode = recording && !manualPaused && fx.valid;

  if (trackingMode) {
    bool shouldRedraw = !mapStaticDrawn || !wasTrackingMode || !haveLastMapDraw;
    if (!shouldRedraw) {
      double moved = TinyGPSPlus::distanceBetween(lastMapDrawLat, lastMapDrawLng, fx.lat, fx.lng);
      // gating: prekresluj jen pri skutecnem pohybu - jinak by mapa blikala
      shouldRedraw = moved > 40.0 && filteredSpeedKmh > 3.0 && (now - lastMapDrawMs) > 3000;
    }
    if (shouldRedraw) {
      computeTrackingTransform(fx.lat, fx.lng);
      redrawMapFull(true);
      lastMapDrawMs = now; lastMapDrawLat = fx.lat; lastMapDrawLng = fx.lng;
      haveLastMapDraw = true; mapStaticDrawn = true;
    }
  } else {
    bool moved = fx.valid && haveLastMapDraw &&
                 TinyGPSPlus::distanceBetween(lastMapDrawLat, lastMapDrawLng, fx.lat, fx.lng) > 30.0 &&
                 (now - lastMapDrawMs) > 15000;
    if (!mapStaticDrawn || wasTrackingMode || moved) {
      if (computeOverviewTransform()) {
        redrawMapFull(false);
        mapStaticDrawn = true;
        lastMapDrawMs = now;
        if (fx.valid) { lastMapDrawLat = fx.lat; lastMapDrawLng = fx.lng; haveLastMapDraw = true; }
      } else {
        drawMapMessage("Zadna trasa ani poloha");
        wasTrackingMode = trackingMode;
        return;
      }
    } else if (breadcrumbCount != lastDrawnBreadcrumbCount && breadcrumbCount >= 2) {
      // inkrementalni dokresleni posledniho useku stopy
      int idxPrev = (breadcrumbHead + breadcrumbCount - 2) % BREADCRUMB_SIZE;
      int idxNew = (breadcrumbHead + breadcrumbCount - 1) % BREADCRUMB_SIZE;
      int16_t px, py, sx, sy;
      llToScreen(breadcrumb[idxPrev].lat, breadcrumb[idxPrev].lng, px, py);
      llToScreen(breadcrumb[idxNew].lat, breadcrumb[idxNew].lng, sx, sy);
      mapLine(px, py, sx, sy, T->track);
      lastDrawnBreadcrumbCount = breadcrumbCount;
    }
  }
  wasTrackingMode = trackingMode;
}

// ============================================================================
// PREPINANI OBRAZOVEK
// ============================================================================
void renderScreens() {
  if (overlayActive) {
    if ((long)(millis() - overlayUntil) > 0) { overlayActive = false; forceFullRedraw(); }
    else { updateStatusBar(); return; }
  }
  if (currentScreen != lastDrawnScreen) {
    switch (currentScreen) {
      case SCR_DASH: layoutDash(); break;
      case SCR_STATS: layoutStats(); break;
      case SCR_MAP: layoutMap(); break;
      case SCR_SYS: layoutSys(); break;
    }
    lastDrawnScreen = currentScreen;
  }
  switch (currentScreen) {
    case SCR_DASH: updateDash(); break;
    case SCR_STATS: updateStats(); break;
    case SCR_MAP: updateMap(); break;
    case SCR_SYS: updateSys(); break;
  }
  updateStatusBar();
}

void drawSplash() {
  tft.fillScreen(T->bg);
  // kolo z primitiv
  int cy = 130;
  tft.drawCircle(80, cy, 26, T->accent); tft.drawCircle(80, cy, 25, T->accent);
  tft.drawCircle(160, cy, 26, T->accent); tft.drawCircle(160, cy, 25, T->accent);
  tft.drawLine(80, cy, 112, cy - 36, T->text); tft.drawLine(112, cy - 36, 150, cy - 36, T->text);
  tft.drawLine(150, cy - 36, 160, cy, T->text); tft.drawLine(80, cy, 120, cy, T->text);
  tft.drawLine(120, cy, 150, cy - 36, T->text); tft.drawLine(120, cy, 108, cy - 44, T->text);
  tft.drawLine(100, cy - 44, 116, cy - 44, T->text);
  tft.drawLine(150, cy - 36, 146, cy - 50, T->text); tft.drawLine(140, cy - 50, 154, cy - 50, T->text);
  drawStaticText("CykloComp", 120, 210, &FreeSansBold18pt7b, T->text, 1);
  drawStaticText("v" FW_VERSION "  " BOARD_NAME, 120, 238, &FreeSans9pt7b, T->muted, 1);
}

// ============================================================================
// PRIKAZY (BLE + USB) - zpracovani ve smycce, ne v BLE callbacku
// ============================================================================
void setBulk(const char* json) {
  if (bulkChar) bulkChar->setValue((const uint8_t*)json, strlen(json));
  Serial.print("BULK:");
  Serial.println(json);
}

void finalizeRoute() {
  double cum = 0;
  if (routePointCount > 0) routePoints[0].cumDistM = 0;
  for (int i = 1; i < routePointCount; i++) {
    cum += TinyGPSPlus::distanceBetween(routePoints[i - 1].lat, routePoints[i - 1].lng, routePoints[i].lat, routePoints[i].lng);
    routePoints[i].cumDistM = cum;
  }
  routeValid = routePointCount > 1;
  mapStaticDrawn = false;
  simIdx = 0;
}

int parsePointsChunk(const char* data, GeoPoint* geo, RoutePoint* rp, int count, int maxCount) {
  // format: lat,lng;lat,lng;...
  const char* p = data;
  while (*p && count < maxCount) {
    char* comma;
    float lat = strtof(p, &comma);
    if (comma == p || *comma != ',') break;
    char* semi;
    float lng = strtof(comma + 1, &semi);
    if (semi == comma + 1) break;
    if (geo) { geo[count].lat = lat; geo[count].lng = lng; }
    if (rp) { rp[count].lat = lat; rp[count].lng = lng; rp[count].cumDistM = 0; }
    count++;
    if (*semi == ';') p = semi + 1; else break;
  }
  return count;
}

void saveFieldConfigToNvs() {
  for (int i = 0; i < 4; i++) {
    char k[4] = {'f', (char)('0' + i), 0, 0};
    prefs.putUChar(k, fieldConfig[i]);
  }
}

void sendStatusNow();

void handleSync(const char* arg) {
  char json[512];
  if (!fsOk) { setBulk("{\"req\":\"ERR\",\"err\":\"nofs\"}"); return; }
  if (!strcmp(arg, "LIST")) {
    uint16_t ids[64];
    int n = listRideIds(ids, 64, true);
    int len = snprintf(json, sizeof(json), "{\"req\":\"LIST\",\"cur\":%u,\"ids\":[", (unsigned)curRideId);
    for (int i = 0; i < n && len < (int)sizeof(json) - 12; i++) len += snprintf(json + len, sizeof(json) - len, i ? ",%u" : "%u", (unsigned)ids[i]);
    snprintf(json + len, sizeof(json) - len, "]}");
    setBulk(json);
  } else if (!strncmp(arg, "SUM:", 4)) {
    uint16_t id = atoi(arg + 4);
    char path[32];
    ridePath(id, "sum", path, sizeof(path));
    File f = LittleFS.open(path, "r");
    if (!f) { snprintf(json, sizeof(json), "{\"req\":\"SUM:%u\",\"err\":\"notfound\"}", (unsigned)id); setBulk(json); return; }
    char sum[220];
    int n = f.readBytes(sum, sizeof(sum) - 1);
    f.close();
    sum[n > 0 ? n : 0] = 0;
    snprintf(json, sizeof(json), "{\"req\":\"SUM:%u\",%s", (unsigned)id, sum[0] == '{' ? sum + 1 : "\"err\":\"bad\"}");
    setBulk(json);
  } else if (!strncmp(arg, "TRK:", 4)) {
    unsigned id = 0; int pg = 0;
    if (sscanf(arg + 4, "%u:%d", &id, &pg) != 2) return;
    char path[32];
    ridePath(id, "trk", path, sizeof(path));
    File f = LittleFS.open(path, "r");
    if (!f) { snprintf(json, sizeof(json), "{\"req\":\"TRK:%u:%d\",\"err\":\"notfound\"}", id, pg); setBulk(json); return; }
    int total = f.size() / 8;
    int start = pg * TRK_PAGE_POINTS;
    int len = snprintf(json, sizeof(json), "{\"req\":\"TRK:%u:%d\",\"n\":%d,\"more\":%d,\"p\":\"", id, pg, total,
                       (start + TRK_PAGE_POINTS < total) ? 1 : 0);
    if (start < total) {
      f.seek(start * 8);
      int32_t pair[2];
      for (int i = 0; i < TRK_PAGE_POINTS && start + i < total; i++) {
        if (f.read((uint8_t*)pair, 8) != 8) break;
        len += snprintf(json + len, sizeof(json) - len, "%.5f,%.5f;", pair[0] / 1e6, pair[1] / 1e6);
      }
    }
    f.close();
    snprintf(json + len, sizeof(json) - len, "\"}");
    setBulk(json);
  } else if (!strncmp(arg, "DEL:", 4)) {
    uint16_t id = atoi(arg + 4);
    if (id && id != curRideId) rideDelete(id);
    snprintf(json, sizeof(json), "{\"req\":\"DEL:%u\",\"ok\":1}", (unsigned)id);
    setBulk(json);
  }
}

void handleCommand(const char* cmd) {
  lastActivityMs = millis();
  if (!strcmp(cmd, "RIDE:START")) { startRide(); }
  else if (!strcmp(cmd, "RIDE:STOP")) { stopRide(); }
  else if (!strcmp(cmd, "RIDE:PAUSE")) { pauseRide(); }
  else if (!strcmp(cmd, "RIDE:RESUME")) { resumeRide(); }
  else if (!strcmp(cmd, "RESET_TRIP")) { resetTrip(); }
  else if (!strncmp(cmd, "SET_TZ:", 7)) {
    timezoneOffsetHours = atoi(cmd + 7);
    prefs.putInt("tz", timezoneOffsetHours);
  }
  else if (!strncmp(cmd, "TIME:", 5)) {
    phoneEpochBase = strtoul(cmd + 5, nullptr, 10);
    phoneEpochMs = millis();
    if (recording && rideStartEpoch == 0) rideStartEpoch = phoneEpochBase - rideTimeMs / 1000;
  }
  else if (!strncmp(cmd, "SCREEN:", 7)) {
    int n = atoi(cmd + 7);
    if (n >= 0 && n < NUM_SCREENS) currentScreen = (uint8_t)n;
  }
  else if (!strncmp(cmd, "CFG:FIELDS:", 11)) {
    int a, b, c, d;
    if (sscanf(cmd + 11, "%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
      int v[4] = {a, b, c, d};
      for (int i = 0; i < 4; i++) fieldConfig[i] = (v[i] >= 0 && v[i] < NUM_FIELDS) ? v[i] : 0;
      saveFieldConfigToNvs();
      if (currentScreen == SCR_DASH) lastDrawnScreen = 255;
    }
  }
  else if (!strncmp(cmd, "THEME:", 6)) {
    applyTheme((uint8_t)atoi(cmd + 6));
    prefs.putUChar("theme", themeIdx);
  }
  else if (!strncmp(cmd, "BL:", 3)) {
    int v = atoi(cmd + 3);
    brightnessPct = (uint8_t)constrain(v, 5, 100);
    prefs.putUChar("bl", brightnessPct);
  }
  else if (!strncmp(cmd, "AP:", 3)) { autoPauseEnabled = atoi(cmd + 3) != 0; prefs.putBool("ap", autoPauseEnabled); }
  else if (!strncmp(cmd, "SENS:", 5)) { sensorsEnabled = atoi(cmd + 5) != 0; prefs.putBool("sens", sensorsEnabled); }
  else if (!strncmp(cmd, "AUTOOFF:", 8)) { autoOffMin = (uint8_t)constrain(atoi(cmd + 8), 0, 120); prefs.putUChar("aoff", autoOffMin); }
  else if (!strncmp(cmd, "GAUGE:", 6)) {
    gaugeMaxKmh = constrain(atoi(cmd + 6), 20, 99);
    prefs.putFloat("gmax", gaugeMaxKmh);
    if (currentScreen == SCR_DASH) lastDrawnScreen = 255;
  }
  else if (!strncmp(cmd, "ODO:", 4)) { odometerM = atof(cmd + 4) * 1000.0; prefs.putFloat("odoM", odometerM); }
  else if (!strncmp(cmd, "SIM:", 4)) {
    simEnabled = atoi(cmd + 4) != 0;
    simLastMs = 0;
    if (simEnabled) {
      if (gps.location.isValid()) { simLat = gps.location.lat(); simLng = gps.location.lng(); }
      else if (routeValid) { simLat = routePoints[0].lat; simLng = routePoints[0].lng; simIdx = 1; }
    }
    haveRefPoint = false;
  }
  else if (!strcmp(cmd, "PWROFF")) { powerOff("Prikaz z aplikace"); }
  else if (!strcmp(cmd, "INFO")) { sendStatusNow(); }
  else if (!strncmp(cmd, "SYNC:", 5)) { handleSync(cmd + 5); }
  else if (!strcmp(cmd, "RT:BEGIN")) { receivingRoute = true; routePointCount = 0; routeValid = false; }
  else if (!strncmp(cmd, "RT:P:", 5)) { routePointCount = parsePointsChunk(cmd + 5, nullptr, routePoints, routePointCount, MAX_ROUTE_POINTS); }
  else if (!strcmp(cmd, "RT:END")) { receivingRoute = false; finalizeRoute(); }
  else if (!strcmp(cmd, "RT:CLEAR")) { routePointCount = 0; routeValid = false; mapStaticDrawn = false; }
  else if (!strcmp(cmd, "MP:BEGIN")) { receivingStreets = true; streetPointCount = 0; }
  else if (!strncmp(cmd, "MP:P:", 5)) { streetPointCount = parsePointsChunk(cmd + 5, streetPoints, nullptr, streetPointCount, MAX_STREET_POINTS); }
  else if (!strcmp(cmd, "MP:END")) { receivingStreets = false; mapStaticDrawn = false; }
}

void processQueuedCommands() {
  CmdMsg m;
  int n = 0;
  while (n < 64 && xQueueReceive(cmdQueue, &m, 0) == pdTRUE) {
    handleCommand(m.s);
    n++;
  }
}

// Cteni stejneho textoveho protokolu z USB (Serial Monitor / PC skript).
void pollSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialCmdLen > 0) {
        serialCmdBuf[serialCmdLen] = 0;
        handleCommand(serialCmdBuf);
        serialCmdLen = 0;
      }
    } else if (serialCmdLen < CMD_MAX_LEN - 1) {
      serialCmdBuf[serialCmdLen++] = c;
    }
  }
}

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    NimBLEAttValue v = pChar->getValue();
    CmdMsg m;
    size_t len = v.length();
    if (len >= CMD_MAX_LEN) len = CMD_MAX_LEN - 1;
    memcpy(m.s, v.data(), len);
    m.s[len] = 0;
    // odriznout pripadny \n z terminalovych BLE aplikaci
    while (len > 0 && (m.s[len - 1] == '\n' || m.s[len - 1] == '\r')) m.s[--len] = 0;
    if (len) xQueueSend(cmdQueue, &m, pdMS_TO_TICKS(30));
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
    (void)srv; (void)connInfo;
    bleConnected = true;
    lastActivityMs = millis();
  }
  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    (void)connInfo; (void)reason;
    bleConnected = srv->getConnectedCount() > 0;
    NimBLEDevice::startAdvertising();
  }
};

// ============================================================================
// BLE SENZORY (tep 0x180D, kadence/rychlost 0x1816) - vlastni FreeRTOS task,
// aby pripojovani (blokujici) nezamrazilo displej
// ============================================================================
NimBLEClient* hrClient = nullptr;
NimBLEClient* cscClient = nullptr;

void onHrNotify(NimBLERemoteCharacteristic* c, uint8_t* d, size_t len, bool isNotify) {
  (void)c; (void)isNotify;
  if (len < 2) return;
  int bpm = (d[0] & 0x01) ? (d[1] | (d[2] << 8)) : d[1];
  if (bpm > 0 && bpm < 250) { hrBpm = bpm; hrLastMs = millis(); }
}

void onCscNotify(NimBLERemoteCharacteristic* c, uint8_t* d, size_t len, bool isNotify) {
  (void)c; (void)isNotify;
  if (len < 1) return;
  uint8_t flags = d[0];
  size_t i = 1;
  if (flags & 0x01) i += 6;               // wheel revs (u32) + event time (u16)
  if ((flags & 0x02) && len >= i + 4) {   // crank revs (u16) + event time (u16, 1/1024 s)
    uint16_t revs = d[i] | (d[i + 1] << 8);
    uint16_t tm = d[i + 2] | (d[i + 3] << 8);
    if (cscHavePrev) {
      uint16_t dRev = revs - cscPrevRevs;
      uint16_t dT = tm - cscPrevTime;
      if (dRev > 0 && dRev < 20 && dT > 0) {
        cadRpm = (int)((uint32_t)dRev * 60UL * 1024UL / dT);
        cadLastMs = millis();
      } else if (dRev == 0 && millis() - cadLastMs > 2500) {
        cadRpm = 0;
      }
    }
    cscPrevRevs = revs; cscPrevTime = tm; cscHavePrev = true;
  }
}

NimBLEClient* connectSensor(const NimBLEAdvertisedDevice* dev, uint16_t svcUuid, uint16_t chrUuid,
                            NimBLERemoteCharacteristic::notify_callback cb) {
  NimBLEClient* c = NimBLEDevice::getDisconnectedClient();
  if (!c) c = NimBLEDevice::createClient();
  if (!c) return nullptr;
  c->setConnectTimeout(5000);
  if (!c->connect(dev)) return nullptr;
  NimBLERemoteService* s = c->getService(NimBLEUUID(svcUuid));
  NimBLERemoteCharacteristic* ch = s ? s->getCharacteristic(NimBLEUUID(chrUuid)) : nullptr;
  if (!ch || !ch->canNotify() || !ch->subscribe(true, cb)) {
    c->disconnect();
    return nullptr;
  }
  return c;
}

void sensorTask(void* arg) {
  (void)arg;
  const NimBLEUUID HR_SVC((uint16_t)0x180D), CSC_SVC((uint16_t)0x1816);
  int failedScans = 0;
  for (;;) {
    hrConnected = hrClient && hrClient->isConnected();
    cscConnected = cscClient && cscClient->isConnected();

    if (!sensorsEnabled) {
      if (hrConnected) hrClient->disconnect();
      if (cscConnected) cscClient->disconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (hrConnected && cscConnected) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(false);
    NimBLEScanResults res = scan->getResults(4000, false);
    bool found = false;
    for (int i = 0; i < res.getCount(); i++) {
      const NimBLEAdvertisedDevice* dev = res.getDevice(i);
      if (!hrConnected && dev->isAdvertisingService(HR_SVC)) {
        NimBLEClient* c = connectSensor(dev, 0x180D, 0x2A37, onHrNotify);
        if (c) { hrClient = c; hrConnected = true; found = true; }
      } else if (!cscConnected && dev->isAdvertisingService(CSC_SVC)) {
        cscHavePrev = false;
        NimBLEClient* c = connectSensor(dev, 0x1816, 0x2A5B, onCscNotify);
        if (c) { cscClient = c; cscConnected = true; found = true; }
      }
    }
    scan->clearResults();
    failedScans = found ? 0 : failedScans + 1;
    // nic nenalezeno -> skenuj stale mene casto (setri baterii)
    uint32_t waitMs = failedScans < 3 ? 15000 : 60000;
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
}

// ============================================================================
// BLE SERVER + TELEMETRIE
// ============================================================================
void initBLE() {
  NimBLEDevice::init("CykloComp");
  NimBLEDevice::setMTU(185);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = pServer->createService(SERVICE_UUID);
  telemetryChar = svc->createCharacteristic(TELEMETRY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  commandChar = svc->createCharacteristic(COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 512);
  statusChar = svc->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  bulkChar = svc->createCharacteristic(BULK_UUID, NIMBLE_PROPERTY::READ, 512);
  commandChar->setCallbacks(new CommandCallbacks());
  bulkChar->setValue("{}");

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  NimBLEAdvertisementData scanData;
  scanData.setName("CykloComp");
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->start();
}

// hlavni telemetrie - musi se vejit do 182 B (MTU 185) - drzime ~170 B
void buildTelemetryJson(char* buf, size_t bufSize) {
  bool fix = fx.valid;
  snprintf(buf, bufSize,
    "{\"fix\":%d,\"spd\":%.1f,\"lat\":%.6f,\"lng\":%.6f,\"alt\":%.1f,\"dst\":%.3f,\"tim\":%lu,\"sat\":%d,"
    "\"rid\":%d,\"pau\":%d,\"bat\":%d,\"asc\":%.0f,\"max\":%.1f,\"hr\":%d,\"cad\":%d}",
    fix ? 1 : 0,
    fix ? filteredSpeedKmh : 0.0f,
    fix ? fx.lat : 0.0,
    fix ? fx.lng : 0.0,
    fx.altValid ? fx.alt : 0.0f,
    tripDistanceM / 1000.0,
    rideTimeMs / 1000,
    fx.sats,
    recording ? 1 : 0,
    manualPaused ? 1 : 0,
    usbPower ? 101 : batPct,
    ascentM,
    maxSpeedKmh,
    hrFresh() ? (int)hrBpm : 0,
    cadFresh() ? (int)cadRpm : 0);
}

void buildStatusJson(char* buf, size_t bufSize) {
  snprintf(buf, bufSize,
    "{\"fw\":\"%s\",\"brd\":%d,\"odo\":%.1f,\"vb\":%d,\"chg\":%d,\"thm\":%d,\"bl\":%d,\"ap\":%d,\"sen\":%d,"
    "\"hrc\":%d,\"csc\":%d,\"grd\":%.1f,\"avg\":%.1f,\"hdp\":%.1f,\"crs\":%.0f,\"scr\":%d,\"tz\":%d,"
    "\"aoff\":%d,\"gmax\":%.0f,\"sim\":%d,\"apa\":%d,\"fs\":%d,\"rcur\":%u,\"f\":[%d,%d,%d,%d]}",
    FW_VERSION, BOARD_PROFILE, odometerM / 1000.0, (int)batMv, usbPower ? 1 : 0, themeIdx, brightnessPct,
    autoPauseEnabled ? 1 : 0, sensorsEnabled ? 1 : 0, hrConnected ? 1 : 0, cscConnected ? 1 : 0,
    gradeValid ? gradePct : 0.0f, avgSpeedKmh(), fx.hdop < 50 ? fx.hdop : 0.0f, fx.course, currentScreen,
    timezoneOffsetHours, autoOffMin, gaugeMaxKmh, simEnabled ? 1 : 0, autoPaused ? 1 : 0, fsOk ? 1 : 0,
    (unsigned)curRideId, fieldConfig[0], fieldConfig[1], fieldConfig[2], fieldConfig[3]);
}

void sendTelemetry() {
  if (mapReceivingActive()) return;
  char buf[256];
  buildTelemetryJson(buf, sizeof(buf));
  if (telemetryChar) {
    telemetryChar->setValue((uint8_t*)buf, strlen(buf));
    if (bleConnected) telemetryChar->notify();
  }
  Serial.println(buf); // vzdy - USB je nezavisly testovaci kanal na BLE/appce
}

void sendStatusNow() {
  char buf[400];
  buildStatusJson(buf, sizeof(buf));
  if (statusChar) {
    statusChar->setValue((uint8_t*)buf, strlen(buf));
    if (bleConnected) statusChar->notify();
  }
  Serial.print("STATUS:");
  Serial.println(buf);
  lastStatusMs = millis();
}

// ============================================================================
// NVS (Preferences)
// ============================================================================
void loadPreferences() {
  prefs.begin("cyklocomp", false);
  odometerM = prefs.getFloat("odoM", 0);
  tripDistanceM = prefs.getFloat("tripM", 0);
  rideTimeMs = prefs.getULong("timeMs", 0);
  maxSpeedKmh = prefs.getFloat("maxKmh", 0);
  ascentM = prefs.getFloat("ascM", 0);
  recording = prefs.getBool("rec", false);
  manualPaused = prefs.getBool("pau", false);
  timezoneOffsetHours = prefs.getInt("tz", 2);
  fieldConfig[0] = prefs.getUChar("f0", 0);
  fieldConfig[1] = prefs.getUChar("f1", 2);
  fieldConfig[2] = prefs.getUChar("f2", 3);
  fieldConfig[3] = prefs.getUChar("f3", 8);
  for (int i = 0; i < 4; i++) if (fieldConfig[i] >= NUM_FIELDS) fieldConfig[i] = 0;
  themeIdx = prefs.getUChar("theme", 0);
  if (themeIdx >= NUM_THEMES) themeIdx = 0;
  brightnessPct = prefs.getUChar("bl", 80);
  autoPauseEnabled = prefs.getBool("ap", true);
  sensorsEnabled = prefs.getBool("sens", false);
  autoOffMin = prefs.getUChar("aoff", 15);
  gaugeMaxKmh = prefs.getFloat("gmax", 60.0f);
  rideSeq = prefs.getUShort("rseq", 0);
  curRideId = prefs.getUShort("rid", 0);
  rideStartEpoch = prefs.getULong("rts", 0);
  T = &THEMES[themeIdx];
}

void savePreferencesPeriodic() {
  prefs.putFloat("odoM", odometerM);
  prefs.putFloat("tripM", tripDistanceM);
  prefs.putULong("timeMs", rideTimeMs);
  prefs.putFloat("maxKmh", maxSpeedKmh);
  prefs.putFloat("ascM", ascentM);
  prefs.putBool("rec", recording);
  prefs.putBool("pau", manualPaused);
  prefs.putULong("rts", rideStartEpoch);
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);
  gpsPower(true);

  loadPreferences();
  Serial.begin(USB_SERIAL_BAUD); // nativni USB-CDC - neceka se na terminal

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(0);
  tft.setTextWrap(false); // dlouhy text se nesmi zalamovat pres jine panely
  initBacklight();
  drawSplash();

  fsOk = LittleFS.begin(true);
  if (fsOk) LittleFS.mkdir("/rides");
  if (recording) rideRestore();   // vypadek napajeni behem jizdy - pokracujeme

  gpsSerial.setRxBufferSize(2048); // kresleni mapy trva - NMEA se nesmi ztratit
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  cmdQueue = xQueueCreate(CMD_QUEUE_LEN, sizeof(CmdMsg));
  initBLE();
  xTaskCreate(sensorTask, "sensors", 6144, nullptr, 1, nullptr);

  delay(900);
  forceFullRedraw();
  lastLoopMillis = millis();
  lastActivityMs = millis();
}

void loop() {
  readGpsSerial();
  pollSerialCommands();
  processQueuedCommands();
  updateFix();
  updateFilteredSpeed();
  updateDistanceAndStats();
  updateRideClock();
  updateBattery();
  handleButtons();

  unsigned long now = millis();
  if (now - lastRenderMs >= 100) {   // ~10 FPS staci a necha cas na GPS/BLE
    renderScreens();
    lastRenderMs = now;
  }
  updateBacklight();

  if (now - lastTelemetryMs >= 1000) {
    sendTelemetry();
    lastTelemetryMs = now;
    updatePowerManagement();
  }
  if (now - lastStatusMs >= 5000) sendStatusNow();
  if (now - lastNvsSaveMs >= 60000) {
    savePreferencesPeriodic();
    lastNvsSaveMs = now;
  }
  if (recording && now - lastRideSaveMs >= 300000UL) {
    rideSave(false);
    lastRideSaveMs = now;
  }
}

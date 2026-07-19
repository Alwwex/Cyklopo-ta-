// CykloComp - chytry cyklocomputer, Waveshare ESP32-C3-Zero + ST7789 + GPS + NimBLE
// Arduino IDE: v tomto adresari smi byt jen tento jeden .ino soubor.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <TinyGPSPlus.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <math.h>

// ============================================================================
// PIN CONFIGURACE
// ============================================================================
#define TFT_CS    3
#define TFT_DC    2
#define TFT_RST   1
#define TFT_MOSI  6
#define TFT_SCLK  4

#define GPS_RX_PIN 20   // do ESP (GPS TX -> sem)
#define GPS_TX_PIN 21   // z ESP (do GPS RX)
#define GPS_BAUD   9600

#define BTN1_PIN 8
#define BTN2_PIN 9
#define BTN3_PIN 10

// ============================================================================
// BLE UUID + PROTOKOL
// ============================================================================
#define SERVICE_UUID      "a5c40001-2f0b-4f6e-9d3a-8c1e2b7d9f10"
#define TELEMETRY_UUID    "a5c40002-2f0b-4f6e-9d3a-8c1e2b7d9f10"
#define COMMAND_UUID      "a5c40003-2f0b-4f6e-9d3a-8c1e2b7d9f10"

#define MAX_ROUTE_POINTS  300
#define MAX_STREET_POINTS 600
#define BREADCRUMB_SIZE   200
#define CMD_BUF_SIZE      620

// ============================================================================
// BAREVNA PALETA (RGB565)
// ============================================================================
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((uint8_t)(b)>>3)))

static const uint16_t COL_BG        = RGB565(0,0,0);
static const uint16_t COL_PANEL     = RGB565(26,30,34);
static const uint16_t COL_PANEL_LN  = RGB565(55,60,66);
static const uint16_t COL_ACCENT    = RGB565(255,122,26); // #FF7A1A
static const uint16_t COL_OK        = RGB565(60,200,90);
static const uint16_t COL_BLE_ON    = RGB565(60,140,255);
static const uint16_t COL_BLE_OFF   = RGB565(90,90,90);
static const uint16_t COL_WARN      = RGB565(255,210,40);
static const uint16_t COL_GRAY      = RGB565(150,150,150);
static const uint16_t COL_WHITE     = RGB565(255,255,255);
static const uint16_t COL_RED       = RGB565(230,50,50);

static const uint16_t COL_MAP_BG    = RGB565(233,227,210);
static const uint16_t COL_MAP_GRID  = RGB565(208,200,182);
static const uint16_t COL_MAP_STREET= RGB565(150,146,138);
static const uint16_t COL_ROUTE_BLUE= RGB565(20,90,220);
static const uint16_t COL_ROUTE_EDGE= RGB565(255,255,255);
static const uint16_t COL_TRACK     = COL_ACCENT;

// ============================================================================
// STRUCTY (MUSI BYT NAD VSEMI FUNKCEMI - Arduino IDE generuje prototypy!)
// ============================================================================
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
  int16_t x, y;       // y = baseline
  int16_t w, h;        // w = sirka pole, h = vyska nad baseline pouzita pro mazani
  const GFXfont* font;
  uint16_t color;
  char text[24];
  bool valid;
};

struct ButtonState {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  bool longFired;
};

struct MapTransform {
  double centerLat, centerLng;
  double metersPerPixel;
  double lngScale; // cos(centerLat)
  int16_t originX, originY;
};

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
volatile bool bleConnected = false;

// obrazovky: 0=RYCHLOST 1=STATISTIKY 2=TRASA 3=POZICE
uint8_t currentScreen = 0;
uint8_t lastDrawnScreen = 255;

bool recording = false;
bool manualPaused = false;

double tripDistanceM = 0;
double odometerM = 0;
float  maxSpeedKmh = 0;
unsigned long rideTimeMs = 0;

float speedEmaRaw = 0;
float filteredSpeedKmh = 0;
static const float SPEED_EMA_ALPHA = 0.35f;
static const float SPEED_STOP_THRESHOLD_KMH = 2.5f;

bool haveRefPoint = false;
double refLat = 0, refLng = 0;

int timezoneOffsetHours = 2;
uint8_t fieldConfig[4] = {0, 1, 3, 4}; // TRIP, CAS, MAX, VYSKA

RoutePoint routePoints[MAX_ROUTE_POINTS];
int routePointCount = 0;
bool routeValid = false;
bool receivingRoute = false;

GeoPoint streetPoints[MAX_STREET_POINTS];
int streetPointCount = 0;
bool receivingStreets = false;

GeoPoint breadcrumb[BREADCRUMB_SIZE];
int breadcrumbCount = 0;
int breadcrumbHead = 0; // index nejstarsiho bodu
bool haveBreadcrumbRef = false;
double lastBreadcrumbLat = 0, lastBreadcrumbLng = 0;

bool mapStaticDrawn = false;
bool wasTrackingMode = false;
unsigned long lastMapDrawMs = 0;
double lastMapDrawLat = 0, lastMapDrawLng = 0;
bool haveLastMapDraw = false;
MapTransform mapT;
int16_t mapAreaX, mapAreaY, mapAreaW, mapAreaH;

unsigned long lastLoopMillis = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastNvsSaveMs = 0;

char cmdBuf[CMD_BUF_SIZE];

ButtonState btn1 = {BTN1_PIN, false, false, 0, 0, false};
ButtonState btn2 = {BTN2_PIN, false, false, 0, 0, false};
ButtonState btn3 = {BTN3_PIN, false, false, 0, 0, false};

// status bar pole
DataField fldClock  = {4,   18, 62, 16, &FreeSans9pt7b, COL_WHITE, "", false};
DataField fldSats   = {182, 18, 34, 16, &FreeSans9pt7b, COL_GRAY,  "", false};
bool lastRecState = false, lastPauseStateDrawn = false, lastBleDrawn = false;
uint8_t lastDotsScreen = 255;

// obrazovka RYCHLOST
DataField fldSpeedBig = {10, 130, 220, 90, &FreeSansBold24pt7b, COL_GRAY, "", false};
DataField fldGridVal[4]; // hodnoty 2x2 mrizky
DataField fldGridLbl[4]; // popisky 2x2 mrizky (staticke, kresli se jen pri zmene layoutu)
bool noGpsMsgShown = false;

// obrazovka STATISTIKY
DataField fldStAlt   = {10,  70,  220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldStMax   = {10,  110, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldStTrip  = {10,  150, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldStTime  = {10,  190, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldStOdo   = {10,  280, 220, 46, &FreeSansBold24pt7b, COL_ACCENT, "", false};

// obrazovka TRASA
DataField fldDestDist = {10, 312, 220, 18, &FreeSans9pt7b, COL_WHITE, "", false};
DataField fldMapMsg   = {20, 170, 200, 20, &FreeSans12pt7b, COL_WARN, "", false};

// obrazovka POZICE
DataField fldPosLat = {10, 90,  220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldPosLng = {10, 130, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldPosAlt = {10, 170, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};
DataField fldPosSat = {10, 210, 220, 26, &FreeSans12pt7b, COL_WHITE, "", false};

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
    default: return "?";
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

void formatFieldValue(uint8_t idx, char* out, size_t outLen) {
  switch (idx) {
    case 0: snprintf(out, outLen, "%.2f km", tripDistanceM / 1000.0); break;
    case 1: { char t[16]; formatTime(rideTimeMs, t, sizeof(t)); snprintf(out, outLen, "%s", t); break; }
    case 2: {
      float hours = rideTimeMs / 3600000.0f;
      float avg = (hours > 0.01f) ? (tripDistanceM / 1000.0f) / hours : 0.0f;
      snprintf(out, outLen, "%.1f km/h", avg);
      break;
    }
    case 3: snprintf(out, outLen, "%.1f km/h", maxSpeedKmh); break;
    case 4: snprintf(out, outLen, "%.0f m", gps.altitude.isValid() ? gps.altitude.meters() : 0.0); break;
    case 5: snprintf(out, outLen, "%.1f km", odometerM / 1000.0); break;
    case 6: snprintf(out, outLen, "%d", gps.satellites.isValid() ? (int)gps.satellites.value() : 0); break;
    case 7: {
      if (gps.time.isValid()) {
        int hh = ((int)gps.time.hour() + timezoneOffsetHours + 24) % 24;
        snprintf(out, outLen, "%02d:%02d", hh, (int)gps.time.minute());
      } else snprintf(out, outLen, "--:--");
      break;
    }
    default: snprintf(out, outLen, "-"); break;
  }
}

// ============================================================================
// ANTI-FLICKER KRESLENI POLE
// ============================================================================
void invalidateField(DataField &f) { f.valid = false; }

void drawField(DataField &f, const char* newText, uint16_t bg) {
  if (f.valid && strcmp(f.text, newText) == 0) return;
  tft.fillRect(f.x, f.y - f.h, f.w, f.h + 6, bg);
  tft.setFont(f.font);
  tft.setTextColor(f.color, bg);
  tft.setCursor(f.x, f.y);
  tft.print(newText);
  strncpy(f.text, newText, sizeof(f.text) - 1);
  f.text[sizeof(f.text) - 1] = 0;
  f.valid = true;
}

// ============================================================================
// STATUSOVA LISTA
// ============================================================================
void drawRecIndicator() {
  bool showRec = recording && !manualPaused;
  bool showPause = recording && manualPaused;
  if (showRec == lastRecState && showPause == lastPauseStateDrawn) return;
  tft.fillRect(90, 4, 20, 16, COL_BG);
  if (showRec) {
    tft.fillCircle(100, 12, 6, COL_RED);
  } else if (showPause) {
    tft.fillRect(95, 6, 4, 12, COL_WARN);
    tft.fillRect(103, 6, 4, 12, COL_WARN);
  }
  lastRecState = showRec;
  lastPauseStateDrawn = showPause;
}

void drawScreenDots() {
  if (lastDotsScreen == currentScreen) return;
  tft.fillRect(120, 4, 56, 16, COL_BG);
  for (int i = 0; i < 4; i++) {
    int cx = 128 + i * 14;
    if (i == currentScreen) tft.fillCircle(cx, 12, 4, COL_ACCENT);
    else tft.drawCircle(cx, 12, 4, COL_GRAY);
  }
  lastDotsScreen = currentScreen;
}

void drawBleIcon() {
  bool c = bleConnected;
  if (c == lastBleDrawn) return;
  tft.fillRect(216, 3, 20, 18, COL_BG);
  uint16_t col = c ? COL_BLE_ON : COL_BLE_OFF;
  // jednoducha "bluetooth" znacka z primitiv
  tft.drawLine(226, 4, 226, 20, col);
  tft.drawLine(226, 4, 232, 9, col);
  tft.drawLine(232, 9, 220, 15, col);
  tft.drawLine(220, 15, 232, 21 - 6, col);
  tft.drawLine(226, 20, 220, 14, col);
  lastBleDrawn = c;
}

void updateStatusBar() {
  char buf[16];
  if (gps.time.isValid()) {
    int hh = ((int)gps.time.hour() + timezoneOffsetHours + 24) % 24;
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, (int)gps.time.minute());
  } else {
    snprintf(buf, sizeof(buf), "--:--");
  }
  drawField(fldClock, buf, COL_BG);

  int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  snprintf(buf, sizeof(buf), "%d", sats);
  drawField(fldSats, buf, COL_BG);

  drawRecIndicator();
  drawScreenDots();
  drawBleIcon();
}

// ============================================================================
// BREADCRUMB (projeta stopa)
// ============================================================================
void breadcrumbClear() {
  breadcrumbCount = 0;
  breadcrumbHead = 0;
  haveBreadcrumbRef = false;
}

void breadcrumbPush(double lat, double lng) {
  int idx = (breadcrumbHead + breadcrumbCount) % BREADCRUMB_SIZE;
  if (breadcrumbCount < BREADCRUMB_SIZE) {
    breadcrumbCount++;
  } else {
    breadcrumbHead = (breadcrumbHead + 1) % BREADCRUMB_SIZE; // zahod nejstarsi
  }
  breadcrumb[idx].lat = lat;
  breadcrumb[idx].lng = lng;
}

void maybeAddBreadcrumb(double lat, double lng) {
  if (!haveBreadcrumbRef) {
    lastBreadcrumbLat = lat;
    lastBreadcrumbLng = lng;
    haveBreadcrumbRef = true;
    breadcrumbPush(lat, lng);
    return;
  }
  double d = TinyGPSPlus::distanceBetween(lastBreadcrumbLat, lastBreadcrumbLng, lat, lng);
  if (d >= 15.0) {
    breadcrumbPush(lat, lng);
    lastBreadcrumbLat = lat;
    lastBreadcrumbLng = lng;
  }
}

// ============================================================================
// RIDE STATE
// ============================================================================
void resetTrip() {
  tripDistanceM = 0;
  rideTimeMs = 0;
  maxSpeedKmh = 0;
  haveRefPoint = false;
  breadcrumbClear();
}

void startRide() {
  recording = true;
  manualPaused = false;
  resetTrip();
  mapStaticDrawn = false;
}

void stopRide() {
  recording = false;
  manualPaused = false;
}

void pauseRide() { if (recording) manualPaused = true; }
void resumeRide() { if (recording) manualPaused = false; }

// ============================================================================
// GPS + FILTRACE
// ============================================================================
void readGpsSerial() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

void updateFilteredSpeed() {
  float raw = gps.speed.isValid() ? (float)gps.speed.kmph() : 0.0f;
  speedEmaRaw = SPEED_EMA_ALPHA * raw + (1.0f - SPEED_EMA_ALPHA) * speedEmaRaw;
  filteredSpeedKmh = (speedEmaRaw < SPEED_STOP_THRESHOLD_KMH) ? 0.0f : speedEmaRaw;
}

void updateDistanceAndStats() {
  if (!gps.location.isUpdated() || !gps.location.isValid()) return;
  double lat = gps.location.lat();
  double lng = gps.location.lng();

  if (!haveRefPoint) {
    refLat = lat; refLng = lng; haveRefPoint = true;
  } else {
    double stepM = TinyGPSPlus::distanceBetween(refLat, refLng, lat, lng);
    bool moving = filteredSpeedKmh >= 3.0f;
    if (stepM >= 2.0) {
      if (moving) {
        tripDistanceM += stepM;
        odometerM += stepM;
      }
      refLat = lat; refLng = lng;
    }
  }

  if (filteredSpeedKmh > maxSpeedKmh) maxSpeedKmh = filteredSpeedKmh;
  maybeAddBreadcrumb(lat, lng);
}

void updateRideClock() {
  unsigned long now = millis();
  unsigned long dt = now - lastLoopMillis;
  lastLoopMillis = now;
  bool isMoving = filteredSpeedKmh > 0.0f;
  bool runningTime = recording ? !manualPaused : isMoving;
  if (runningTime) rideTimeMs += dt;
}

// ============================================================================
// TLACITKA (debounce 30ms, dlouhy stisk 2000ms)
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
      } else {
        if (!b.longFired) result = 1; // kratky stisk pri puste
      }
    } else if (b.stableState && !b.longFired && (now - b.pressStartMs) >= 2000) {
      b.longFired = true;
      result = 2; // dlouhy stisk
    }
  }
  return result;
}

void handleButtons() {
  int e1 = pollButton(btn1);
  if (e1 == 1) { currentScreen = (currentScreen + 1) % 4; }

  int e2 = pollButton(btn2);
  if (e2 == 1) { if (!recording) startRide(); else stopRide(); }
  else if (e2 == 2) { resetTrip(); }

  int e3 = pollButton(btn3);
  if (e3 == 1) {
    if (recording) { if (manualPaused) resumeRide(); else pauseRide(); }
    else { currentScreen = (currentScreen + 3) % 4; }
  }
}

// ============================================================================
// KRESLENI OBRAZOVEK - STATICKE LAYOUTY (kresli se jen pri prepnuti obrazovky)
// ============================================================================
void layoutSpeedScreen() {
  tft.fillRect(0, 24, 240, 296, COL_BG);
  tft.drawFastHLine(10, 226, 220, COL_PANEL_LN);
  tft.drawFastVLine(120, 236, 76, COL_PANEL_LN);
  tft.drawFastHLine(10, 274, 220, COL_PANEL_LN);

  int16_t xs[4] = {14, 130, 14, 130};
  int16_t ys[4] = {258, 258, 306, 306};
  for (int i = 0; i < 4; i++) {
    fldGridLbl[i] = {xs[i], (int16_t)(ys[i] - 34), 100, 14, &FreeSans9pt7b, COL_GRAY, "", false};
    fldGridVal[i] = {xs[i], ys[i], 100, 20, &FreeSans12pt7b, COL_WHITE, "", false};
    invalidateField(fldGridLbl[i]);
    invalidateField(fldGridVal[i]);
  }
  invalidateField(fldSpeedBig);
  noGpsMsgShown = false;
}

void layoutStatsScreen() {
  tft.fillRect(0, 24, 240, 296, COL_BG);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COL_GRAY, COL_BG);
  tft.setCursor(10, 54); tft.print("VYSKA");
  tft.setCursor(10, 94); tft.print("MAX RYCHLOST");
  tft.setCursor(10, 134); tft.print("TRIP");
  tft.setCursor(10, 174); tft.print("CAS JIZDY");
  tft.drawFastHLine(10, 250, 220, COL_PANEL_LN);
  tft.setCursor(10, 268); tft.print("ODOMETR");
  invalidateField(fldStAlt); invalidateField(fldStMax);
  invalidateField(fldStTrip); invalidateField(fldStTime); invalidateField(fldStOdo);
}

void layoutMapScreen() {
  mapAreaX = 6; mapAreaY = 30; mapAreaW = 228; mapAreaH = 244;
  tft.fillRect(0, 24, 240, 296, COL_BG);
  mapStaticDrawn = false;
  invalidateField(fldDestDist);
  invalidateField(fldMapMsg);
}

void layoutPosScreen() {
  tft.fillRect(0, 24, 240, 296, COL_BG);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COL_GRAY, COL_BG);
  tft.setCursor(10, 74); tft.print("LATITUDE");
  tft.setCursor(10, 114); tft.print("LONGITUDE");
  tft.setCursor(10, 154); tft.print("VYSKA");
  tft.setCursor(10, 194); tft.print("SATELITY");
  invalidateField(fldPosLat); invalidateField(fldPosLng);
  invalidateField(fldPosAlt); invalidateField(fldPosSat);
}

// ============================================================================
// AKTUALIZACE OBRAZOVEK - HODNOTY (anti-flicker, jen pri zmene)
// ============================================================================
void updateSpeedScreen() {
  bool fix = gps.location.isValid();
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", fix ? filteredSpeedKmh : 0.0f);
  fldSpeedBig.color = fix ? COL_ACCENT : COL_GRAY;
  drawField(fldSpeedBig, buf, COL_BG);

  if (!fix) {
    if (!noGpsMsgShown) {
      tft.fillRect(10, 220, 220, 20, COL_BG);
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(COL_WARN, COL_BG);
      tft.setCursor(10, 220);
      tft.print("Hledam GPS signal...");
      noGpsMsgShown = true;
    }
  } else if (noGpsMsgShown) {
    tft.fillRect(10, 205, 220, 20, COL_BG);
    noGpsMsgShown = false;
  }

  for (int i = 0; i < 4; i++) {
    drawField(fldGridLbl[i], fieldLabel(fieldConfig[i]), COL_BG);
    char v[24];
    formatFieldValue(fieldConfig[i], v, sizeof(v));
    drawField(fldGridVal[i], v, COL_BG);
  }
}

void updateStatsScreen() {
  char buf[24];
  formatFieldValue(4, buf, sizeof(buf)); drawField(fldStAlt, buf, COL_BG);
  snprintf(buf, sizeof(buf), "%.1f km/h", maxSpeedKmh); drawField(fldStMax, buf, COL_BG);
  snprintf(buf, sizeof(buf), "%.2f km", tripDistanceM / 1000.0); drawField(fldStTrip, buf, COL_BG);
  char t[16]; formatTime(rideTimeMs, t, sizeof(t)); drawField(fldStTime, t, COL_BG);
  snprintf(buf, sizeof(buf), "%.1f km", odometerM / 1000.0); drawField(fldStOdo, buf, COL_BG);
}

void updatePosScreen() {
  char buf[24];
  if (gps.location.isValid()) {
    snprintf(buf, sizeof(buf), "%.6f", gps.location.lat()); drawField(fldPosLat, buf, COL_BG);
    snprintf(buf, sizeof(buf), "%.6f", gps.location.lng()); drawField(fldPosLng, buf, COL_BG);
  } else {
    drawField(fldPosLat, "---", COL_BG);
    drawField(fldPosLng, "---", COL_BG);
  }
  formatFieldValue(4, buf, sizeof(buf)); drawField(fldPosAlt, buf, COL_BG);
  formatFieldValue(6, buf, sizeof(buf)); drawField(fldPosSat, buf, COL_BG);
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
    minLat = min(minLat, (double)routePoints[i].lat);
    maxLat = max(maxLat, (double)routePoints[i].lat);
    minLng = min(minLng, (double)routePoints[i].lng);
    maxLng = max(maxLng, (double)routePoints[i].lng);
    any = true;
  }
  for (int i = 0; i < breadcrumbCount; i++) {
    int idx = (breadcrumbHead + i) % BREADCRUMB_SIZE;
    minLat = min(minLat, (double)breadcrumb[idx].lat);
    maxLat = max(maxLat, (double)breadcrumb[idx].lat);
    minLng = min(minLng, (double)breadcrumb[idx].lng);
    maxLng = max(maxLng, (double)breadcrumb[idx].lng);
    any = true;
  }
  if (!any && gps.location.isValid()) {
    minLat = maxLat = gps.location.lat();
    minLng = maxLng = gps.location.lng();
    any = true;
  }
  if (!any) return false;

  double centerLat = (minLat + maxLat) / 2.0;
  double centerLng = (minLng + maxLng) / 2.0;
  double lngScale = cos(centerLat * DEG_TO_RAD);

  double spanLatM = (maxLat - minLat) * 111320.0;
  double spanLngM = (maxLng - minLng) * 111320.0 * lngScale;
  spanLatM = max(spanLatM * 1.2, 20.0);
  spanLngM = max(spanLngM * 1.2, 20.0);

  double mppLat = spanLatM / mapAreaH;
  double mppLng = spanLngM / mapAreaW;
  double mpp = max(mppLat, mppLng);

  mapT.centerLat = centerLat;
  mapT.centerLng = centerLng;
  mapT.lngScale = lngScale;
  mapT.metersPerPixel = mpp;
  mapT.originX = mapAreaX + mapAreaW / 2;
  mapT.originY = mapAreaY + mapAreaH / 2;
  return true;
}

void computeTrackingTransform(double lat, double lng) {
  const double WINDOW_M = 600.0;
  mapT.centerLat = lat;
  mapT.centerLng = lng;
  mapT.lngScale = cos(lat * DEG_TO_RAD);
  mapT.metersPerPixel = WINDOW_M / min(mapAreaW, mapAreaH);
  mapT.originX = mapAreaX + mapAreaW / 2;
  mapT.originY = mapAreaY + mapAreaH / 2;
}

void clipToMapArea(int16_t &x, int16_t &y) {
  x = constrain(x, mapAreaX, mapAreaX + mapAreaW);
  y = constrain(y, mapAreaY, mapAreaY + mapAreaH);
}

void drawMapGridScaleNorth() {
  double gridM = niceGridStepM(mapT.metersPerPixel * 60.0);
  double gridPx = gridM / mapT.metersPerPixel;

  for (double x = mapT.originX; x < mapAreaX + mapAreaW; x += gridPx)
    tft.drawFastVLine((int16_t)x, mapAreaY, mapAreaH, COL_MAP_GRID);
  for (double x = mapT.originX - gridPx; x > mapAreaX; x -= gridPx)
    tft.drawFastVLine((int16_t)x, mapAreaY, mapAreaH, COL_MAP_GRID);
  for (double y = mapT.originY; y < mapAreaY + mapAreaH; y += gridPx)
    tft.drawFastHLine(mapAreaX, (int16_t)y, mapAreaW, COL_MAP_GRID);
  for (double y = mapT.originY - gridPx; y > mapAreaY; y -= gridPx)
    tft.drawFastHLine(mapAreaX, (int16_t)y, mapAreaW, COL_MAP_GRID);

  int16_t sy = mapAreaY + mapAreaH - 6;
  int16_t sx0 = mapAreaX + 4;
  tft.drawFastHLine(sx0, sy, (int16_t)gridPx, COL_GRAY);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COL_GRAY, COL_MAP_BG);
  char lbl[16];
  if (gridM >= 1000) snprintf(lbl, sizeof(lbl), "%.0f km", gridM / 1000.0);
  else snprintf(lbl, sizeof(lbl), "%.0f m", gridM);
  tft.setCursor(sx0, sy - 4);
  tft.print(lbl);

  int16_t nx = mapAreaX + mapAreaW - 14, ny = mapAreaY + 16;
  tft.fillTriangle(nx, ny - 12, nx - 5, ny, nx + 5, ny, COL_GRAY);
  tft.setCursor(nx - 4, ny + 14);
  tft.print("N");
}

void drawStreetsLayer() {
  int16_t px = 0, py = 0;
  bool have = false;
  for (int i = 0; i < streetPointCount; i++) {
    if (streetPoints[i].lat > 90.0f) { have = false; continue; }
    int16_t sx, sy;
    llToScreen(streetPoints[i].lat, streetPoints[i].lng, sx, sy);
    clipToMapArea(sx, sy);
    if (have) tft.drawLine(px, py, sx, sy, COL_MAP_STREET);
    px = sx; py = sy; have = true;
  }
}

void drawRouteLayer() {
  if (routePointCount < 1) return;
  for (int pass = 0; pass < 2; pass++) {
    uint16_t col = (pass == 0) ? COL_ROUTE_EDGE : COL_ROUTE_BLUE;
    int16_t px = 0, py = 0;
    for (int i = 0; i < routePointCount; i++) {
      int16_t sx, sy;
      llToScreen(routePoints[i].lat, routePoints[i].lng, sx, sy);
      clipToMapArea(sx, sy);
      if (i > 0) {
        if (pass == 0) {
          tft.drawLine(px - 1, py, sx - 1, sy, col);
          tft.drawLine(px + 1, py, sx + 1, sy, col);
          tft.drawLine(px, py - 1, sx, sy - 1, col);
          tft.drawLine(px, py + 1, sx, sy + 1, col);
        } else {
          tft.drawLine(px, py, sx, sy, col);
        }
      }
      px = sx; py = sy;
    }
  }
  int16_t sx, sy;
  llToScreen(routePoints[0].lat, routePoints[0].lng, sx, sy);
  tft.drawCircle(sx, sy, 5, COL_OK);
  tft.drawCircle(sx, sy, 4, COL_OK);
  llToScreen(routePoints[routePointCount - 1].lat, routePoints[routePointCount - 1].lng, sx, sy);
  tft.fillRect(sx - 4, sy - 4, 4, 4, COL_BG);
  tft.fillRect(sx, sy, 4, 4, COL_BG);
  tft.fillRect(sx - 4, sy, 4, 4, COL_WHITE);
  tft.fillRect(sx, sy - 4, 4, 4, COL_WHITE);
}

void drawBreadcrumbLayerFull() {
  int16_t px = 0, py = 0;
  bool have = false;
  for (int i = 0; i < breadcrumbCount; i++) {
    int idx = (breadcrumbHead + i) % BREADCRUMB_SIZE;
    int16_t sx, sy;
    llToScreen(breadcrumb[idx].lat, breadcrumb[idx].lng, sx, sy);
    clipToMapArea(sx, sy);
    if (have) tft.drawLine(px, py, sx, sy, COL_TRACK);
    px = sx; py = sy; have = true;
  }
}

void drawPositionArrow(double lat, double lng) {
  int16_t cx, cy;
  llToScreen(lat, lng, cx, cy);
  tft.fillCircle(cx, cy, 11, COL_WHITE);
  float courseDeg = gps.course.isValid() ? gps.course.deg() : 0.0f;
  float rad = courseDeg * DEG_TO_RAD;
  float tipX = cx + sinf(rad) * 9, tipY = cy - cosf(rad) * 9;
  float backAng1 = rad + 2.6f, backAng2 = rad - 2.6f;
  float b1x = cx + sinf(backAng1) * 7, b1y = cy - cosf(backAng1) * 7;
  float b2x = cx + sinf(backAng2) * 7, b2y = cy - cosf(backAng2) * 7;
  tft.fillTriangle((int16_t)tipX, (int16_t)tipY, (int16_t)b1x, (int16_t)b1y, (int16_t)b2x, (int16_t)b2y, COL_BLE_ON);
}

double distanceToDestinationAlongRoute() {
  if (!routeValid || routePointCount < 2 || !gps.location.isValid()) return -1;
  double lat = gps.location.lat(), lng = gps.location.lng();
  double best = 1e18; int bestIdx = 0;
  for (int i = 0; i < routePointCount; i++) {
    double d = TinyGPSPlus::distanceBetween(lat, lng, routePoints[i].lat, routePoints[i].lng);
    if (d < best) { best = d; bestIdx = i; }
  }
  double total = routePoints[routePointCount - 1].cumDistM;
  double remain = total - routePoints[bestIdx].cumDistM;
  if (remain < 0) remain = 0;
  return remain / 1000.0;
}

void drawMapMessage(const char* msg) {
  tft.fillRect(mapAreaX, mapAreaY, mapAreaW, mapAreaH, COL_BG);
  drawField(fldMapMsg, msg, COL_BG);
  mapStaticDrawn = false;
}

void updateMapScreen() {
  if (mapReceivingActive()) {
    drawMapMessage("Prijimam mapu...");
    return;
  }

  bool fix = gps.location.isValid();
  bool trackingMode = recording && !manualPaused && fix;

  if (trackingMode) {
    unsigned long now = millis();
    bool shouldRedraw = !mapStaticDrawn || !wasTrackingMode;
    if (!shouldRedraw && haveLastMapDraw) {
      double moved = TinyGPSPlus::distanceBetween(lastMapDrawLat, lastMapDrawLng, gps.location.lat(), gps.location.lng());
      bool overThreshold = moved > 40.0 && filteredSpeedKmh > 3.0 && (now - lastMapDrawMs) > 3000;
      shouldRedraw = overThreshold;
    } else if (!haveLastMapDraw) {
      shouldRedraw = true;
    }

    if (shouldRedraw) {
      computeTrackingTransform(gps.location.lat(), gps.location.lng());
      tft.fillRect(mapAreaX, mapAreaY, mapAreaW, mapAreaH, COL_MAP_BG);
      drawMapGridScaleNorth();
      drawStreetsLayer();
      drawRouteLayer();
      drawBreadcrumbLayerFull();
      drawPositionArrow(gps.location.lat(), gps.location.lng());
      lastMapDrawMs = now;
      lastMapDrawLat = gps.location.lat();
      lastMapDrawLng = gps.location.lng();
      haveLastMapDraw = true;
      mapStaticDrawn = true;
    }
  } else {
    if (!mapStaticDrawn) {
      if (computeOverviewTransform()) {
        tft.fillRect(mapAreaX, mapAreaY, mapAreaW, mapAreaH, COL_MAP_BG);
        drawMapGridScaleNorth();
        drawStreetsLayer();
        drawRouteLayer();
        drawBreadcrumbLayerFull();
        mapStaticDrawn = true;
      } else {
        drawMapMessage("Zadna trasa ani stopa");
        return;
      }
    } else {
      // inkrementalni dokresleni posledniho useku stopy (bez prekresleni cele mapy)
      static int lastDrawnBreadcrumbCount = -1;
      if (breadcrumbCount != lastDrawnBreadcrumbCount && breadcrumbCount >= 2) {
        int idxPrev = (breadcrumbHead + breadcrumbCount - 2) % BREADCRUMB_SIZE;
        int idxNew = (breadcrumbHead + breadcrumbCount - 1) % BREADCRUMB_SIZE;
        int16_t px, py, sx, sy;
        llToScreen(breadcrumb[idxPrev].lat, breadcrumb[idxPrev].lng, px, py);
        llToScreen(breadcrumb[idxNew].lat, breadcrumb[idxNew].lng, sx, sy);
        clipToMapArea(px, py); clipToMapArea(sx, sy);
        tft.drawLine(px, py, sx, sy, COL_TRACK);
        lastDrawnBreadcrumbCount = breadcrumbCount;
      }
    }
  }
  wasTrackingMode = trackingMode;

  double destKm = distanceToDestinationAlongRoute();
  char buf[32];
  if (destKm >= 0) snprintf(buf, sizeof(buf), "DO CILE: %.1f km", destKm);
  else snprintf(buf, sizeof(buf), "DO CILE: --");
  drawField(fldDestDist, buf, COL_BG);
}

// ============================================================================
// PREPINANI OBRAZOVEK
// ============================================================================
void renderScreens() {
  if (currentScreen != lastDrawnScreen) {
    switch (currentScreen) {
      case 0: layoutSpeedScreen(); break;
      case 1: layoutStatsScreen(); break;
      case 2: layoutMapScreen(); break;
      case 3: layoutPosScreen(); break;
    }
    lastDrawnScreen = currentScreen;
  }
  switch (currentScreen) {
    case 0: updateSpeedScreen(); break;
    case 1: updateStatsScreen(); break;
    case 2: updateMapScreen(); break;
    case 3: updatePosScreen(); break;
  }
  updateStatusBar();
}

// ============================================================================
// BLE PRIKAZOVY PROTOKOL
// ============================================================================
bool mapReceivingActive() { return receivingRoute || receivingStreets; }

void finalizeRoute() {
  double cum = 0;
  routePoints[0].cumDistM = 0;
  for (int i = 1; i < routePointCount; i++) {
    cum += TinyGPSPlus::distanceBetween(routePoints[i - 1].lat, routePoints[i - 1].lng,
                                         routePoints[i].lat, routePoints[i].lng);
    routePoints[i].cumDistM = cum;
  }
  routeValid = routePointCount > 0;
  mapStaticDrawn = false;
}

void parseRoutePointsChunk(const char* data) {
  // format: lat,lng;lat,lng;...
  const char* p = data;
  while (*p && routePointCount < MAX_ROUTE_POINTS) {
    char* comma;
    float lat = strtof(p, &comma);
    if (comma == p) break;
    if (*comma != ',') break;
    char* semi;
    float lng = strtof(comma + 1, &semi);
    if (semi == comma + 1) break;
    routePoints[routePointCount].lat = lat;
    routePoints[routePointCount].lng = lng;
    routePoints[routePointCount].cumDistM = 0;
    routePointCount++;
    if (*semi == ';') p = semi + 1; else break;
  }
}

void parseStreetPointsChunk(const char* data) {
  const char* p = data;
  while (*p && streetPointCount < MAX_STREET_POINTS) {
    char* comma;
    float lat = strtof(p, &comma);
    if (comma == p) break;
    if (*comma != ',') break;
    char* semi;
    float lng = strtof(comma + 1, &semi);
    if (semi == comma + 1) break;
    streetPoints[streetPointCount].lat = lat; // 999 = oddelovac segmentu
    streetPoints[streetPointCount].lng = lng;
    streetPointCount++;
    if (*semi == ';') p = semi + 1; else break;
  }
}

void saveFieldConfigToNvs() {
  prefs.putUChar("f0", fieldConfig[0]);
  prefs.putUChar("f1", fieldConfig[1]);
  prefs.putUChar("f2", fieldConfig[2]);
  prefs.putUChar("f3", fieldConfig[3]);
}

void handleCommand(const char* cmd) {
  if (!strcmp(cmd, "RIDE:START")) { startRide(); }
  else if (!strcmp(cmd, "RIDE:STOP")) { stopRide(); }
  else if (!strcmp(cmd, "RIDE:PAUSE")) { pauseRide(); }
  else if (!strcmp(cmd, "RIDE:RESUME")) { resumeRide(); }
  else if (!strcmp(cmd, "RESET_TRIP")) { resetTrip(); }
  else if (!strncmp(cmd, "SET_TZ:", 7)) {
    timezoneOffsetHours = atoi(cmd + 7);
    prefs.putInt("tz", timezoneOffsetHours);
  }
  else if (!strncmp(cmd, "SCREEN:", 7)) {
    int n = atoi(cmd + 7);
    if (n >= 0 && n <= 3) currentScreen = (uint8_t)n;
  }
  else if (!strncmp(cmd, "CFG:FIELDS:", 11)) {
    int a, b, c, d;
    if (sscanf(cmd + 11, "%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
      fieldConfig[0] = a; fieldConfig[1] = b; fieldConfig[2] = c; fieldConfig[3] = d;
      saveFieldConfigToNvs();
      if (currentScreen == 0) lastDrawnScreen = 255; // vynutit prekresleni layoutu
    }
  }
  else if (!strcmp(cmd, "RT:BEGIN")) { receivingRoute = true; routePointCount = 0; routeValid = false; }
  else if (!strncmp(cmd, "RT:P:", 5)) { parseRoutePointsChunk(cmd + 5); }
  else if (!strcmp(cmd, "RT:END")) { receivingRoute = false; finalizeRoute(); }
  else if (!strcmp(cmd, "RT:CLEAR")) { routePointCount = 0; routeValid = false; mapStaticDrawn = false; }
  else if (!strcmp(cmd, "MP:BEGIN")) { receivingStreets = true; streetPointCount = 0; }
  else if (!strncmp(cmd, "MP:P:", 5)) { parseStreetPointsChunk(cmd + 5); }
  else if (!strcmp(cmd, "MP:END")) { receivingStreets = false; mapStaticDrawn = false; }
}

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string v = pChar->getValue();
    size_t len = v.length();
    if (len >= CMD_BUF_SIZE) len = CMD_BUF_SIZE - 1;
    memcpy(cmdBuf, v.data(), len);
    cmdBuf[len] = 0;
    handleCommand(cmdBuf);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
    bleConnected = true;
  }
  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    bleConnected = false;
    NimBLEDevice::startAdvertising();
  }
};

void initBLE() {
  NimBLEDevice::init("CykloComp");
  NimBLEDevice::setMTU(185);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = pServer->createService(SERVICE_UUID);
  telemetryChar = svc->createCharacteristic(TELEMETRY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  commandChar = svc->createCharacteristic(COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  commandChar->setCallbacks(new CommandCallbacks());
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();
}

void sendTelemetry() {
  if (!bleConnected || mapReceivingActive()) return;
  char buf[196];
  bool fix = gps.location.isValid();
  snprintf(buf, sizeof(buf),
    "{\"fix\":%d,\"spd\":%.1f,\"lat\":%.6f,\"lng\":%.6f,\"alt\":%.1f,\"dst\":%.3f,\"tim\":%lu,\"sat\":%d,\"rid\":%d,\"pau\":%d}",
    fix ? 1 : 0,
    filteredSpeedKmh,
    fix ? gps.location.lat() : 0.0,
    fix ? gps.location.lng() : 0.0,
    gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
    tripDistanceM / 1000.0,
    rideTimeMs / 1000,
    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
    recording ? 1 : 0,
    manualPaused ? 1 : 0
  );
  telemetryChar->setValue((uint8_t*)buf, strlen(buf));
  telemetryChar->notify();
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
  recording = prefs.getBool("rec", false);
  timezoneOffsetHours = prefs.getInt("tz", 2);
  fieldConfig[0] = prefs.getUChar("f0", 0);
  fieldConfig[1] = prefs.getUChar("f1", 1);
  fieldConfig[2] = prefs.getUChar("f2", 3);
  fieldConfig[3] = prefs.getUChar("f3", 4);
}

void savePreferencesPeriodic() {
  prefs.putFloat("odoM", odometerM);
  prefs.putFloat("tripM", tripDistanceM);
  prefs.putULong("timeMs", rideTimeMs);
  prefs.putFloat("maxKmh", maxSpeedKmh);
  prefs.putBool("rec", recording);
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);

  loadPreferences();

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  initBLE();

  lastLoopMillis = millis();
}

void loop() {
  readGpsSerial();
  updateFilteredSpeed();
  updateDistanceAndStats();
  updateRideClock();
  handleButtons();
  renderScreens();

  unsigned long now = millis();
  if (now - lastTelemetryMs >= 1000) {
    sendTelemetry();
    lastTelemetryMs = now;
  }
  if (now - lastNvsSaveMs >= 60000) {
    savePreferencesPeriodic();
    lastNvsSaveMs = now;
  }
}

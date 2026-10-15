// Nahled displeje CykloComp na PC: prelozi PRESNE stejny CykloComp.ino,
// misto SPI displeje kresli do pameti a uklada obrazky obrazovek (PPM).
// Spusteni: ./build_preview.sh  (vystup v ./out/*.png)
#include "Arduino.h"

uint32_t g_fakeMillis = 0;
int g_buttonsDown = 0;
uint32_t g_fakeAdcMv = 1960;   // ~3.92 V na baterii (delic 1:2)
std::string g_fsRoot = "./preview_fs";

#include "../CykloComp/CykloComp.ino"

HardwareSerial Serial;
SPIClass SPI;
LittleFSClass LittleFS;

static void savePPM(const char* name) {
  std::string path = std::string("out/") + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); return; }
  fprintf(f, "P6\n240 320\n255\n");
  const uint16_t* buf = tft.getBuffer();
  for (int i = 0; i < 240 * 320; i++) {
    uint16_t c = buf[i];
    uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63), (uint8_t)((c & 0x1F) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("ulozeno %s\n", path.c_str());
}

static void cmd(const char* c) { handleCommand(c); }

static void runFor(uint32_t ms) {
  uint32_t end = g_fakeMillis + ms;
  while (g_fakeMillis < end) {
    // falesne BLE senzory (tep + kadence)
    hrConnected = true; cscConnected = true;
    hrBpm = 128 + (int)(14 * sin(g_fakeMillis / 20000.0));
    hrLastMs = g_fakeMillis;
    cadRpm = filteredSpeedKmh > 1 ? 82 + (int)(6 * sin(g_fakeMillis / 7000.0)) : 0;
    cadLastMs = g_fakeMillis;
    loop();
    g_fakeMillis += 50;
  }
}

static void sendRouteAndStreets() {
  // klikata trasa ~9 km kolem startu
  const double lat0 = 50.0755, lng0 = 14.4378;
  char buf[200];
  cmd("RT:CLEAR");
  cmd("RT:BEGIN");
  std::string chunk;
  int inChunk = 0;
  for (int i = 0; i <= 80; i++) {
    double t = i / 80.0 * 2 * M_PI;
    double lat = lat0 + 0.018 * sin(t) + 0.002 * sin(5 * t);
    double lng = lng0 + 0.03 * (1 - cos(t)) + 0.003 * cos(3 * t);
    snprintf(buf, sizeof(buf), "%.5f,%.5f;", lat, lng);
    chunk += buf;
    if (++inChunk == 8) { cmd(("RT:P:" + chunk).c_str()); chunk.clear(); inChunk = 0; }
  }
  if (!chunk.empty()) cmd(("RT:P:" + chunk).c_str());
  cmd("RT:END");

  // ulice: pravidelna sit s par diagonalami
  cmd("MP:BEGIN");
  chunk.clear(); inChunk = 0;
  auto flush = [&]() { if (!chunk.empty()) { cmd(("MP:P:" + chunk).c_str()); chunk.clear(); inChunk = 0; } };
  auto add = [&](double la, double lo) { snprintf(buf, sizeof(buf), "%.5f,%.5f;", la, lo); chunk += buf; if (++inChunk == 8) flush(); };
  for (int r = -6; r <= 6; r++) {
    for (int k = 0; k <= 8; k++) add(lat0 + r * 0.004 + 0.0007 * sin(k), lng0 - 0.01 + k * 0.009);
    add(999, 999);
  }
  for (int c = -2; c <= 9; c++) {
    for (int k = 0; k <= 8; k++) add(lat0 - 0.026 + k * 0.0065, lng0 + c * 0.0075 + 0.0009 * cos(k));
    add(999, 999);
  }
  flush();
  cmd("MP:END");
}

int main() {
  system("mkdir -p out && rm -rf ./preview_fs");
  setup();
  // splash se kresli v setup() - zachyt ho znovu
  drawSplash();
  savePPM("00_splash");
  forceFullRedraw();

  cmd("TIME:1791457380");   // 8. 10. 2026 11:03 UTC
  cmd("SET_TZ:2");
  sendRouteAndStreets();
  cmd("SIM:1");
  cmd("RIDE:START");
  runFor(14 * 60 * 1000);

  const char* themes[3] = {"noc", "retro", "den"};
  for (int th = 0; th < 3; th++) {
    char c[16]; snprintf(c, sizeof(c), "THEME:%d", th); cmd(c);
    const char* scr[4] = {"dash", "statistiky", "mapa", "system"};
    for (int s = 0; s < 4; s++) {
      snprintf(c, sizeof(c), "SCREEN:%d", s); cmd(c);
      runFor(3000);
      char name[40]; snprintf(name, sizeof(name), "%d%d_%s_%s", th + 1, s, themes[th], scr[s]);
      savePPM(name);
    }
  }
  cmd("THEME:0");
  cmd("SCREEN:0");
  runFor(2000);
  cmd("RIDE:STOP");
  runFor(500);
  savePPM("90_souhrn_jizdy");

  // kontrola, ze se jizda ulozila a jde stahnout pres SYNC
  Serial.echo = true;
  cmd("SYNC:LIST");
  cmd("SYNC:SUM:1");
  cmd("SYNC:TRK:1:0");
  return 0;
}

// Displej pro PC: misto SPI se kresli do pole pixelu (GFXcanvas16).
#pragma once
#include "Arduino.h"
#include <Adafruit_GFX.h>
class Adafruit_ST7789 : public GFXcanvas16 {
public:
  Adafruit_ST7789(int, int, int) : GFXcanvas16(240, 320) {}
  void init(uint16_t, uint16_t, uint8_t = 0) {}
  void setSPISpeed(uint32_t) {}
  void enableDisplay(bool) {}
  void enableSleep(bool) {}
};

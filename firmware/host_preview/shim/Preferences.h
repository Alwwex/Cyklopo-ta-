#pragma once
#include "Arduino.h"
#include <map>
#include <string>
class Preferences {
public:
  bool begin(const char*, bool) { return true; }
  float getFloat(const char* k, float d) { return get(k, d); }
  void putFloat(const char* k, float v) { m[k] = v; }
  unsigned long getULong(const char* k, unsigned long d) { return (unsigned long)get(k, (double)d); }
  void putULong(const char* k, unsigned long v) { m[k] = v; }
  bool getBool(const char* k, bool d) { return get(k, d) != 0; }
  void putBool(const char* k, bool v) { m[k] = v; }
  int getInt(const char* k, int d) { return (int)get(k, d); }
  void putInt(const char* k, int v) { m[k] = v; }
  uint8_t getUChar(const char* k, uint8_t d) { return (uint8_t)get(k, d); }
  void putUChar(const char* k, uint8_t v) { m[k] = v; }
  uint16_t getUShort(const char* k, uint16_t d) { return (uint16_t)get(k, d); }
  void putUShort(const char* k, uint16_t v) { m[k] = v; }
private:
  double get(const char* k, double d) { auto it = m.find(k); return it == m.end() ? d : it->second; }
  std::map<std::string, double> m;
};

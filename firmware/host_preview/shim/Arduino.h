// Minimalni "Arduino" pro PC - jen tolik, aby sel CykloComp.ino prelozit
// a vykreslit displej do obrazku (viz host_preview/README.md).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <algorithm>
#include <string>
#include <functional>
#include <deque>
#include <vector>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

using std::min;
using std::max;

typedef bool boolean;
typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define DEC 10
#define HEX 16
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_pointer(addr) ((void *)*(void **)(addr))
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define TWO_PI 6.283185307179586476925286766559
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define ESP_ARDUINO_VERSION_MAJOR 3
#define SERIAL_8N1 0x800001c

// ---- cas: rizeny testovacim programem ----
extern uint32_t g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }
inline void delay(unsigned long ms) { g_fakeMillis += ms; }

// ---- piny ----
extern int g_buttonsDown;  // bitmask stisknutych pinu (pro testy)
inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return (g_buttonsDown >> pin) & 1 ? LOW : HIGH; }
inline void digitalWrite(int, int) {}
extern uint32_t g_fakeAdcMv;
inline uint32_t analogReadMilliVolts(int) { return g_fakeAdcMv; }
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline bool ledcWrite(uint8_t, uint32_t) { return true; }

class __FlashStringHelper;
class String {
public:
  String(const char* s = "") : s_(s ? s : "") {}
  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return s_.size(); }
private:
  std::string s_;
};

#include "Print.h"

class HardwareSerial : public Print {
public:
  HardwareSerial(int n = 0) { (void)n; }
  void begin(unsigned long, uint32_t = 0, int = -1, int = -1) {}
  void setRxBufferSize(size_t) {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(uint8_t c) override { if (echo) fputc(c, stdout); return 1; }
  bool echo = false;
};
extern HardwareSerial Serial;

// ---- FreeRTOS ----
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
struct FakeQueue { size_t itemSize; size_t cap; std::deque<std::vector<uint8_t>> items; };
typedef FakeQueue* QueueHandle_t;
inline QueueHandle_t xQueueCreate(size_t n, size_t sz) { return new FakeQueue{sz, n, {}}; }
inline BaseType_t xQueueSend(QueueHandle_t q, const void* p, TickType_t) {
  if (q->items.size() >= q->cap) return pdFALSE;
  const uint8_t* b = (const uint8_t*)p;
  q->items.emplace_back(b, b + q->itemSize);
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void* p, TickType_t) {
  if (q->items.empty()) return pdFALSE;
  memcpy(p, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}
typedef void (*TaskFunction_t)(void*);
inline BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, int, void*) { return pdTRUE; }
inline void vTaskDelay(TickType_t t) { g_fakeMillis += t; }

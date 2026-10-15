#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>

class String;

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t r = 0; while (n--) r += write(*b++); return r; }
  size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v, int base = 10) { return printNum((long)v, base); }
  size_t print(unsigned v, int base = 10) { return printNum((long)v, base); }
  size_t print(long v, int base = 10) { return printNum(v, base); }
  size_t print(unsigned long v, int base = 10) { return printNum((long)v, base); }
  size_t print(double v, int digits = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", digits, v); return write(b); }
  size_t println() { return write("\n"); }
  template <typename X> size_t println(X v) { size_t n = print(v); return n + println(); }
  size_t printf(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); return write(b);
  }
private:
  size_t printNum(long v, int base) { char b[40]; snprintf(b, sizeof(b), base == 16 ? "%lx" : "%ld", v); return write(b); }
};

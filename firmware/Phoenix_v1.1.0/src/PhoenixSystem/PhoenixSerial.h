#pragma once
#include <Arduino.h>
#include "PhoenixVersion.h"

// FINAL release: PHX_SERIAL_LEVEL==0 must silence *all* serial output,
// including legacy/direct Serial.print/printf/println calls that are not
// routed through PHX_INFO_* macros.  The local macro alias also prevents
// Serial.begin() from touching the real UART/USB CDC object in this build.
#if PHX_SERIAL_LEVEL == 0
class PhoenixSilentSerialSink {
public:
  void begin(unsigned long) {}
  template <typename T> size_t print(const T&) { return 0U; }
  template <typename T> size_t println(const T&) { return 0U; }
  size_t println() { return 0U; }
  int printf(const char*, ...) { return 0; }
};
static PhoenixSilentSerialSink phxSilentSerialSink;
#define Serial phxSilentSerialSink
#endif

#if PHX_SERIAL_LEVEL >= 2
  #define PHX_INFO_PRINTF(...)  Serial.printf(__VA_ARGS__)
  #define PHX_INFO_PRINT(...)   Serial.print(__VA_ARGS__)
  #define PHX_INFO_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define PHX_INFO_PRINTF(...)  do {} while (0)
  #define PHX_INFO_PRINT(...)   do {} while (0)
  #define PHX_INFO_PRINTLN(...) do {} while (0)
#endif

#if PHX_SERIAL_LEVEL >= 1
  #define PHX_ERROR_PRINTF(...)  Serial.printf(__VA_ARGS__)
  #define PHX_ERROR_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define PHX_ERROR_PRINTF(...)  do {} while (0)
  #define PHX_ERROR_PRINTLN(...) do {} while (0)
#endif

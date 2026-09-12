#pragma once
#include <Arduino.h>
#include "PhoenixVersion.h"

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

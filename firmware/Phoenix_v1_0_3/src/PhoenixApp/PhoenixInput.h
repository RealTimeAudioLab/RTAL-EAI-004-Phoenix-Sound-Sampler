/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>

struct PhoenixInputState {
  bool f[9];          // f[1]..f[8]
  bool encPress;
  bool encLong;
  int16_t encoderStep; // accelerated encoder delta: -10..+10 (sign = direction)

  void clear() {
    for (uint8_t i = 0; i < 9; ++i) f[i] = false;
    encPress = false;
    encLong = false;
    encoderStep = 0;
  }
};

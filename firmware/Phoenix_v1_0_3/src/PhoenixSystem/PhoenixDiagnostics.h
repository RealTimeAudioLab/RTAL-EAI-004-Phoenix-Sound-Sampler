/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>

// Project Phoenix v1.0.0 FINAL
// Lightweight, serial-only system diagnostics. No permanent buffers are used.
class PhoenixDiagnostics {
public:
  static void printBootSnapshot(const char *stage, uint8_t smartMode);

private:
  static const char *smartModeName(uint8_t mode);
  static void printBytes(const __FlashStringHelper *label, uint32_t bytes);
};

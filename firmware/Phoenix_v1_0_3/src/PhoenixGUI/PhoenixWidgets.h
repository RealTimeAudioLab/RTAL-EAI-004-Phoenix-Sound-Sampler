/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "PhoenixFont.h"

// Shared RTAL/Phoenix GUI building blocks.
// Phase 2 centralizes the frameless status text and the collision-safe footer.
class PhoenixWidgets {
public:
  static void drawStatusBox(U8G2 &d, int x, int y, int w, int h, const char *status);
  static void drawFooter(U8G2 &d, int y, const char *left, const char *mid, const char *right);

private:
  static void fitText(const char *src, char *dst, size_t cap, int maxWidth);
};

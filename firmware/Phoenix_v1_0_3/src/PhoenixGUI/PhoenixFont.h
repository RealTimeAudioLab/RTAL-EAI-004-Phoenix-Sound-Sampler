/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

class PhoenixFont {
public:
  static const uint8_t ADV = 4;   // 3 pixel glyph + 1 pixel spacing
  static const uint8_t W   = 3;
  static const uint8_t H   = 5;

  static void drawChar(U8G2 &d, int x, int y, char c, bool inverse=false);
  static void drawText(U8G2 &d, int x, int y, const char *s, bool inverse=false);
  static void drawCentered(U8G2 &d, int x, int y, int w, const char *s, bool inverse=false);
  static void drawText4(U8G2 &d, int x, int y, const char *s, bool inverse=false);
  static void drawCentered4(U8G2 &d, int x, int y, int w, const char *s, bool inverse=false);
  static int textWidth(const char *s);

private:
  static const uint8_t* glyphFor(char c);
};

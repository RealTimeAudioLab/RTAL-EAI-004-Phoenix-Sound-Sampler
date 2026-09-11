/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "PhoenixFont.h"

static const uint8_t GLYPH_SPACE[5] = {0,0,0,0,0};

const uint8_t* PhoenixFont::glyphFor(char c) {
  c = toupper((unsigned char)c);
  switch (c) {
    case 'A': { static const uint8_t g[5]={0b010,0b101,0b111,0b101,0b101}; return g; }
    case 'B': { static const uint8_t g[5]={0b110,0b101,0b110,0b101,0b110}; return g; }
    case 'C': { static const uint8_t g[5]={0b011,0b100,0b100,0b100,0b011}; return g; }
    case 'D': { static const uint8_t g[5]={0b110,0b101,0b101,0b101,0b110}; return g; }
    case 'E': { static const uint8_t g[5]={0b111,0b100,0b110,0b100,0b111}; return g; }
    case 'F': { static const uint8_t g[5]={0b111,0b100,0b110,0b100,0b100}; return g; }
    case 'G': { static const uint8_t g[5]={0b011,0b100,0b101,0b101,0b011}; return g; }
    case 'H': { static const uint8_t g[5]={0b101,0b101,0b111,0b101,0b101}; return g; }
    case 'I': { static const uint8_t g[5]={0b111,0b010,0b010,0b010,0b111}; return g; }
    case 'J': { static const uint8_t g[5]={0b001,0b001,0b001,0b101,0b010}; return g; }
    case 'K': { static const uint8_t g[5]={0b101,0b101,0b110,0b101,0b101}; return g; }
    case 'L': { static const uint8_t g[5]={0b100,0b100,0b100,0b100,0b111}; return g; }
    case 'M': { static const uint8_t g[5]={0b101,0b111,0b111,0b101,0b101}; return g; }
    case 'N': { static const uint8_t g[5]={0b101,0b111,0b111,0b111,0b101}; return g; }
    case 'O': { static const uint8_t g[5]={0b010,0b101,0b101,0b101,0b010}; return g; }
    case 'P': { static const uint8_t g[5]={0b110,0b101,0b110,0b100,0b100}; return g; }
    case 'Q': { static const uint8_t g[5]={0b010,0b101,0b101,0b111,0b011}; return g; }
    case 'R': { static const uint8_t g[5]={0b110,0b101,0b110,0b101,0b101}; return g; }
    case 'S': { static const uint8_t g[5]={0b011,0b100,0b010,0b001,0b110}; return g; }
    case 'T': { static const uint8_t g[5]={0b111,0b010,0b010,0b010,0b010}; return g; }
    case 'U': { static const uint8_t g[5]={0b101,0b101,0b101,0b101,0b111}; return g; }
    case 'V': { static const uint8_t g[5]={0b101,0b101,0b101,0b101,0b010}; return g; }
    case 'W': { static const uint8_t g[5]={0b101,0b101,0b111,0b111,0b101}; return g; }
    case 'X': { static const uint8_t g[5]={0b101,0b101,0b010,0b101,0b101}; return g; }
    case 'Y': { static const uint8_t g[5]={0b101,0b101,0b010,0b010,0b010}; return g; }
    case 'Z': { static const uint8_t g[5]={0b111,0b001,0b010,0b100,0b111}; return g; }
    case '0': { static const uint8_t g[5]={0b111,0b101,0b101,0b101,0b111}; return g; }
    case '1': { static const uint8_t g[5]={0b010,0b110,0b010,0b010,0b111}; return g; }
    case '2': { static const uint8_t g[5]={0b110,0b001,0b010,0b100,0b111}; return g; }
    case '3': { static const uint8_t g[5]={0b110,0b001,0b010,0b001,0b110}; return g; }
    case '4': { static const uint8_t g[5]={0b101,0b101,0b111,0b001,0b001}; return g; }
    case '5': { static const uint8_t g[5]={0b111,0b100,0b110,0b001,0b110}; return g; }
    case '6': { static const uint8_t g[5]={0b011,0b100,0b110,0b101,0b010}; return g; }
    case '7': { static const uint8_t g[5]={0b111,0b001,0b010,0b010,0b010}; return g; }
    case '8': { static const uint8_t g[5]={0b010,0b101,0b010,0b101,0b010}; return g; }
    case '9': { static const uint8_t g[5]={0b010,0b101,0b011,0b001,0b110}; return g; }
    case '#': { static const uint8_t g[5]={0b101,0b111,0b101,0b111,0b101}; return g; }
    case '-': { static const uint8_t g[5]={0b000,0b000,0b111,0b000,0b000}; return g; }
    case '+': { static const uint8_t g[5]={0b000,0b010,0b111,0b010,0b000}; return g; }
    case '%': { static const uint8_t g[5]={0b101,0b001,0b010,0b100,0b101}; return g; }
    case '(': { static const uint8_t g[5]={0b001,0b010,0b010,0b010,0b001}; return g; }
    case ')': { static const uint8_t g[5]={0b100,0b010,0b010,0b010,0b100}; return g; }
    case '.': { static const uint8_t g[5]={0b000,0b000,0b000,0b000,0b010}; return g; }
    case ':': { static const uint8_t g[5]={0b000,0b010,0b000,0b010,0b000}; return g; }
    case '/': { static const uint8_t g[5]={0b001,0b001,0b010,0b100,0b100}; return g; }
    case '>': { static const uint8_t g[5]={0b100,0b110,0b111,0b110,0b100}; return g; }  // Phoenix menu arrow
    case '<': { static const uint8_t g[5]={0b001,0b010,0b100,0b010,0b001}; return g; }
    case '_': { static const uint8_t g[5]={0b000,0b000,0b000,0b000,0b111}; return g; }
    default: return GLYPH_SPACE;
  }
}

int PhoenixFont::textWidth(const char *s) {
  int n = 0;
  while (s && *s++) n++;
  return n * ADV;
}

void PhoenixFont::drawChar(U8G2 &d, int x, int y, char c, bool inverse) {
  const uint8_t *g = glyphFor(c);
  if (inverse) {
    d.setDrawColor(1);
    d.drawBox(x - 1, y, ADV, H);
  }
  for (uint8_t r = 0; r < H; ++r) {
    for (uint8_t col = 0; col < W; ++col) {
      bool bit = g[r] & (1 << (2 - col));
      if (inverse) bit = !bit;
      d.setDrawColor(bit ? 1 : 0);
      d.drawPixel(x + col, y + r);
    }
  }
  d.setDrawColor(1);
}

void PhoenixFont::drawText(U8G2 &d, int x, int y, const char *s, bool inverse) {
  int cx = x;
  while (s && *s) {
    drawChar(d, cx, y, *s, inverse);
    cx += ADV;
    s++;
  }
}

void PhoenixFont::drawCentered(U8G2 &d, int x, int y, int w, const char *s, bool inverse) {
  int tx = x + max(0, (w - textWidth(s)) / 2);
  drawText(d, tx, y, s, inverse);
}


void PhoenixFont::drawText4(U8G2 &d, int x, int y, const char *s, bool inverse) {
  int cx = x;
  while (s && *s) {
    const uint8_t *g = glyphFor(*s);
    if (inverse) {
      d.setDrawColor(1);
      d.drawBox(cx - 1, y, ADV, 4);
    }
    for (uint8_t r = 0; r < 4; ++r) {
      for (uint8_t col = 0; col < W; ++col) {
        bool bit = g[r] & (1 << (2 - col));
        if (inverse) bit = !bit;
        d.setDrawColor(bit ? 1 : 0);
        d.drawPixel(cx + col, y + r);
      }
    }
    d.setDrawColor(1);
    cx += ADV;
    s++;
  }
}

void PhoenixFont::drawCentered4(U8G2 &d, int x, int y, int w, const char *s, bool inverse) {
  int tx = x + max(0, (w - textWidth(s)) / 2);
  drawText4(d, tx, y, s, inverse);
}

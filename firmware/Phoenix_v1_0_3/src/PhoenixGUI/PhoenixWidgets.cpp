/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "PhoenixWidgets.h"

void PhoenixWidgets::fitText(const char *src, char *dst, size_t cap, int maxWidth) {
  if (!dst || cap == 0) return;
  dst[0] = 0;
  if (!src || !src[0] || cap < 2) return;

  size_t n = 0;
  while (src[n] && n + 1 < cap) {
    dst[n] = src[n];
    dst[n + 1] = 0;
    if (PhoenixFont::textWidth(dst) > maxWidth) {
      dst[n] = 0;
      break;
    }
    ++n;
  }
}

void PhoenixWidgets::drawStatusBox(U8G2 &d, int x, int y, int w, int h, const char *status) {
  // RTAL status text: deliberately frameless so it remains visually subordinate
  // to the rounded LED meter above it.
  if (w < 1 || h < 1 || !status || !status[0]) return;
  d.setDrawColor(1);
  PhoenixFont::drawCentered(d, x, y + 1, w, status, false);
}

void PhoenixWidgets::drawFooter(U8G2 &d, int y, const char *left, const char *mid, const char *right) {
  // Fixed zones keep F8 BACK/EXIT readable and prevent cross-zone overlap.
  const int leftX = 1,  leftW = 37;
  const int midX = 39,  midW = 45;
  const int rightX = 86, rightW = 41;

  char l[20], m[20], r[20];
  fitText(left, l, sizeof(l), leftW);
  fitText(mid, m, sizeof(m), midW);
  fitText(right, r, sizeof(r), rightW);

  if (l[0]) PhoenixFont::drawText(d, leftX, y, l, false);
  if (m[0]) PhoenixFont::drawCentered(d, midX, y, midW, m, false);
  if (r[0]) PhoenixFont::drawText(d, rightX + rightW - PhoenixFont::textWidth(r), y, r, false);
}

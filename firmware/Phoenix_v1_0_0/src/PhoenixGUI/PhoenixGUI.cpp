#include "PhoenixGUI.h"


static void phxDrawMiniDigit(U8G2 &d, int x, int y, uint8_t digit) {
  // 3x4 digits for the narrow LEVEL meter. This avoids the squeezed look
  // of the full 5-pixel PhoenixFont inside the small left-side meter.
  static const uint8_t glyphs[9][4] = {
    {0,0,0,0},       // 0 unused here
    {2,6,2,7},       // 1
    {7,1,7,4},       // 2
    {7,3,1,7},       // 3
    {5,7,1,1},       // 4
    {7,6,1,7},       // 5
    {7,6,7,7},       // 6
    {7,1,2,2},       // 7
    {7,7,7,7}        // 8
  };
  if (digit > 8) return;
  d.setDrawColor(1);
  for (uint8_t row = 0; row < 4; ++row) {
    uint8_t bits = glyphs[digit][row];
    for (uint8_t col = 0; col < 3; ++col) {
      if (bits & (1 << (2 - col))) d.drawPixel(x + col, y + row);
    }
  }
}


PhoenixGUI::PhoenixGUI(U8G2 &display) : _d(display), _level(0), _peakHold(0), _high(false), _clip(false), _recording(false), _playing(false), _triggerMarkerEnabled(false), _triggerMarkerLevel(4), _recordStatus(0), _waveCount(0), _waveValid(false), _waveZoom(1), _waveCenterQ(5000) {
  memset(_waveBins, 0, sizeof(_waveBins));
}

void PhoenixGUI::setLevel(uint8_t level) {
  if (level > 8) level = 8;
  _level = level;
}

void PhoenixGUI::setMeter(uint8_t level, uint8_t peakHold, bool high, bool clip) {
  setLevel(level);
  if (peakHold > 8) peakHold = 8;
  _peakHold = peakHold;
  _high = high;
  _clip = clip;
}

void PhoenixGUI::setTriggerMarker(bool enabled, uint8_t level) {
  _triggerMarkerEnabled = enabled;
  if (level < 1) level = 1;
  if (level > 8) level = 8;
  _triggerMarkerLevel = level;
}

void PhoenixGUI::setRecordStatus(uint8_t status) {
  _recordStatus = status;
}

void PhoenixGUI::setRecording(bool on) {
  _recording = on;
}

void PhoenixGUI::setPlaying(bool on) {
  _playing = on;
}

void PhoenixGUI::setWaveform(const uint8_t *bins, uint8_t count, bool valid) {
  if (!bins || count == 0 || !valid) {
    _waveValid = false;
    _waveCount = 0;
    return;
  }
  if (count > sizeof(_waveBins)) count = sizeof(_waveBins);
  memcpy(_waveBins, bins, count);
  _waveCount = count;
  _waveValid = true;
}

void PhoenixGUI::setWaveformView(uint8_t zoom, uint16_t centerQ) {
  if (zoom != 1 && zoom != 2 && zoom != 4 && zoom != 8) zoom = 1;
  if (centerQ > 10000U) centerQ = 10000U;
  _waveZoom = zoom;
  _waveCenterQ = centerQ;
}

void PhoenixGUI::begin(uint8_t contrast) {
  _d.begin();
  _d.setContrast(contrast);
}

void PhoenixGUI::clear() { _d.clearBuffer(); _d.setDrawColor(1); }
void PhoenixGUI::send()  { _d.sendBuffer(); }
void PhoenixGUI::text(int x, int y, const char *s, bool inverse) { PhoenixFont::drawText(_d, x, y, s, inverse); }
void PhoenixGUI::centered(int x, int y, int w, const char *s, bool inverse) { PhoenixFont::drawCentered(_d, x, y, w, s, inverse); }
void PhoenixGUI::frame(int x, int y, int w, int h) { _d.setDrawColor(1); _d.drawFrame(x, y, w, h); }
void PhoenixGUI::hline(int x, int y, int w) { _d.setDrawColor(1); _d.drawHLine(x, y, w); }
void PhoenixGUI::vline(int x, int y, int h) { _d.setDrawColor(1); _d.drawVLine(x, y, h); }
void PhoenixGUI::fill(int x, int y, int w, int h, bool on) { _d.setDrawColor(on ? 1 : 0); _d.drawBox(x, y, w, h); _d.setDrawColor(1); }

void PhoenixGUI::drawWindow(int x, int y, int w, int h, const char *title) {
  frame(x, y, w, h);
  centered(x + 1, y + PhoenixLayout::TITLE_Y, w - 2, title, false);
  hline(x + 1, y + PhoenixLayout::TITLE_LINE_Y, w - 2);
}

void PhoenixGUI::drawClassicLevelMeter(uint8_t level) {
  if (level == 0) level = _level;
  const int x = PhoenixLayout::LEVEL_X;
  const int y = PhoenixLayout::LEVEL_Y;
  const int w = PhoenixLayout::LEVEL_W;
  const int h = PhoenixLayout::LEVEL_H;

  drawWindow(x, y, w, h, "LEVEL");

  // RTAL LED meter: eight wide, low-profile rounded LED elements.
  const int meterY = y + 10;
  const int rowH   = 5;
  const int ledX   = x + 6;
  const int ledW   = w - 9;
  const int ledH   = 4;
  const int radius = 1;

  if (level > 8) level = 8;
  uint8_t ph = _peakHold;
  if (ph > 8) ph = 8;

  for (int i = 0; i < 8; ++i) {
    const uint8_t rowNumber = 8 - i;
    const int yy = meterY + i * rowH;
    _d.setDrawColor(1);
    if (rowNumber <= level) _d.drawRBox(ledX, yy, ledW, ledH, radius);
    else                    _d.drawRFrame(ledX, yy, ledW, ledH, radius);
  }

  // Trigger threshold marker remains independent from the level fill.
  if (_triggerMarkerEnabled) {
    uint8_t tl = _triggerMarkerLevel;
    if (tl < 1) tl = 1;
    if (tl > 8) tl = 8;
    const int trigY = meterY + (8 - (int)tl) * rowH;
    _d.drawPixel(x + 2, trigY + 1);
    _d.drawPixel(x + 3, trigY + 1);
    _d.drawPixel(x + 4, trigY + 2);
    _d.drawPixel(x + 3, trigY + 3);
    _d.drawPixel(x + 2, trigY + 3);
  }

  if (ph > 0) {
    const int holdY = meterY + (8 - (int)ph) * rowH;
    _d.drawVLine(ledX + ledW + 1, holdY, ledH);
  }

  // RTAL Status Box. One concise state is always visible.
  const char *status = "READY";
  if (_clip)                         status = "CLIP";
  else if (_recordStatus == 2 || _recording) status = "REC";
  else if (_recordStatus == 1)       status = "WAIT";
  else if (_playing)                 status = "PLAY";
  else if (_triggerMarkerEnabled)    status = "TRIG";
  else if (_high)                    status = "HIGH";

  PhoenixWidgets::drawStatusBox(_d, x + 1, y + h - 8, w - 2, 8, status);
}

void PhoenixGUI::drawClassicHelp(const char *left, const char *mid, const char *right) {
  PhoenixWidgets::drawFooter(_d, PhoenixLayout::HELP_Y, left, mid, right);
}

void PhoenixGUI::drawClassicMenu(const char *title, const char * const *items, uint8_t count, uint8_t selected, bool withLevel) {
  if (withLevel) {
    drawClassicLevelMeter(_level);
  }

  const int x = withLevel ? PhoenixLayout::WIN_X : 10;
  const int y = PhoenixLayout::WIN_Y;
  const int w = withLevel ? PhoenixLayout::WIN_W : 112;
  const int h = PhoenixLayout::WIN_H;

  drawWindow(x, y, w, h, title);

  const int listX = x + PhoenixLayout::MENU_X_PAD;
  const int listY = y + PhoenixLayout::MENU_FIRST_Y;
  const int rowH  = PhoenixLayout::MENU_ROW_H;

  uint8_t first = 0;
  if (count > 8 && selected >= 8) first = selected - 7;
  for (uint8_t r = 0; r < count && r < 8; ++r) {
    uint8_t i = first + r;
    if (i >= count) break;
    int yy = listY + r * rowH;
    bool sel = (i == selected);
    // v0.7.2: no inverted menu rows. A Phoenix arrow marks selection.
    // This is calmer on OLED and removes underline/block artefacts globally.
    text(listX, yy, sel ? ">" : " ", false);
    text(listX + 5, yy, items[i], false);
  }
}

void PhoenixGUI::drawBoot() {
  clear();
  frame(0, 0, 128, 64);
  centered(0, 10, 128, "PHOENIX", false);
  centered(0, 25, 128, "(C) RealTimeAudioLab", false);
  centered(0, 36, 128, "Phoenix Sound Sampler", false);
  centered(0, 48, 128, "Version 1.0", false);
  send();
}

void PhoenixGUI::drawClassicWaveform(const char *title, uint16_t playPosQ) {
  drawClassicLevelMeter(_level);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  drawWindow(x, y, w, h, title);

  const int ox = x + 4;
  const int oy = y + 14;
  const int ow = w - 8;
  const int oh = 28;
  const int mid = oy + oh / 2;
  frame(ox, oy, ow, oh);
  hline(ox + 1, mid, ow - 2);

  uint32_t viewStartQ = 0U;
  uint32_t viewEndQ = 10000U;
  if (_waveZoom > 1U) {
    uint32_t viewSpanQ = 10000U / _waveZoom;
    const uint32_t centerQ = _waveCenterQ;
    if (centerQ > viewSpanQ / 2U) viewStartQ = centerQ - viewSpanQ / 2U;
    if (viewStartQ + viewSpanQ > 10000U) viewStartQ = 10000U - viewSpanQ;
    viewEndQ = viewStartQ + viewSpanQ;
  }

  if (_waveValid && _waveCount > 0U) {
    const int drawW = ow - 2;
    const uint32_t viewSpanQ = viewEndQ - viewStartQ;
    for (int i = 0; i < drawW; ++i) {
      const uint32_t q = (drawW > 1)
                       ? viewStartQ + ((uint32_t)i * viewSpanQ) / (uint32_t)(drawW - 1)
                       : viewStartQ;
      uint16_t idx = (uint16_t)((q * (uint32_t)(_waveCount - 1U)) / 10000UL);
      if (idx >= _waveCount) idx = _waveCount - 1U;
      uint8_t bin = _waveBins[idx];
      if (bin > 15U) bin = 15U;
      const int amp = (bin * (oh / 2 - 3)) / 15;
      const int xx = ox + 1 + i;
      if (amp <= 0) _d.drawPixel(xx, mid);
      else vline(xx, mid - amp, 2 * amp + 1);
    }
  } else {
    for (int i = 0; i < ow - 2; ++i) {
      int xx = ox + 1 + i;
      int v = ((i * 7 + (i >> 1) * 3 + (i * i >> 4)) % 17) - 8;
      int yy = mid + v;
      _d.drawPixel(xx, yy);
      if ((i & 7) == 0) _d.drawPixel(xx, yy + (v > 0 ? -1 : 1));
    }
  }

  if (playPosQ > 10000U) playPosQ = 10000U;
  int px;
  if (playPosQ <= viewStartQ) px = ox + 1;
  else if (playPosQ >= viewEndQ) px = ox + ow - 3;
  else px = ox + 1 + (int)(((uint32_t)(playPosQ - viewStartQ) * (uint32_t)(ow - 4)) /
                            (viewEndQ - viewStartQ));
  if (px < ox + 1) px = ox + 1;
  if (px > ox + ow - 3) px = ox + ow - 3;
  vline(px, oy + 2, oh - 4);
  if (px + 1 < ox + ow - 1) vline(px + 1, oy + 2, oh - 4);

  text(x + 5, y + 47, "START 0000", false);
  if (_waveValid) text(x + 56, y + 47, "END SAMPLE", false);
  else text(x + 56, y + 47, "NO SAMPLE", false);
}


void PhoenixGUI::drawSampleEditorWaveform(int x, int y, int w, int h,
                                              uint16_t sampleStartQ, uint16_t loopStartQ,
                                              uint16_t loopEndQ, uint16_t sampleEndQ,
                                              uint16_t cursorQ, uint16_t playQ, bool playing,
                                              uint8_t selectedMarker) {
  frame(x, y, w, h);
  const int innerX = x + 1;
  const int innerY = y + 1;
  const int innerW = w - 2;
  const int innerH = h - 2;
  const int mid = innerY + innerH / 2;
  hline(innerX, mid, innerW);

  // C015: derive one common view window for waveform, markers and playhead.
  // Earlier zoom versions enlarged the waveform but still positioned markers
  // in full-sample coordinates, so the graphics no longer matched at X2/X4/X8.
  uint32_t viewStartQ = 0U;   // 0.01 percent units
  uint32_t viewEndQ = 10000U;
  if (_waveZoom > 1U) {
    uint32_t viewSpanQ = 10000U / _waveZoom;
    if (viewSpanQ < 1U) viewSpanQ = 1U;
    const uint32_t centerQ = _waveCenterQ;
    if (centerQ > viewSpanQ / 2U) viewStartQ = centerQ - viewSpanQ / 2U;
    if (viewStartQ + viewSpanQ > 10000U) viewStartQ = 10000U - viewSpanQ;
    viewEndQ = viewStartQ + viewSpanQ;
  }

  if (_waveValid && _waveCount > 0U && innerW > 0) {
    const uint32_t viewSpanQ = viewEndQ - viewStartQ;
    for (int i = 0; i < innerW; ++i) {
      const uint32_t q = (innerW > 1)
                       ? viewStartQ + ((uint32_t)i * viewSpanQ) / (uint32_t)(innerW - 1)
                       : viewStartQ;
      uint16_t idx = (uint16_t)((q * (uint32_t)(_waveCount - 1U)) / 10000UL);
      if (idx >= _waveCount) idx = _waveCount - 1U;
      uint8_t bin = _waveBins[idx];
      if (bin > 15U) bin = 15U;
      const int amp = (bin * (innerH / 2 - 2)) / 15;
      const int xx = innerX + i;
      if (amp <= 0) _d.drawPixel(xx, mid);
      else vline(xx, mid - amp, 2 * amp + 1);
    }
  }

  auto qToX = [&](uint16_t q) -> int {
    if (q > 10000U) q = 10000U;
    if ((uint32_t)q <= viewStartQ) return innerX;
    if ((uint32_t)q >= viewEndQ) return innerX + innerW - 1;
    const uint32_t span = viewEndQ - viewStartQ;
    return innerX + (int)((((uint32_t)q - viewStartQ) * (uint32_t)(innerW - 1)) / span);
  };

  const int ssx = qToX(sampleStartQ);
  const int lsx = qToX(loopStartQ);
  const int lex = qToX(loopEndQ);
  const int sex = qToX(sampleEndQ);
  const int cx = qToX(cursorQ);
  const int px = qToX(playQ);

  // Outer sample boundaries use triangles; loop boundaries use T-shaped lines.
  _d.drawTriangle(ssx - 3, innerY + 1, ssx + 3, innerY + 1, ssx, innerY + 5);
  _d.drawTriangle(sex - 3, innerY + innerH - 2, sex + 3, innerY + innerH - 2, sex, innerY + innerH - 6);
  vline(lsx, innerY + 1, innerH - 2);
  hline(lsx - 2, innerY + 1, 5);
  vline(lex, innerY + 1, innerH - 2);
  hline(lex - 2, innerY + innerH - 2, 5);

  // Selected marker: a two-pixel line plus caps remains visible even when
  // several marker positions coincide at a zoom-window edge.
  vline(cx, innerY + 1, innerH - 2);
  if (cx + 1 < innerX + innerW) vline(cx + 1, innerY + 1, innerH - 2);
  const bool selectedAtTop = ((selectedMarker & 3U) <= 1U);
  const int capY = selectedAtTop ? innerY : (innerY + innerH - 2);
  const int capX = (cx <= innerX) ? innerX : ((cx >= innerX + innerW - 1) ? innerX + innerW - 3 : cx - 1);
  _d.drawBox(capX, capY, 3, 2);

  // Playback cursor from F7 or newest active MIDI voice.
  if (playing) {
    vline(px, innerY + 1, innerH - 2);
    if (px + 1 < innerX + innerW) vline(px + 1, innerY + 1, innerH - 2);
  }
}

void PhoenixGUI::drawClassicKeyboard(uint8_t sampleNo) {
  drawClassicLevelMeter(_level);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  drawWindow(x, y, w, h, "QUATTRO KEYBOARD");

  char s[18];
  snprintf(s, sizeof(s), "SAMPLE #%u", (unsigned)sampleNo);
  centered(x + 1, y + 12, w - 2, s, false);
  text(x + 8,  y + 23, "1 2 3 4", false);
  text(x + 8,  y + 31, "Q W E R T Y U I", false);
  text(x + 8,  y + 39, "A S D F G H J K", false);
  text(x + 8,  y + 47, "Z X C V B N M", false);
}

void PhoenixGUI::drawClassicSequencer(uint8_t step) {
  drawClassicLevelMeter(_level);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  drawWindow(x, y, w, h, "QUATTRO SEQUENCER");

  text(x + 5, y + 13, "1 2 3 4 5 6 7 8", false);
  for (uint8_t i = 0; i < 8; ++i) {
    int sx = x + 5 + i * 11;
    int sy = y + 23;
    frame(sx, sy, 8, 8);
    if (i == step) fill(sx + 2, sy + 2, 4, 4, true);
  }
  text(x + 5, y + 38, "PROGRAM MODE", false);
  text(x + 5, y + 47, "TEMPO 120", false);
}

void PhoenixGUI::drawDialog(const char *title, const char * const *lines, uint8_t count, const char *footer) {
  // PhoenixGUI 2.3 DialogWindow style.
  // All spacing is controlled in PhoenixMetrics.h so later fine tuning
  // does not require touching rendering logic.
  const int x = PhoenixMetrics::DIALOG_X;
  const int y = PhoenixMetrics::DIALOG_Y;
  const int w = PhoenixMetrics::DIALOG_W;
  const int h = PhoenixMetrics::DIALOG_H;
  const int m = PhoenixMetrics::DIALOG_CLEAR_MARGIN;

  // Clear slightly larger background so the dialog behaves like a modal overlay.
  fill(x - m, y - m, w + 2 * m, h + 2 * m, false);
  frame(x, y, w, h);

  // Header uses the same visual language as all classic windows.
  centered(x + 1, y + PhoenixMetrics::DIALOG_TITLE_Y, w - 2, title, false);
  hline(x + 1, y + PhoenixMetrics::DIALOG_TITLE_LINE_Y, w - 2);

  // Footer area: one separated footer band with explicit top and bottom margins.
  const int footerLineY = y + h - PhoenixMetrics::DIALOG_FOOTER_LINE_FROM_BOTTOM;
  hline(x + 1, footerLineY, w - 2);

  // Body lines. Slightly larger 7 px pitch gives the dialog more air than 2.2.
  const int bodyY = y + PhoenixMetrics::DIALOG_BODY_Y;
  for (uint8_t i = 0; i < count && i < 4; ++i) {
    centered(x + 1, bodyY + i * PhoenixMetrics::DIALOG_BODY_PITCH, w - 2, lines[i], false);
  }

  // Footer text is still inside the frame, with visible bottom breathing room.
  if (footer && footer[0]) {
    centered(x + 1, y + h - PhoenixMetrics::DIALOG_FOOTER_TEXT_FROM_BOTTOM, w - 2, footer, false);
  }
}

void PhoenixGUI::drawAboutPopup() {
  // v0.7.35b: dedicated Phoenix 1.0 product information screen.
  // The layout deliberately uses the complete OLED instead of the generic
  // dialog body so all requested lines remain centered and readable.
  const int x = 1, y = 1, w = 126, h = 62;
  fill(0, 0, 128, 64, false);
  frame(x, y, w, h);

  centered(x + 1, 5,  w - 2, "PHOENIX", false);
  centered(x + 1, 16, w - 2, "(C) REALTIMEAUDIOLAB", false);
  centered(x + 1, 25, w - 2, "PHOENIX SOUND SAMPLER", false);
  centered(x + 1, 34, w - 2, "2026", false);
  centered(x + 1, 41, w - 2, "VERSION 1.0", false);

  hline(x + 1, 54, w - 2);
  centered(x + 1, 57, w - 2, "F8 EXIT", false);
}

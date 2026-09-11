/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "PhoenixFont.h"
#include "PhoenixLayout.h"
#include "PhoenixMetrics.h"
#include "PhoenixWidgets.h"

class PhoenixGUI {
public:
  explicit PhoenixGUI(U8G2 &display);

  void setLevel(uint8_t level);
  void setMeter(uint8_t level, uint8_t peakHold, bool high, bool clip);
  void setTriggerMarker(bool enabled, uint8_t level);
  void setRecordStatus(uint8_t status); // 0 none, 1 WAIT, 2 REC
  void setRecording(bool on);
  void setPlaying(bool on);
  void setWaveform(const uint8_t *bins, uint8_t count, bool valid);
  void setWaveformView(uint8_t zoom, uint16_t centerQ);

  void begin(uint8_t contrast = 190);
  void clear();
  void send();

  void text(int x, int y, const char *s, bool inverse = false);
  void centered(int x, int y, int w, const char *s, bool inverse = false);
  void frame(int x, int y, int w, int h);
  void fill(int x, int y, int w, int h, bool on = true);
  void hline(int x, int y, int w);
  void vline(int x, int y, int h);

  void drawWindow(int x, int y, int w, int h, const char *title);
  void drawClassicLevelMeter(uint8_t level);
  void drawClassicHelp(const char *left, const char *mid, const char *right);
  void drawClassicMenu(const char *title, const char * const *items, uint8_t count, uint8_t selected, bool withLevel = true);
  void drawClassicWaveform(const char *title, uint16_t playPosQ);
  void drawSampleEditorWaveform(int x, int y, int w, int h, uint16_t sampleStartQ, uint16_t loopStartQ, uint16_t loopEndQ, uint16_t sampleEndQ, uint16_t cursorQ, uint16_t playQ, bool playing, uint8_t selectedMarker);
  void drawClassicKeyboard(uint8_t sampleNo);
  void drawClassicSequencer(uint8_t step);
  void drawDialog(const char *title, const char * const *lines, uint8_t count, const char *footer);
  void drawAboutPopup();
  void drawBoot();

private:
  U8G2 &_d;
  uint8_t _level;
  uint8_t _peakHold;
  bool _high;
  bool _clip;
  bool _recording;
  bool _playing;
  bool _triggerMarkerEnabled;
  uint8_t _triggerMarkerLevel;
  uint8_t _recordStatus;
  uint8_t _waveBins[96];
  uint8_t _waveCount;
  bool _waveValid;
  uint8_t _waveZoom;
  uint16_t _waveCenterQ;
};

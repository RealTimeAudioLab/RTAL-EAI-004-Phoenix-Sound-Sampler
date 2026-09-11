/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>

enum PhoenixMidiMonitorType : uint8_t {
  PHX_MMON_NONE = 0,
  PHX_MMON_NOTE_ON,
  PHX_MMON_NOTE_OFF,
  PHX_MMON_CC,
  PHX_MMON_PROGRAM,
  PHX_MMON_PITCH_BEND,
  PHX_MMON_CHANNEL_PRESSURE,
  PHX_MMON_CLOCK,
  PHX_MMON_START,
  PHX_MMON_CONTINUE,
  PHX_MMON_STOP
};

struct PhoenixMidiMonitorSnapshot {
  PhoenixMidiMonitorType type;
  uint8_t channel;
  uint8_t data1;
  int16_t value;
  uint32_t counter;       // visible monitor events only
  uint32_t timestampMs;   // last visible monitor event
  uint32_t clockCounter;  // all received MIDI clock ticks
  uint32_t clockAgeMs;    // age of last clock tick
  uint16_t clockBpm10;    // BPM x 10, 0 while not yet stable
  bool clockPresent;
  bool clockVisible;
};

void phxMidiMonitorRecord(PhoenixMidiMonitorType type, uint8_t channel, uint8_t data1, int16_t value);
PhoenixMidiMonitorSnapshot phxMidiMonitorSnapshot();
bool phxMidiActivityActive(uint32_t nowMs = millis());

// v0.7.41d: clock ticks are hidden by default, but still measured.
bool phxMidiMonitorClockVisible();
void phxMidiMonitorSetClockVisible(bool visible);
void phxMidiMonitorToggleClockVisible();

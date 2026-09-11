/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>

class PhoenixSongManager {
public:
  static constexpr uint8_t kMaxEntries = 16;
  enum LoopMode : uint8_t { LOOP_OFF = 0, LOOP_SONG = 1, LOOP_PATTERN = 2 };

  struct SongEntry {
    uint8_t pattern;   // 0..3 = P1..P4
    uint8_t repeats;   // 1..16
    bool end;
    uint8_t reserved;
  };

  PhoenixSongManager();

  void beginDefaultSong();
  void beginTestSong();
  void start();
  void stop();
  void pause();
  void continuePlay();

  // Called exactly once at a musical pattern boundary.
  // Returns true when another pattern must start and writes its index.
  // Returns false when END was reached and transport must stop.
  bool onPatternFinished(uint8_t &nextPattern);

  bool playing() const { return _playing; }
  uint8_t position() const { return _position; }
  uint8_t repeatIndex() const { return _repeatIndex; }
  uint8_t currentPattern() const;
  uint8_t currentRepeatTarget() const;

  const SongEntry &entry(uint8_t index) const { return _entries[index < kMaxEntries ? index : 0]; }
  void setEntry(uint8_t index, uint8_t pattern, uint8_t repeats, bool end);
  void insertEntry(uint8_t index);
  void deleteEntry(uint8_t index);

  void setLoopMode(uint8_t mode, uint8_t start, uint8_t end);
  void setLoop(bool enabled, uint8_t start, uint8_t end) { setLoopMode(enabled ? LOOP_SONG : LOOP_OFF, start, end); }
  uint8_t loopMode() const { return _loopMode; }
  bool loopEnabled() const { return _loopMode != LOOP_OFF; }
  uint8_t loopStart() const { return _loopStart; }
  uint8_t loopEnd() const { return _loopEnd; }

  bool saveConfig(const char *bankDir) const;
  bool loadConfig(const char *bankDir);

  void queuePattern(uint8_t pattern);
  void clearQueue();

private:
  SongEntry _entries[kMaxEntries];
  bool _playing;
  uint8_t _position;
  uint8_t _repeatIndex;
  bool _queued;
  uint8_t _queuedPattern;
  uint8_t _loopMode;
  uint8_t _loopStart;
  uint8_t _loopEnd;

  void sanitizeEntry(SongEntry &entry);
  void sanitizeLoop();
};

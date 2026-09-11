/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include "../PhoenixSampling/PhoenixSampleAnalysis.h"
#include "../PhoenixAudio/PhoenixAudioManager.h"

// Project Phoenix v0.7.43a
// Serial-only Smart Workflow report. This module does not alter audio data,
// analysis decisions, display classes, or the SAFE/SMART/FORCE mode logic.
class PhoenixSmartReport {
public:
  PhoenixSmartReport();

  void clear();

  void buildProcessed(uint8_t slot,
                      const char *modeName,
                      const char *qualityName,
                      const PhoenixSampleAnalysisResult &analysis,
                      const PhoenixAudioManager::SmartProcessResult &processing);

  void buildProtected(uint8_t slot,
                      const char *modeName,
                      const char *qualityName,
                      const PhoenixSampleAnalysisResult &analysis);

  void buildFailed(uint8_t slot,
                   const char *modeName,
                   const char *qualityName,
                   const PhoenixSampleAnalysisResult &analysis);

  void printSerial() const;

private:
  enum Action : uint8_t {
    ACTION_NONE = 0,
    ACTION_PROCESSED,
    ACTION_PROTECTED,
    ACTION_FAILED
  };

  uint8_t _slot;
  const char *_modeName;
  const char *_qualityName;
  Action _action;

  uint8_t _qualityScore;
  uint8_t _confidence;
  uint8_t _peakBeforePercent;
  uint8_t _peakAfterPercent;
  int16_t _dcOffsetBefore;
  uint32_t _analysisTimeMs;

  bool _trimApplied;
  bool _dcRemoved;
  bool _normalized;
  bool _dataChanged;

  uint32_t _originalFrames;
  uint32_t _finalFrames;
  uint32_t _removedLeadingFrames;
  uint32_t _removedTrailingFrames;
  int16_t _removedDcOffset;
  uint32_t _normalizeGainX1000;

  static uint8_t peakPercentFromAbsolute(uint16_t peak);
  static int16_t gainDbX10(uint32_t gainX1000);
  static uint32_t savedFrames(uint32_t originalFrames, uint32_t finalFrames);
  static uint8_t reductionPercent(uint32_t originalFrames, uint32_t finalFrames);
  static void printSignedTenthsDb(int16_t dbX10);
};

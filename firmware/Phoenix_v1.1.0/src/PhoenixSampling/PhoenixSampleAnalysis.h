#pragma once
#include <Arduino.h>

// Project Phoenix v0.7.42g
// Smart Trim Safety Engine. Analysis remains fully non-destructive.
struct PhoenixSampleAnalysisResult {
  bool valid;
  uint32_t sampleCount;
  uint32_t suggestedStart;
  uint32_t suggestedEnd;   // exclusive
  uint32_t leadingSilenceFrames;
  uint32_t trailingSilenceFrames;
  uint32_t analysisTimeMs;
  uint16_t absolutePeak;
  uint16_t rms;
  uint16_t noiseFloor;
  uint16_t detectionThreshold;
  uint16_t crestFactorX100;
  int16_t dcOffset;
  uint8_t peakPercent;
  uint8_t qualityScore;         // recording quality, 1..5
  uint8_t trimConfidence;       // trim proposal confidence, 0..100
  bool clipped;
  bool dcDetected;
  bool normalizeRecommended;
  bool noiseValid;              // stable, sufficiently quiet noise cluster found
  bool startConfirmed;          // safe leading-silence proposal
  bool endConfirmed;            // safe trailing-silence proposal
  bool startOpen;               // signal reaches recording start
  bool endOpen;                 // signal reaches recording end / natural tail protected

  PhoenixSampleAnalysisResult()
  : valid(false), sampleCount(0), suggestedStart(0), suggestedEnd(0),
    leadingSilenceFrames(0), trailingSilenceFrames(0), analysisTimeMs(0),
    absolutePeak(0), rms(0), noiseFloor(0), detectionThreshold(0),
    crestFactorX100(0), dcOffset(0), peakPercent(0), qualityScore(1),
    trimConfidence(0), clipped(false), dcDetected(false),
    normalizeRecommended(false), noiseValid(false), startConfirmed(false),
    endConfirmed(false), startOpen(true), endOpen(true) {}
};

class PhoenixSampleAnalysis {
public:
  // thresholdPermille is the minimum peak-relative RMS threshold
  // (default 15 = 1.5%). guardFrames preserves transients.
  static bool analyze(const int16_t *buffer,
                      uint32_t sampleCount,
                      PhoenixSampleAnalysisResult &result,
                      uint16_t thresholdPermille = 15,
                      uint16_t guardFrames = 96);
};

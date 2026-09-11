/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "PhoenixSampleAnalysis.h"
#include <math.h>

static inline uint16_t phxAbs16(int16_t v) {
  if (v == INT16_MIN) return 32768U;
  return (uint16_t)(v < 0 ? -v : v);
}

static inline uint32_t phxMinU32(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static inline uint32_t phxMaxU32(uint32_t a, uint32_t b) {
  return a > b ? a : b;
}

static uint16_t phxWindowRms(const int16_t *buffer, uint32_t base, uint32_t count) {
  if (!buffer || count == 0U) return 0U;
  uint64_t sumSquares = 0ULL;
  for (uint32_t i = 0; i < count; ++i) {
    const int32_t s = buffer[base + i];
    sumSquares += (uint64_t)((int64_t)s * (int64_t)s);
  }
  const double mean = (double)sumSquares / (double)count;
  const double r = sqrt(mean);
  return (uint16_t)(r > 32768.0 ? 32768U : (uint32_t)r);
}

static void phxSortU16(uint16_t *values, uint16_t count) {
  for (uint16_t i = 1U; i < count; ++i) {
    const uint16_t key = values[i];
    int j = (int)i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

static uint32_t phxFindZeroCrossing(const int16_t *buffer,
                                    uint32_t sampleCount,
                                    uint32_t center,
                                    uint16_t radius) {
  if (!buffer || sampleCount < 2U) return center;
  if (center >= sampleCount) center = sampleCount - 1U;

  uint32_t best = center;
  uint16_t bestAbs = phxAbs16(buffer[center]);
  const uint32_t lo = center > radius ? center - radius : 1U;
  const uint32_t hi = phxMinU32(sampleCount - 1U, center + (uint32_t)radius);

  for (uint32_t i = lo; i <= hi; ++i) {
    const int16_t a = buffer[i - 1U];
    const int16_t b = buffer[i];
    const bool crossing = (a <= 0 && b >= 0) || (a >= 0 && b <= 0);
    if (crossing) {
      const uint16_t aa = phxAbs16(a);
      const uint16_t bb = phxAbs16(b);
      const uint16_t magnitude = aa < bb ? aa : bb;
      if (magnitude < bestAbs) {
        bestAbs = magnitude;
        best = i;
        if (bestAbs == 0U) break;
      }
    }
  }
  return best;
}

static uint8_t phxQualityScore(const PhoenixSampleAnalysisResult &r) {
  uint8_t score = 5U;
  if (r.clipped) score = score > 2U ? score - 2U : 1U;
  if (phxAbs16(r.dcOffset) >= 328U && score > 1U) --score;
  if (r.peakPercent < 10U && score > 1U) --score;
  if (r.peakPercent > 98U && score > 1U) --score;
  if (r.noiseValid && r.noiseFloor > 800U && score > 1U) --score;
  if (score < 1U) score = 1U;
  return score;
}

bool PhoenixSampleAnalysis::analyze(const int16_t *buffer,
                                    uint32_t sampleCount,
                                    PhoenixSampleAnalysisResult &result,
                                    uint16_t thresholdPermille,
                                    uint16_t guardFrames) {
  const uint32_t startedMs = millis();
  result = PhoenixSampleAnalysisResult();
  if (!buffer || sampleCount == 0U) return false;

  if (thresholdPermille < 1U) thresholdPermille = 1U;
  if (thresholdPermille > 250U) thresholdPermille = 250U;

  int64_t sum = 0;
  uint64_t sumSquares = 0ULL;
  uint16_t peak = 0U;

  for (uint32_t i = 0; i < sampleCount; ++i) {
    const int16_t s = buffer[i];
    const uint16_t a = phxAbs16(s);
    sum += (int32_t)s;
    sumSquares += (uint64_t)((int64_t)s * (int64_t)s);
    if (a > peak) peak = a;
  }

  result.valid = true;
  result.sampleCount = sampleCount;
  result.absolutePeak = peak;
  result.peakPercent = (uint8_t)phxMinU32(100U,
      ((uint32_t)peak * 100U + 16383U) / 32767U);
  result.dcOffset = (int16_t)(sum / (int64_t)sampleCount);
  result.rms = (uint16_t)phxMinU32(32768U,
      (uint32_t)sqrt((double)sumSquares / (double)sampleCount));
  result.crestFactorX100 = result.rms > 0U
      ? (uint16_t)phxMinU32(65535U, ((uint32_t)peak * 100U) / result.rms)
      : 0U;
  result.clipped = peak >= 32760U;
  result.dcDetected = phxAbs16(result.dcOffset) >= 164U;
  result.normalizeRecommended = peak > 0U && result.peakPercent < 90U;

  const uint16_t windowFrames = 64U;
  const uint32_t windowCount = (sampleCount + windowFrames - 1U) / windowFrames;
  uint16_t quietSamples[256];
  uint16_t quietCount = 0U;
  const uint32_t sampleStride = windowCount > 256U
      ? (windowCount + 255U) / 256U : 1U;

  for (uint32_t w = 0U; w < windowCount && quietCount < 256U; w += sampleStride) {
    const uint32_t base = w * windowFrames;
    const uint32_t count = phxMinU32(windowFrames, sampleCount - base);
    quietSamples[quietCount++] = phxWindowRms(buffer, base, count);
  }

  uint16_t q10 = 0U;
  uint16_t q25 = 0U;
  if (quietCount > 0U) {
    phxSortU16(quietSamples, quietCount);
    const uint16_t q10Index = (uint16_t)(((uint32_t)(quietCount - 1U) * 10U) / 100U);
    const uint16_t q25Index = (uint16_t)(((uint32_t)(quietCount - 1U) * 25U) / 100U);
    q10 = quietSamples[q10Index];
    q25 = quietSamples[q25Index];
    result.noiseFloor = q10;
  }

  // A noise estimate is trusted only when the quiet quartile forms a compact,
  // genuinely quiet cluster. This prevents musical decay from becoming "noise".
  const uint32_t maxNoiseForSignal = phxMaxU32(96U, (uint32_t)result.rms / 10U);
  const bool compactQuietCluster = q25 <= ((uint32_t)q10 * 2U + 32U);
  result.noiseValid = quietCount >= 16U &&
                      q10 <= maxNoiseForSignal &&
                      compactQuietCluster;

  const uint32_t peakThreshold = phxMaxU32(48U,
      ((uint32_t)peak * thresholdPermille) / 1000U);
  const uint32_t noiseThreshold = (uint32_t)result.noiseFloor * 4U + 16U;
  uint32_t threshold = result.noiseValid
      ? phxMaxU32(peakThreshold, noiseThreshold)
      : peakThreshold;
  if (peak > 0U && threshold >= peak) threshold = phxMaxU32(1U, (uint32_t)peak / 2U);
  result.detectionThreshold = (uint16_t)phxMinU32(32767U, threshold);

  // First/last active content from a 64-frame RMS envelope.
  const uint8_t requiredActiveWindows = 2U;
  uint32_t firstActiveWindow = sampleCount;
  uint32_t lastActiveWindowEnd = 0U;
  uint8_t activeRun = 0U;

  // Edge silence runs are evaluated separately. End trimming is intentionally
  // conservative: at least 200 ms of continuous silence is required.
  uint32_t leadingInactiveWindows = 0U;
  uint32_t trailingInactiveWindows = 0U;
  bool leadingRunOpen = true;

  for (uint32_t w = 0U; w < windowCount; ++w) {
    const uint32_t base = w * windowFrames;
    const uint32_t count = phxMinU32(windowFrames, sampleCount - base);
    const uint16_t envelopeRms = phxWindowRms(buffer, base, count);
    const bool active = envelopeRms >= result.detectionThreshold;

    if (leadingRunOpen) {
      if (active) leadingRunOpen = false;
      else ++leadingInactiveWindows;
    }

    if (active) {
      trailingInactiveWindows = 0U;
      if (activeRun < 255U) ++activeRun;
      if (activeRun == requiredActiveWindows && firstActiveWindow == sampleCount) {
        firstActiveWindow = base - (uint32_t)(requiredActiveWindows - 1U) * windowFrames;
      }
      if (activeRun >= requiredActiveWindows) lastActiveWindowEnd = base + count;
    } else {
      activeRun = 0U;
      ++trailingInactiveWindows;
    }
  }

  const uint32_t startConfirmFrames = 160U;   // 5 ms at 32 kHz
  const uint32_t endConfirmFrames = 6400U;    // 200 ms at 32 kHz
  const uint32_t leadingQuietFrames = leadingInactiveWindows * (uint32_t)windowFrames;
  const uint32_t trailingQuietFrames = trailingInactiveWindows * (uint32_t)windowFrames;

  result.startConfirmed = firstActiveWindow != sampleCount &&
                          leadingQuietFrames >= startConfirmFrames;
  result.endConfirmed = lastActiveWindowEnd > 0U &&
                        trailingQuietFrames >= endConfirmFrames;
  result.startOpen = !result.startConfirmed;
  result.endOpen = !result.endConfirmed;

  uint32_t start = 0U;
  uint32_t end = sampleCount;

  if (firstActiveWindow != sampleCount && lastActiveWindowEnd > firstActiveWindow) {
    // Start proposals remain useful with a short, clearly detected pre-roll.
    if (result.startConfirmed) {
      start = firstActiveWindow > guardFrames ? firstActiveWindow - guardFrames : 0U;
      start = phxFindZeroCrossing(buffer, sampleCount, start, 200U);
    }

    // Never shorten a natural or uncertain release. Only a long, confirmed
    // trailing silence may produce an end trim proposal.
    if (result.endConfirmed) {
      end = phxMinU32(sampleCount, lastActiveWindowEnd + (uint32_t)guardFrames);
      if (end < sampleCount) end = phxFindZeroCrossing(buffer, sampleCount, end, 200U);
    }
  }

  if (end <= start) {
    start = 0U;
    end = sampleCount;
    result.startConfirmed = false;
    result.endConfirmed = false;
    result.startOpen = true;
    result.endOpen = true;
  }

  result.suggestedStart = start;
  result.suggestedEnd = end;
  result.leadingSilenceFrames = start;
  result.trailingSilenceFrames = sampleCount - end;

  uint16_t confidence = 20U;
  if (result.noiseValid) confidence += 25U;
  if (result.startConfirmed) confidence += 20U;
  if (result.endConfirmed) confidence += 25U;
  if (!result.clipped) confidence += 5U;
  if (!result.dcDetected) confidence += 5U;
  result.trimConfidence = (uint8_t)phxMinU32(100U, confidence);

  result.qualityScore = phxQualityScore(result);
  result.analysisTimeMs = millis() - startedMs;
  return true;
}

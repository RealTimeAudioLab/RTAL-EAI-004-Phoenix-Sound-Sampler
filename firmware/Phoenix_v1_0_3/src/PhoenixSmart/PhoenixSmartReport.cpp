#include "PhoenixSmartReport.h"
#include <math.h>

PhoenixSmartReport::PhoenixSmartReport() {
  clear();
}

void PhoenixSmartReport::clear() {
  _slot = 0U;
  _modeName = "UNKNOWN";
  _qualityName = "UNKNOWN";
  _action = ACTION_NONE;
  _qualityScore = 0U;
  _confidence = 0U;
  _peakBeforePercent = 0U;
  _peakAfterPercent = 0U;
  _dcOffsetBefore = 0;
  _analysisTimeMs = 0U;
  _trimApplied = false;
  _dcRemoved = false;
  _normalized = false;
  _dataChanged = false;
  _originalFrames = 0U;
  _finalFrames = 0U;
  _removedLeadingFrames = 0U;
  _removedTrailingFrames = 0U;
  _removedDcOffset = 0;
  _normalizeGainX1000 = 1000U;
}

uint8_t PhoenixSmartReport::peakPercentFromAbsolute(uint16_t peak) {
  if (peak >= 32767U) return 100U;
  return (uint8_t)(((uint32_t)peak * 100U + 16383U) / 32767U);
}

int16_t PhoenixSmartReport::gainDbX10(uint32_t gainX1000) {
  if (gainX1000 == 0U) return 0;
  const double gain = (double)gainX1000 / 1000.0;
  const double dbX10 = 200.0 * log10(gain);
  if (dbX10 >= 32767.0) return 32767;
  if (dbX10 <= -32768.0) return -32768;
  return (int16_t)(dbX10 >= 0.0 ? dbX10 + 0.5 : dbX10 - 0.5);
}

uint32_t PhoenixSmartReport::savedFrames(uint32_t originalFrames, uint32_t finalFrames) {
  return originalFrames > finalFrames ? originalFrames - finalFrames : 0U;
}

uint8_t PhoenixSmartReport::reductionPercent(uint32_t originalFrames, uint32_t finalFrames) {
  if (originalFrames == 0U || finalFrames >= originalFrames) return 0U;
  const uint32_t saved = originalFrames - finalFrames;
  const uint32_t pct = (saved * 100U + originalFrames / 2U) / originalFrames;
  return (uint8_t)(pct > 100U ? 100U : pct);
}

void PhoenixSmartReport::printSignedTenthsDb(int16_t dbX10) {
  const bool negative = dbX10 < 0;
  const uint16_t magnitude = (uint16_t)(negative ? -(int32_t)dbX10 : dbX10);
  Serial.printf("%c%u.%u dB", negative ? '-' : '+',
                (unsigned)(magnitude / 10U),
                (unsigned)(magnitude % 10U));
}

void PhoenixSmartReport::buildProcessed(
    uint8_t slot,
    const char *modeName,
    const char *qualityName,
    const PhoenixSampleAnalysisResult &analysis,
    const PhoenixAudioManager::SmartProcessResult &processing) {
  clear();
  _slot = slot & 3U;
  _modeName = modeName ? modeName : "UNKNOWN";
  _qualityName = qualityName ? qualityName : "UNKNOWN";
  _action = processing.success ? ACTION_PROCESSED : ACTION_FAILED;
  _qualityScore = analysis.qualityScore;
  _confidence = analysis.trimConfidence;
  _peakBeforePercent = analysis.peakPercent;
  _peakAfterPercent = peakPercentFromAbsolute(processing.finalPeak);
  _dcOffsetBefore = analysis.dcOffset;
  _analysisTimeMs = analysis.analysisTimeMs;
  _trimApplied = processing.trimApplied;
  _dcRemoved = processing.dcRemoved;
  _normalized = processing.normalized;
  _dataChanged = processing.dataChanged;
  _originalFrames = processing.originalFrames;
  _finalFrames = processing.finalFrames;
  _removedLeadingFrames = processing.removedLeadingFrames;
  _removedTrailingFrames = processing.removedTrailingFrames;
  _removedDcOffset = processing.removedDcOffset;
  _normalizeGainX1000 = processing.normalizeGainX1000;
}

void PhoenixSmartReport::buildProtected(
    uint8_t slot,
    const char *modeName,
    const char *qualityName,
    const PhoenixSampleAnalysisResult &analysis) {
  clear();
  _slot = slot & 3U;
  _modeName = modeName ? modeName : "UNKNOWN";
  _qualityName = qualityName ? qualityName : "UNKNOWN";
  _action = ACTION_PROTECTED;
  _qualityScore = analysis.qualityScore;
  _confidence = analysis.trimConfidence;
  _peakBeforePercent = analysis.peakPercent;
  _peakAfterPercent = analysis.peakPercent;
  _dcOffsetBefore = analysis.dcOffset;
  _analysisTimeMs = analysis.analysisTimeMs;
  _originalFrames = analysis.sampleCount;
  _finalFrames = analysis.sampleCount;
}

void PhoenixSmartReport::buildFailed(
    uint8_t slot,
    const char *modeName,
    const char *qualityName,
    const PhoenixSampleAnalysisResult &analysis) {
  buildProtected(slot, modeName, qualityName, analysis);
  _action = ACTION_FAILED;
}

void PhoenixSmartReport::printSerial() const {
  const uint32_t saved = savedFrames(_originalFrames, _finalFrames);
  const uint8_t reduction = reductionPercent(_originalFrames, _finalFrames);

  Serial.println();
  Serial.println(F("=================================="));
  Serial.println(F(" SMART REPORT v1.1"));
  Serial.println(F("=================================="));
  Serial.printf("Slot           S%u\n", (unsigned)(_slot + 1U));
  Serial.printf("Mode           %s\n", _modeName);
  Serial.printf("Recording quality  %u/5\n", (unsigned)_qualityScore);
  Serial.printf("Trim status        %s\n", _qualityName);
  Serial.printf("Confidence         %u %%\n", (unsigned)_confidence);

  if (_action == ACTION_PROTECTED) {
    Serial.println(F("Action         PROTECTED"));
  } else if (_action == ACTION_FAILED) {
    Serial.println(F("Action         FAILED"));
  } else {
    Serial.println(F("Action         PROCESSED"));
  }

  Serial.printf("Trim           %s\n", _trimApplied ? "Applied" : "Skipped");
  if (_trimApplied) {
    Serial.printf("Removed lead   %lu frames\n", (unsigned long)_removedLeadingFrames);
    Serial.printf("Removed tail   %lu frames\n", (unsigned long)_removedTrailingFrames);
  }

  Serial.printf("DC measured        %d\n", (int)_dcOffsetBefore);
  if (_dcRemoved) {
    Serial.printf("DC correction      Applied (%d)\n", (int)_removedDcOffset);
  } else {
    Serial.println(F("DC correction      Not required"));
  }

  Serial.print(F("Normalize      "));
  if (_normalized) {
    printSignedTenthsDb(gainDbX10(_normalizeGainX1000));
    Serial.printf(" (%lu.%03lux)\n",
                  (unsigned long)(_normalizeGainX1000 / 1000U),
                  (unsigned long)(_normalizeGainX1000 % 1000U));
  } else {
    Serial.println(F("Skipped"));
  }

  Serial.printf("Peak           %u %% -> %u %%\n",
                (unsigned)_peakBeforePercent,
                (unsigned)_peakAfterPercent);
  Serial.printf("Frames         %lu -> %lu\n",
                (unsigned long)_originalFrames,
                (unsigned long)_finalFrames);
  Serial.printf("Saved          %lu frames (%u%%)\n",
                (unsigned long)saved,
                (unsigned)reduction);
  Serial.printf("Data changed   %s\n", _dataChanged ? "YES" : "NO");
  Serial.printf("Analysis       %lu ms\n", (unsigned long)_analysisTimeMs);
  Serial.println(F("Done."));
  Serial.println(F("=================================="));
}

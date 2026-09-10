#include "PhoenixOS.h"

static constexpr bool PHX_ENABLE_MIDI_CC_DEBUG = false;
static constexpr uint32_t PHX_MIDI_CC_DEBUG_MIN_MS = 100UL;
#include <esp_heap_caps.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>

PhoenixOS::PhoenixOS(PhoenixGUI &gui, PhoenixScreenManager &screens, PhoenixInputManager &input, PhoenixAudioManager &audio)
: _gui(gui), _screens(screens), _input(input), _audio(audio), _lastDrawMs(0), _perfWindowStartMs(0), _perfLoopAccumUs(0), _perfLoopCount(0), _perfLoopMaxUs(0), _perfDrawAccumUs(0), _perfDrawCount(0), _perfDrawMaxUs(0), _diagStartMs(0), _diagLoopAvgUs(0), _diagLoopPeakUs(0), _diagDrawAvgUs(0), _diagDrawPeakUs(0), _diagGuiFps10(0), _diagLastPublishMs(0), _midiCounter(nullptr), _seqLastStepMs(0), _seqWasRunning(false), _seqPreviewHeld(false), _seqPreviewTrack(0), _seqPreviewNote(60), _seqPreviewOffMs(0), _seqMidiTransportPending(0), _seqMidiLastClockUs(0), _seqMidiClockPeriodUs(0), _seqExtTickPhase(0), _seqExtLastSeenMs(0), _seqLastPattern(0), _seqDroppedClockTicks(0), _seqIdleClockTicksIgnored(0), _seqTransportStartCount(0), _seqTransportContinueCount(0), _seqTransportStopCount(0), _seqPatternTransitionCount(0), _seqGateRecycleCount(0), _seqForcedGateReleaseCount(0), _seqClockQueueDrops(0), _seqClockBatchMax(0), _seqStepLateMaxUs(0), _seqStepEventCount(0), _seqLastStepClockUs(0), _integrationBankQuiesceCount(0), _integrationBankLoadFailCount(0), _integrationBankSaveFailCount(0), _integrationStaleClockTicksCleared(0), _integrationActiveVoiceGuardCount(0), _songPatternStepCounter(0), _lastProgressPercent(255), _lastProgressDrawMs(0), _lastSessionSaveMs(0), _sessionBankCache(0), _sessionScreenCache(255), _sessionSlotCache(255) {
  _progressLabel[0] = 0;
  _midiEchoSync = false;
  _midiEchoTimeValue = 32;
  _midiModulationValue = 0;
  _midiLearnActive = false;
  _midiLearnParameter = 0;
  _midiLearnCcPending = -1;
  _sampleAnalysisPending = false;
  _sampleAnalysisSlot = 0;
  memset(_sampleProcessingRecalcDue, 0, sizeof(_sampleProcessingRecalcDue));
  _recordActivePrev = false;
  _recordActiveSlot = 0;
  memset(_waveBins, 0, sizeof(_waveBins));
  memset((void*)_seqClockUs,0,sizeof(_seqClockUs)); _seqClockWrite=_seqClockRead=0;
  memset((void*)_seqRtStepOn,0,sizeof(_seqRtStepOn));
  memset((void*)_seqRtStepNote,60,sizeof(_seqRtStepNote));
  memset((void*)_seqRtStepVelocity,100,sizeof(_seqRtStepVelocity));
  memset((void*)_seqRtStepGate,80,sizeof(_seqRtStepGate));
  memset(_midiNormalizeArmed, 1, sizeof(_midiNormalizeArmed));
}



void PhoenixOS::progressCallback(uint8_t percent, void *context) {
  if (!context) return;
  static_cast<PhoenixOS*>(context)->showProgress(percent);
}

void PhoenixOS::showProgress(uint8_t percent) {
  const uint32_t now = millis();
  if (percent == _lastProgressPercent && percent != 100) return;
  if (_lastProgressPercent != 255 && percent < _lastProgressPercent && percent != 0) return;
  if (percent != 0 && percent != 100 && (uint32_t)(now - _lastProgressDrawMs) < 60UL) return;
  _lastProgressPercent = percent;
  _lastProgressDrawMs = now;
  char msg[24];
  snprintf(msg, sizeof(msg), "%s %u%%", _progressLabel[0] ? _progressLabel : "WORKING", (unsigned)percent);
  if (!strncmp(_progressLabel, "IMPORTING", 9)) _screens.setBrowserStatus(msg);
  else _screens.setDiskStatus(msg);
  drawScreensTimed();
}


static bool phxEnsureSD() {
  // v0.7.36: Do not permanently lock the storage layer into a failed state.
  // A card that was missing during boot can be inserted later and is retried
  // at a deliberately low rate to avoid blocking the control loop.
  static uint32_t lastTryMs = 0;
  const uint32_t now = millis();
  if (SD.cardType() != CARD_NONE) return true;
  if (lastTryMs != 0 && (uint32_t)(now - lastTryMs) < 2000UL) return false;
  lastTryMs = now;
  return SD.begin(9, SPI);
}

// v0.7.13c FIX8D:
// All Phoenix project banks now live below /PHOENIX/BANKS.
// The old legacy path /PHXBNK01 is intentionally no longer used.
static bool phxBuildBankDir(uint8_t bank, char *out, size_t outSize) {
  if (!out || outSize == 0) return false;
  if (bank < 1) bank = 1;
  if (bank > 99) bank = 99;

  SD.mkdir("/PHOENIX");
  SD.mkdir("/PHOENIX/BANKS");
  snprintf(out, outSize, "/PHOENIX/BANKS/BANK%02u", (unsigned)bank);
  return true;
}

static bool phxBankExists(const char *dir) {
  if (!dir || !dir[0] || !SD.exists(dir)) return false;

  char path[96];

  // A conventional Phoenix bank is valid when BANK.CFG exists.
  snprintf(path, sizeof(path), "%s/BANK.CFG", dir);
  if (SD.exists(path)) return true;

  // v0.7.24b: A multisample-only bank is valid as well. This allows a bank
  // containing only MULTISAMPLE.CFG and external WAV references to be loaded.
  snprintf(path, sizeof(path), "%s/MULTISAMPLE.CFG", dir);
  if (SD.exists(path)) return true;

  // Legacy / single-sample banks may consist only of SLOT1.WAV ... SLOT4.WAV.
  for (uint8_t i = 1; i <= 4; ++i) {
    snprintf(path, sizeof(path), "%s/SLOT%u.WAV", dir, (unsigned)i);
    if (SD.exists(path)) return true;
  }

  return false;
}

void PhoenixOS::quiesceForBankIo(const char *reason) {
  const uint8_t voicesBefore = _audio.activeVoiceCount();
  bool sustainBefore = false;
  for (uint8_t s = 0; s < PhoenixAudioManager::SLOT_COUNT; ++s) sustainBefore |= _audio.sustainDown(s);
  const bool seqBefore = _screens.sequencerRunning();
  if (voicesBefore || sustainBefore || seqBefore) ++_integrationActiveVoiceGuardCount;

  stopSequencerVoices();
  _screens.setSequencerRunning(false);
  stopSongTransport();
  _seqWasRunning = false;
  _seqPreviewHeld = false;
  _audio.stopTransport();

  uint32_t staleTicks = 0;
  portENTER_CRITICAL(&_seqMidiMux);
  const uint8_t cw=_seqClockWrite, cr=_seqClockRead;
  staleTicks=(uint8_t)((cw+kSeqClockQueueSize-cr)%kSeqClockQueueSize); _seqClockRead=cw;
  _seqMidiTransportPending = 0;
  _seqRtRunning = false;
  portEXIT_CRITICAL(&_seqMidiMux);
  _integrationStaleClockTicksCleared += staleTicks;
  _seqExtTickPhase = 0;
  ++_integrationBankQuiesceCount;
  Serial.printf("V1.0 BANK QUIESCE reason=%s voices=%u sustain=%u seq=%u stale_clk=%lu\n",
                reason ? reason : "?", (unsigned)voicesBefore, sustainBefore ? 1U : 0U,
                seqBefore ? 1U : 0U, (unsigned long)staleTicks);
}

bool PhoenixOS::loadBankForSession(uint8_t bank) {
  if (!phxEnsureSD()) return false;
  char bankDir[40];
  if (!phxBuildBankDir(bank, bankDir, sizeof(bankDir)) || !phxBankExists(bankDir)) return false;
  _screens.setSelectedBank(bank);
  quiesceForBankIo("SESSION_LOAD");
  snprintf(_progressLabel, sizeof(_progressLabel), "LOADING");
  _lastProgressPercent=255; _lastProgressDrawMs=0;
  _audio.setProgressCallback(&PhoenixOS::progressCallback, this);
  bool ok=_audio.loadBankFromSD(bankDir);
  _audio.setProgressCallback(nullptr,nullptr);
  if(ok) ok=_screens.loadSequencerConfig(bankDir);
  if(ok) ok=_songManager.loadConfig(bankDir);
  if(!ok) { ++_integrationBankLoadFailCount; return false; }
  syncSongToScreens();
  uint8_t lo[4],hi[4],ch[4],send[4];
  for(uint8_t i=0;i<4;++i){lo[i]=_audio.quattroKeyLow(i);hi[i]=_audio.quattroKeyHigh(i);ch[i]=_audio.quattroMidiChannel(i);send[i]=_audio.echoSend(i);}
  _screens.setQuattroStatus(_audio.quattroMode(),lo,hi,ch);
  _screens.setEchoStatus(_audio.echoDelayMs(),_audio.echoFeedback(),_audio.echoMix(),send);
  _screens.setTriggerStatus(_audio.triggerAuto(),_audio.triggerLevel());
  for(uint8_t i=0;i<4;++i){
    _screens.setPitchSlotStatus(i,_audio.slotCoarse(i),_audio.slotFine(i),_audio.slotRoot(i),_audio.slotOctave(i),_audio.slotPitchTracking(i),_audio.pitchBendRange(i));
    _screens.setEnvelopeSlotStatus(i,_audio.slotAttackMs(i),_audio.slotDecayMs(i),_audio.slotSustainPct(i),_audio.slotReleaseMs(i));
    _screens.setVintageSlotStatus(i,_audio.slotVintagePreset(i),_audio.slotVintageSampleRate(i),_audio.slotVintageBitDepth(i),_audio.slotVintageFilter(i),_audio.slotVintageJitter(i));
    _screens.setVoiceSlotStatus(i,_audio.slotVoiceMode(i),_audio.slotVoiceLimit(i),_audio.slotNotePriority(i),_audio.slotGlideMs(i));
    _screens.setMixerSlotStatus(i,_audio.slotLevel(i),_audio.slotPan(i),_audio.echoSend(i));
  }
  return true;
}

void PhoenixOS::restoreLastSession() {
  Preferences p; p.begin("phoenix", true);
  const bool valid=p.getBool("valid",false);
  const uint8_t bank=p.getUChar("bank",1);
  const uint8_t screen=p.getUChar("screen",5); // SCR_MAIN
  const uint8_t slot=p.getUChar("slot",0);
  p.end();
  bool loaded=false;
  if(valid && bank>=1 && bank<=99) loaded=loadBankForSession(bank);
  if(!loaded) { _screens.setSelectedBank(1); }
  _audio.selectSlot(slot&3U);
  _screens.restoreSessionView(screen,slot);
  _sessionBankCache=_screens.selectedBank(); _sessionScreenCache=_screens.sessionScreen(); _sessionSlotCache=_screens.sessionSlot();
}

void PhoenixOS::saveSessionIfChanged() {
  const uint32_t now=millis(); if((uint32_t)(now-_lastSessionSaveMs)<1000UL)return;
  _lastSessionSaveMs=now;
  const uint8_t bank=_screens.selectedBank(), screen=_screens.sessionScreen(), slot=_screens.sessionSlot();
  if(bank==_sessionBankCache && screen==_sessionScreenCache && slot==_sessionSlotCache)return;
  Preferences p; p.begin("phoenix",false); p.putBool("valid",true); p.putUChar("bank",bank); p.putUChar("screen",screen); p.putUChar("slot",slot); p.end();
  _sessionBankCache=bank; _sessionScreenCache=screen; _sessionSlotCache=slot;
}

void PhoenixOS::begin(bool serviceMode) {
  _events.clear();
  _input.begin();

  // v0.7.40b: Initialize and draw the OLED before the longer audio/storage
  // setup. Previously SCR_BOOT was replaced by session restore before the
  // first physical display update, so the splash screen was never visible.
  _gui.begin(190);
  _screens.begin(serviceMode);
  _diagStartMs = millis();
  _gui.drawBoot();
  delay(1000);

  phxMidiMapBegin();
  _audio.begin();
  refreshRealtimeSequencerSnapshot();
  if (!_seqRtTaskHandle) xTaskCreatePinnedToCore(realtimeSequencerTaskThunk, "PhoenixSeqRT", 6144, this, 8, &_seqRtTaskHandle, 0);
  syncSongToScreens();
  // v0.7.18a: initialize the audio-side Multi-Mode channels from the
  // base-channel-derived defaults prepared by PhoenixScreens.
  {
    uint8_t lo[4], hi[4], ch[4];
    for (uint8_t i = 0; i < 4; ++i) {
      lo[i] = _screens.quattroKeyLow(i);
      hi[i] = _screens.quattroKeyHigh(i);
      ch[i] = _screens.quattroMidiChannel(i);
    }
    _audio.setQuattroConfig(_screens.quattroMode(), lo, hi, ch);
    for(uint8_t i=0;i<4;++i) { _audio.setVoiceConfig(i,0,PhoenixAudioManager::VOICE_COUNT,0,0); _audio.setMixerParams(i,100,0); }
  }
  // v0.7.13c FIX5: persistent system settings are loaded by PhoenixScreens.
  // Apply trigger settings immediately so recorder/levelmeter are correct after boot.
  _audio.setTriggerSettings(_screens.triggerAuto(), _screens.triggerLevel());
  _gui.setMeter(_audio.peakLevel(), _audio.peakHoldLevel(), _audio.highActive(), _audio.clipActive());
  _gui.setTriggerMarker(_audio.triggerAuto() || _screens.triggerTestActive(), _audio.triggerLevel());
  _gui.setRecordStatus(_audio.waitingForTrigger() ? 1 : (_audio.activelyRecording() ? 2 : 0));
  _gui.setRecording(_audio.recording());
  _gui.setPlaying(_audio.playing());
  _screens.setHardwareStatus(_audio.psramOk(), _audio.psramFreeKb(), _audio.audioOk(), 0, _audio.underruns(), _audio.clipCount());
  _screens.setTransportStatus(_audio.recording(), _audio.playing(), _audio.sampleReady(), _audio.sampleFrames(), _audio.recordFrames(), _audio.sampleCapacityFrames(), _audio.playFrames(), _audio.sampleRate(), _audio.dcOffsetL(), _audio.dcOffsetR(), _audio.selectedSlot());
  _screens.setSamplePlayheadStatus(_audio.samplePlayheadActive(_audio.selectedSlot()), _audio.samplePlayheadFrame(_audio.selectedSlot()));
  _screens.setRecorderUxStatus(_audio.waitingForTrigger(), _audio.activelyRecording());
  _screens.setPlaybackReverse(_audio.playingReverse());
  {
    bool rec[4], rcd[4], ply[4]; uint32_t frames[4];
    for (uint8_t i=0;i<4;++i){ rcd[i]=_audio.slotRecorded(i); rec[i]=_audio.slotRecording(i); ply[i]=_audio.slotPlaying(i); frames[i]=_audio.slotFrames(i); }
    _screens.setSlotStatus(rcd, rec, ply, frames);
    uint8_t loopMode[4], loopXfade[4]; uint32_t ls[4], le[4], ss[4], se[4];
    for (uint8_t i=0;i<4;++i){ loopMode[i]=_audio.loopMode(i); loopXfade[i]=_audio.loopCrossfadeMs(i); ls[i]=_audio.loopStart(i); le[i]=_audio.loopEnd(i); ss[i]=_audio.sampleStart(i); se[i]=_audio.sampleEnd(i); }
    bool tr[4], dc[4], nm[4]; for(uint8_t i=0;i<4;++i){tr[i]=_audio.trimEnabled(i);dc[i]=_audio.dcCorrectionEnabled(i);nm[i]=_audio.normalizeEnabled(i);} _screens.setSampleProcessingStatus(tr,dc,nm);
    _screens.setLoopModeStatus(loopMode);
    _screens.setLoopCrossfadeStatus(loopXfade);
    _screens.setLoopRangeStatus(ls, le);
    _screens.setSampleRangeStatus(ss, se);
  }
  if (!serviceMode) restoreLastSession();
  drawScreensTimed();
}

void PhoenixOS::drawScreensTimed() {
  const uint32_t startUs = micros();
  _screens.draw(_gui);
  const uint32_t elapsedUs = (uint32_t)(micros() - startUs);
  _perfDrawAccumUs += elapsedUs;
  ++_perfDrawCount;
  if (elapsedUs > _perfDrawMaxUs) _perfDrawMaxUs = elapsedUs;
}

void PhoenixOS::updatePerformanceAudit(uint32_t loopStartUs) {
  const uint32_t loopUs = (uint32_t)(micros() - loopStartUs);
  _perfLoopAccumUs += loopUs;
  ++_perfLoopCount;
  if (loopUs > _perfLoopMaxUs) _perfLoopMaxUs = loopUs;

  const uint32_t nowMs = millis();
  if (_perfWindowStartMs == 0) _perfWindowStartMs = nowMs;
  if ((uint32_t)(nowMs - _perfWindowStartMs) < 2000UL) return;

  const uint32_t loopAvgUs = _perfLoopCount ? (uint32_t)(_perfLoopAccumUs / _perfLoopCount) : 0;
  const uint32_t drawAvgUs = _perfDrawCount ? (uint32_t)(_perfDrawAccumUs / _perfDrawCount) : 0;
  const uint32_t guiFps10 = ((uint32_t)_perfDrawCount * 10000UL) /
                            max((uint32_t)1UL, (uint32_t)(nowMs - _perfWindowStartMs));
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t freePsram = ESP.getFreePsram();

  _diagLoopAvgUs = loopAvgUs;
  if (_perfLoopMaxUs > _diagLoopPeakUs) _diagLoopPeakUs = _perfLoopMaxUs;
  _diagDrawAvgUs = drawAvgUs;
  if (_perfDrawMaxUs > _diagDrawPeakUs) _diagDrawPeakUs = _perfDrawMaxUs;
  _diagGuiFps10 = guiFps10;

  const uint32_t rtLateAvg = _seqRtLateCount ? (uint32_t)(_seqRtLateAccumUs / _seqRtLateCount) : 0U;
  Serial.printf("PERF audio avg=%luus peak=%luus last=%luus load=%.1f%% voices=%u/%u vpeak=%u risk=%lu overrun=%lu underruns=%lu steals=%lu dup_on=%lu orphan_off=%lu sus_def=%lu panic_kill=%lu midi_cmd_drop=%lu midi_ctl_drop=%lu | fx echo_limit=%lu filter_guard=%lu echo_slew=%lu | ms note_miss=%lu vel_miss=%lu rr_fallback=%lu sanitize=%lu | integ bank_q=%lu load_fail=%lu save_fail=%lu stale_clk=%lu guard=%lu | gui avg=%luus peak=%luus fps=%lu.%lu | loop avg=%luus peak=%luus | heap=%luKB psram=%luKB | seq start=%lu cont=%lu stop=%lu pat=%lu tick_drop=%lu idle_clk=%lu gate_recycle=%lu gate_release=%lu clk_batch=%lu clk_qdrop=%lu step_late_max=%luus seq_cmd_drop=%lu note_on_fail=%lu owner_fail=%lu | rt batch=%lu late_max=%luus late_avg=%luus pred_err=%luus sched_miss=%lu frame_off=%lu owner_stale=%lu\n",
                (unsigned long)_audio.audioLoadAvgUs(),
                (unsigned long)_audio.audioLoadPeakUs(),
                (unsigned long)_audio.audioLoadLastUs(),
                ((float)_audio.audioLoadAvgUs() * 100.0f) / 4000.0f,
                (unsigned)_audio.activeVoiceCount(),
                (unsigned)PhoenixAudioManager::VOICE_COUNT,
                (unsigned)_audio.activeVoicePeak(),
                (unsigned long)_audio.audioRiskCount(),
                (unsigned long)_audio.audioOverrunCount(),
                (unsigned long)_audio.underruns(),
                (unsigned long)_audio.voiceStealCount(),
                (unsigned long)_audio.duplicateNoteOnCount(),
                (unsigned long)_audio.orphanNoteOffCount(),
                (unsigned long)_audio.sustainDeferredNoteOffCount(),
                (unsigned long)_audio.panicKillCount(),
                (unsigned long)_audio.midiCommandDrops(),
                (unsigned long)_midiControlDrops,
                (unsigned long)_audio.echoLimitCount(),
                (unsigned long)_audio.filterGuardCount(),
                (unsigned long)_audio.echoParamSlewCount(),
                (unsigned long)_audio.multisampleNoteMissCount(),
                (unsigned long)_audio.multisampleVelocityMissCount(),
                (unsigned long)_audio.multisampleRrFallbackCount(),
                (unsigned long)_audio.multisampleSanitizeCount(),
                (unsigned long)_integrationBankQuiesceCount,
                (unsigned long)_integrationBankLoadFailCount,
                (unsigned long)_integrationBankSaveFailCount,
                (unsigned long)_integrationStaleClockTicksCleared,
                (unsigned long)_integrationActiveVoiceGuardCount,
                (unsigned long)drawAvgUs,
                (unsigned long)_diagDrawPeakUs,
                (unsigned long)(guiFps10 / 10UL),
                (unsigned long)(guiFps10 % 10UL),
                (unsigned long)loopAvgUs,
                (unsigned long)_diagLoopPeakUs,
                (unsigned long)(freeHeap / 1024UL),
                (unsigned long)(freePsram / 1024UL),
                (unsigned long)_seqTransportStartCount, (unsigned long)_seqTransportContinueCount,
                (unsigned long)_seqTransportStopCount, (unsigned long)_seqPatternTransitionCount,
                (unsigned long)_seqDroppedClockTicks, (unsigned long)_seqIdleClockTicksIgnored, (unsigned long)_seqGateRecycleCount,
                (unsigned long)_seqForcedGateReleaseCount,
                (unsigned long)_seqClockBatchMax, (unsigned long)_seqClockQueueDrops, (unsigned long)_seqStepLateMaxUs,
                (unsigned long)_audio.sequencerCommandDrops(), (unsigned long)_audio.sequencerNoteOnFailCount(), (unsigned long)_audio.sequencerOwnerFailCount(),
                (unsigned long)_seqRtBatchMax, (unsigned long)_seqRtLateMaxUs, (unsigned long)rtLateAvg,
                (unsigned long)_seqRtPredictionErrorMaxUs, (unsigned long)_audio.sequencerScheduleMissCount(),
                (unsigned long)_audio.sequencerFrameOffsetMax(), (unsigned long)_audio.sequencerOwnerStaleCount());

  _perfWindowStartMs = nowMs;
  _perfLoopAccumUs = 0;
  _perfLoopCount = 0;
  _perfLoopMaxUs = 0;
  _perfDrawAccumUs = 0;
  _perfDrawCount = 0;
  _perfDrawMaxUs = 0;
}

void PhoenixOS::analyzeRecordedSlot(uint8_t slot) {
  slot &= 3U;
  const int16_t *buffer = _audio.slotSampleBuffer(slot);
  const uint32_t frames = _audio.slotFrames(slot);
  PhoenixSampleAnalysisResult result;
  if (!PhoenixSampleAnalysis::analyze(buffer, frames, result)) {
    Serial.printf("SMART SAMPLE S%u: analysis failed\n", (unsigned)(slot + 1U));
    return;
  }

  const bool trimApproved = result.valid &&
                            result.trimConfidence >= 90U &&
                            result.startConfirmed &&
                            result.endConfirmed &&
                            result.suggestedEnd > result.suggestedStart + 1U;

  Serial.printf("SMART SAMPLE S%u frames=%lu peak=%u%% rms=%u dc=%d noise=%u noiseValid=%u threshold=%u trim=%lu..%lu lead=%lu tail=%lu confidence=%u start=%s end=%s analysis=%lums quality=%u crest=%u.%02u clip=%u normalize=%u\n",
                (unsigned)(slot + 1U),
                (unsigned long)result.sampleCount,
                (unsigned)result.peakPercent,
                (unsigned)result.rms,
                (int)result.dcOffset,
                (unsigned)result.noiseFloor,
                result.noiseValid ? 1U : 0U,
                (unsigned)result.detectionThreshold,
                (unsigned long)result.suggestedStart,
                (unsigned long)result.suggestedEnd,
                (unsigned long)result.leadingSilenceFrames,
                (unsigned long)result.trailingSilenceFrames,
                (unsigned)result.trimConfidence,
                result.startConfirmed ? "confirmed" : "open",
                result.endConfirmed ? "confirmed" : "open",
                (unsigned long)result.analysisTimeMs,
                (unsigned)result.qualityScore,
                (unsigned)(result.crestFactorX100 / 100U),
                (unsigned)(result.crestFactorX100 % 100U),
                result.clipped ? 1U : 0U,
                result.normalizeRecommended ? 1U : 0U);

  // v0.7.43b: runtime Smart Processing mode loaded from /PHOENIX/CONFIG.TXT.
  // 0 SAFE, 1 SMART, 2 FORCE. Default is SAFE.
  const uint8_t mode = _screens.smartProcessingMode();
  const char *modeName = (mode == 0U) ? "SAFE" : ((mode == 1U) ? "SMART" : "FORCE");

  bool applyTrim = false;
  bool removeDc = false;
  bool normalize = false;
  bool processAllowed = true;

  if (mode == 0U) { // SAFE
    processAllowed = trimApproved;
    applyTrim = trimApproved;
    removeDc = trimApproved;
    normalize = trimApproved && result.normalizeRecommended;
  } else if (mode == 1U) { // SMART
    applyTrim = trimApproved;
    removeDc = true;
    normalize = result.normalizeRecommended;
  } else { // FORCE
    applyTrim = result.valid &&
                result.suggestedEnd > result.suggestedStart + 1U &&
                (result.suggestedStart > 0U || result.suggestedEnd < frames);
    removeDc = true;
    normalize = result.peakPercent > 0U && !result.clipped;
  }

  const char *qualityName;
  if (result.clipped) qualityName = "CLIPPED";
  else if (!result.startConfirmed || !result.endConfirmed) qualityName = "OPEN";
  else if (result.qualityScore >= 5U) qualityName = "STUDIO";
  else if (result.qualityScore >= 4U) qualityName = "GOOD";
  else if (result.qualityScore >= 3U) qualityName = "FAIR";
  else qualityName = "CHECK";

  if (!processAllowed) {
    _audio.setSampleRange(slot, 0, frames);
    _screens.setSampleAnalysisSummary(result.peakPercent,
                                      result.dcDetected,
                                      result.suggestedStart,
                                      result.suggestedEnd,
                                      false);
    Serial.printf("SMART PROCESSING S%u mode=%s action=protected quality=%s trim=skipped dc=skipped normalize=skipped frames=%lu->%lu dataChanged=0\n",
                  (unsigned)(slot + 1U), modeName, qualityName,
                  (unsigned long)frames, (unsigned long)frames);
    PhoenixSmartReport smartReport;
    smartReport.buildProtected(slot, modeName, qualityName, result);
    smartReport.printSerial();
    return;
  }

  PhoenixAudioManager::SmartProcessResult processed;
  const bool processingOk = _audio.smartProcessSlot(slot,
                                                     result.suggestedStart,
                                                     result.suggestedEnd,
                                                     applyTrim,
                                                     removeDc,
                                                     normalize,
                                                     processed);
  if (!processingOk) {
    _audio.setSampleRange(slot, 0, frames);
    _screens.setSampleAnalysisSummary(result.peakPercent,
                                      result.dcDetected,
                                      result.suggestedStart,
                                      result.suggestedEnd,
                                      false);
    Serial.printf("SMART PROCESSING S%u mode=%s failed quality=%s dataChanged=0\n",
                  (unsigned)(slot + 1U), modeName, qualityName);
    PhoenixSmartReport smartReport;
    smartReport.buildFailed(slot, modeName, qualityName, result);
    smartReport.printSerial();
    return;
  }

  _screens.setSampleAnalysisSummary(result.peakPercent,
                                    processed.dcRemoved,
                                    0,
                                    processed.finalFrames,
                                    processed.trimApplied);

  Serial.printf("SMART PROCESSING S%u mode=%s quality=%s trim=%s removedLead=%lu removedTail=%lu dc=%s dcValue=%d normalize=%s normalizeGain=%lu.%03lux frames=%lu->%lu peak=%u%% dataChanged=%u\n",
                (unsigned)(slot + 1U),
                modeName,
                qualityName,
                processed.trimApplied ? "applied" : "skipped",
                (unsigned long)processed.removedLeadingFrames,
                (unsigned long)processed.removedTrailingFrames,
                processed.dcRemoved ? "removed" : "skipped",
                (int)processed.removedDcOffset,
                processed.normalized ? "applied" : "skipped",
                (unsigned long)(processed.normalizeGainX1000 / 1000U),
                (unsigned long)(processed.normalizeGainX1000 % 1000U),
                (unsigned long)processed.originalFrames,
                (unsigned long)processed.finalFrames,
                (unsigned)((processed.finalPeak * 100UL + 16383UL) / 32767UL),
                processed.dataChanged ? 1U : 0U);

  PhoenixSmartReport smartReport;
  smartReport.buildProcessed(slot, modeName, qualityName, result, processed);
  smartReport.printSerial();

}

void PhoenixOS::routeEvents() {
  PhoenixEvent ev;
  while (_events.poll(ev)) {
    (void)ev;
  }
}


bool PhoenixOS::queueMidiControlChange(uint8_t slot, uint8_t cc, uint8_t value) {
  const uint8_t write = __atomic_load_n(&_midiControlWrite, __ATOMIC_RELAXED);
  const uint8_t next = (uint8_t)((write + 1U) % kMidiControlQueueSize);
  const uint8_t read = __atomic_load_n(&_midiControlRead, __ATOMIC_ACQUIRE);
  if (next == read) {
    __atomic_add_fetch(&_midiControlDrops, 1U, __ATOMIC_RELAXED);
    return false;
  }
  _midiControlQueue[write] = { (uint8_t)(slot & 3U), cc, value };
  __atomic_store_n(&_midiControlWrite, next, __ATOMIC_RELEASE);
  return true;
}

void PhoenixOS::processMidiControlQueue() {
  for (;;) {
    const uint8_t read = __atomic_load_n(&_midiControlRead, __ATOMIC_RELAXED);
    const uint8_t write = __atomic_load_n(&_midiControlWrite, __ATOMIC_ACQUIRE);
    if (read == write) break;
    const MidiControlEvent ev = _midiControlQueue[read];
    __atomic_store_n(&_midiControlRead, (uint8_t)((read + 1U) % kMidiControlQueueSize), __ATOMIC_RELEASE);
    notifyMidiControlChange(ev.slot, ev.cc, ev.value);
  }
}

void PhoenixOS::notifyMidiControlChange(uint8_t slot, uint8_t cc, uint8_t value) {
  slot &= 3U;
  if (_midiLearnActive) { _midiLearnCcPending=(int16_t)cc; return; }
  uint8_t matchStart=0;
  const PhoenixMidiParameterDef *def=nullptr;
  while ((def=phxMidiParameterForCc(cc,matchStart)) != nullptr) {
    matchStart=(uint8_t)def->id+1U;
    switch (def->id) {
    case PHX_PAR_SLOT_VOLUME:
      _audio.setMixerParams(slot, (uint8_t)((value * 100U + 63U) / 127U), _audio.slotPan(slot));
      _screens.setMixerSlotStatus(slot, _audio.slotLevel(slot), _audio.slotPan(slot), _audio.echoSend(slot));
      break;
    case PHX_PAR_SLOT_PAN:
      _audio.setMixerParams(slot, _audio.slotLevel(slot), (int8_t)((int)value * 200 / 127 - 100));
      _screens.setMixerSlotStatus(slot, _audio.slotLevel(slot), _audio.slotPan(slot), _audio.echoSend(slot));
      break;
    case PHX_PAR_SAMPLE_START: {
      const uint32_t frames = _audio.slotFrames(slot); if (!frames) break;
      uint32_t start = (uint32_t)(((uint64_t)value * (frames - 1U)) / 127ULL);
      _audio.setSampleRange(slot, start, _audio.sampleEnd(slot));
      break;
    }
    case PHX_PAR_SAMPLE_END: {
      const uint32_t frames = _audio.slotFrames(slot); if (!frames) break;
      uint32_t end = 1U + (uint32_t)(((uint64_t)value * (frames - 1U)) / 127ULL);
      _audio.setSampleRange(slot, _audio.sampleStart(slot), end);
      break;
    }
    case PHX_PAR_LOOP_START: {
      const uint32_t frames = _audio.slotFrames(slot); if (!frames) break;
      uint32_t start = (uint32_t)(((uint64_t)value * (frames - 1U)) / 127ULL);
      _audio.setLoopRange(slot, start, _audio.loopEnd(slot));
      break;
    }
    case PHX_PAR_LOOP_END: {
      const uint32_t frames = _audio.slotFrames(slot); if (!frames) break;
      uint32_t end = 1U + (uint32_t)(((uint64_t)value * (frames - 1U)) / 127ULL);
      _audio.setLoopRange(slot, _audio.loopStart(slot), end);
      break;
    }
    case PHX_PAR_ROOT_KEY:
      _audio.setInstrumentParams(slot, _audio.slotCoarse(slot), _audio.slotFine(slot), value);
      _screens.setPitchSlotStatus(slot,_audio.slotCoarse(slot),_audio.slotFine(slot),_audio.slotRoot(slot),_audio.slotOctave(slot),_audio.slotPitchTracking(slot),_audio.pitchBendRange(slot));
      break;
    case PHX_PAR_TRANSPOSE:
      _audio.setInstrumentParams(slot, (int8_t)constrain((int)value - 64, -24, 24), _audio.slotFine(slot), _audio.slotRoot(slot));
      _screens.setPitchSlotStatus(slot,_audio.slotCoarse(slot),_audio.slotFine(slot),_audio.slotRoot(slot),_audio.slotOctave(slot),_audio.slotPitchTracking(slot),_audio.pitchBendRange(slot));
      break;
    case PHX_PAR_FINE_TUNE:
      _audio.setInstrumentParams(slot, _audio.slotCoarse(slot), (int16_t)((int)value * 200 / 127 - 100), _audio.slotRoot(slot));
      _screens.setPitchSlotStatus(slot,_audio.slotCoarse(slot),_audio.slotFine(slot),_audio.slotRoot(slot),_audio.slotOctave(slot),_audio.slotPitchTracking(slot),_audio.pitchBendRange(slot));
      break;
    case PHX_PAR_REVERSE:
      _screens.setPlaybackReverseMode(value >= 64);
      break;
    case PHX_PAR_NORMALIZE:
      if (value < 64) _midiNormalizeArmed[slot] = true;
      else if (_midiNormalizeArmed[slot]) { _midiNormalizeArmed[slot] = false; _audio.normalizeSlot(slot); }
      break;
    case PHX_PAR_SAMPLE_GAIN:
      _audio.setSampleGain(slot, (uint8_t)((value * 200U + 63U) / 127U));
      break;
    case PHX_PAR_AMP_ATTACK:
      _audio.setEnvelopeParams(slot, (uint16_t)(((uint32_t)value * 5000U) / 127U), _audio.slotDecayMs(slot), _audio.slotSustainPct(slot), _audio.slotReleaseMs(slot));
      _screens.setEnvelopeSlotStatus(slot,_audio.slotAttackMs(slot),_audio.slotDecayMs(slot),_audio.slotSustainPct(slot),_audio.slotReleaseMs(slot));
      break;
    case PHX_PAR_AMP_DECAY:
      _audio.setEnvelopeParams(slot, _audio.slotAttackMs(slot), (uint16_t)(((uint32_t)value * 5000U) / 127U), _audio.slotSustainPct(slot), _audio.slotReleaseMs(slot));
      _screens.setEnvelopeSlotStatus(slot,_audio.slotAttackMs(slot),_audio.slotDecayMs(slot),_audio.slotSustainPct(slot),_audio.slotReleaseMs(slot));
      break;
    case PHX_PAR_AMP_SUSTAIN:
      _audio.setEnvelopeParams(slot, _audio.slotAttackMs(slot), _audio.slotDecayMs(slot), (uint8_t)((value * 100U + 63U) / 127U), _audio.slotReleaseMs(slot));
      _screens.setEnvelopeSlotStatus(slot,_audio.slotAttackMs(slot),_audio.slotDecayMs(slot),_audio.slotSustainPct(slot),_audio.slotReleaseMs(slot));
      break;
    case PHX_PAR_AMP_RELEASE:
      _audio.setEnvelopeParams(slot, _audio.slotAttackMs(slot), _audio.slotDecayMs(slot), _audio.slotSustainPct(slot), (uint16_t)(((uint32_t)value * 5000U) / 127U));
      _screens.setEnvelopeSlotStatus(slot,_audio.slotAttackMs(slot),_audio.slotDecayMs(slot),_audio.slotSustainPct(slot),_audio.slotReleaseMs(slot));
      break;
    case PHX_PAR_VELOCITY_AMOUNT:
      _audio.setVelocityAmount(slot, (uint8_t)((value * 100U + 63U) / 127U));
      break;
    case PHX_PAR_FILTER_CUTOFF:
      _audio.setFilterParams(slot,(uint8_t)((value*100U+63U)/127U),_audio.slotFilterResonance(slot),_audio.slotFilterEnvAmount(slot),_audio.slotFilterVelocityAmount(slot),_audio.slotFilterKeytrack(slot));
      break;
    case PHX_PAR_FILTER_RESONANCE:
      _audio.setFilterParams(slot,_audio.slotFilterCutoff(slot),(uint8_t)((value*100U+63U)/127U),_audio.slotFilterEnvAmount(slot),_audio.slotFilterVelocityAmount(slot),_audio.slotFilterKeytrack(slot));
      break;
    case PHX_PAR_ECHO_SEND:
      _audio.setEchoSend(slot,(uint8_t)((value*100U+63U)/127U));
      _screens.setMixerSlotStatus(slot,_audio.slotLevel(slot),_audio.slotPan(slot),_audio.echoSend(slot));
      break;
    case PHX_PAR_ECHO_TIME: {
      _midiEchoTimeValue=value;
      uint16_t ms;
      if (_midiEchoSync) {
        static const uint8_t divNum[8]={1,1,1,1,1,1,1,2};
        static const uint8_t divDen[8]={8,6,4,3,2,1,1,1};
        uint8_t idx=(uint8_t)min(7,(int)(value>>4));
        uint32_t quarter=60000UL/max((uint16_t)40,_screens.sequencerBpm());
        ms=(uint16_t)constrain((int)((quarter*divNum[idx])/divDen[idx]),50,1000);
      } else ms=(uint16_t)(50U+((uint32_t)value*950U)/127U);
      _audio.setEchoParams(ms,_audio.echoFeedback(),_audio.echoMix());
      break;
    }
    case PHX_PAR_ECHO_FEEDBACK:
      _audio.setEchoParams(_audio.echoDelayMs(),(uint8_t)((value*90U+63U)/127U),_audio.echoMix());
      break;
    case PHX_PAR_ECHO_MIX:
      _audio.setEchoParams(_audio.echoDelayMs(),_audio.echoFeedback(),(uint8_t)((value*100U+63U)/127U));
      break;
    case PHX_PAR_ECHO_SYNC:
      _midiEchoSync=(value>=64); notifyMidiControlChange(slot,89,_midiEchoTimeValue); break;
    case PHX_PAR_GLIDE:
      _audio.setVoiceConfig(slot,_audio.slotVoiceMode(slot),_audio.slotVoiceLimit(slot),_audio.slotNotePriority(slot),(uint16_t)(((uint32_t)value*2000U)/127U));
      _screens.setVoiceSlotStatus(slot,_audio.slotVoiceMode(slot),_audio.slotVoiceLimit(slot),_audio.slotNotePriority(slot),_audio.slotGlideMs(slot));
      break;
    case PHX_PAR_MODULATION:
      _midiModulationValue=value; // reserved for Phoenix 1.1 modulation system
      break;
      default: break;
    }
    if (PHX_ENABLE_MIDI_CC_DEBUG) {
      static uint32_t lastCcDebugMs = 0;
      const uint32_t nowCcDebugMs = millis();
      if ((uint32_t)(nowCcDebugMs - lastCcDebugMs) >= PHX_MIDI_CC_DEBUG_MIN_MS) {
        lastCcDebugMs = nowCcDebugMs;
        Serial.printf("MIDI CC%03u -> %s value=%u slot=%u\n", (unsigned)cc, def->name, (unsigned)value, (unsigned)(slot+1));
      }
    }
  }
}

void PhoenixOS::notifyMidiClock() {
  const uint32_t nowUs = micros();
  portENTER_CRITICAL(&_seqMidiMux);
  if (_seqMidiLastClockUs != 0) {
    const uint32_t period = nowUs - _seqMidiLastClockUs;
    if (period >= 1000U && period <= 200000U) {
      if (_seqMidiClockPeriodUs == 0) _seqMidiClockPeriodUs = period;
      else _seqMidiClockPeriodUs = (_seqMidiClockPeriodUs * 7U + period) / 8U;
    }
  }
  _seqMidiLastClockUs = nowUs;
  const uint8_t write=_seqClockWrite, next=(uint8_t)((write+1U)%kSeqClockQueueSize);
  if(next==_seqClockRead) ++_seqClockQueueDrops; else { _seqClockUs[write]=nowUs; _seqClockWrite=next; }
  portEXIT_CRITICAL(&_seqMidiMux);
  if (_seqRtTaskHandle) xTaskNotifyGive(_seqRtTaskHandle);
}

void PhoenixOS::notifyMidiStart() {
  portENTER_CRITICAL(&_seqMidiMux);
  // C034b: clocks that arrived before FA belong to STOP/idle time and must not
  // be replayed after START. F8 bytes arriving after FA accumulate normally.
  _seqClockRead = _seqClockWrite;
  _seqMidiTransportPending = 1U;
  portEXIT_CRITICAL(&_seqMidiMux);
  if (_seqRtTaskHandle) xTaskNotifyGive(_seqRtTaskHandle);
}

void PhoenixOS::notifyMidiContinue() {
  portENTER_CRITICAL(&_seqMidiMux);
  // C034b: discard idle clocks before FB; only clocks after CONTINUE advance.
  _seqClockRead = _seqClockWrite;
  _seqMidiTransportPending = 2U;
  portEXIT_CRITICAL(&_seqMidiMux);
  if (_seqRtTaskHandle) xTaskNotifyGive(_seqRtTaskHandle);
}

void PhoenixOS::notifyMidiStop() {
  portENTER_CRITICAL(&_seqMidiMux);
  // C034b: make STOP a hard clock-domain boundary. Pending pre-STOP ticks are
  // irrelevant once transport is stopped and must never become a later burst.
  _seqClockRead = _seqClockWrite;
  _seqMidiTransportPending = 3U;
  portEXIT_CRITICAL(&_seqMidiMux);
  if (_seqRtTaskHandle) xTaskNotifyGive(_seqRtTaskHandle);
}



void PhoenixOS::syncSongToScreens() {
  for (uint8_t i=0;i<PhoenixSongManager::kMaxEntries;++i) {
    const PhoenixSongManager::SongEntry &e=_songManager.entry(i);
    _screens.setSongEntryStatus(i,e.pattern,e.repeats,e.end);
  }
  _screens.setSongLoopStatus(_songManager.loopMode(),_songManager.loopStart(),_songManager.loopEnd());
  _screens.setSongPlaybackStatus(_songManager.playing(),_songManager.position(),_songManager.repeatIndex(),_songManager.currentRepeatTarget());
}

void PhoenixOS::syncScreensToSong() {
  for (uint8_t i=0;i<PhoenixSongManager::kMaxEntries;++i) {
    _songManager.setEntry(i,_screens.songPattern(i),_screens.songRepeats(i),_screens.songEnd(i));
  }
  _songManager.setLoopMode(_screens.songLoopMode(),_screens.songLoopStart(),_screens.songLoopEnd());
}

void PhoenixOS::setSequencerPatternRobust(uint8_t pattern, bool releaseOldGates) {
  const uint8_t next = pattern & 3U;
  const uint8_t old = _screens.sequencerPattern();
  if (releaseOldGates && old != next) stopSequencerVoices();
  _screens.setSequencerPattern(next);
  _seqLastPattern = next;
  if (old != next) ++_seqPatternTransitionCount;
}

void PhoenixOS::startSongTestTransport() {
  _songManager.start();
  _songPatternStepCounter = 0;
  setSequencerPatternRobust(_songManager.currentPattern(), true);
  _screens.setSequencerPlayhead(0);
  syncSongToScreens();
}

void PhoenixOS::pauseSongTransport() {
  _songManager.pause();
  syncSongToScreens();
}

void PhoenixOS::stopSongTransport() {
  _songManager.stop();
  _songPatternStepCounter = 0;
  syncSongToScreens();
}

void PhoenixOS::advanceSongPatternCycle() {
  if (!_songManager.playing()) return;
  uint8_t cycleLength = _screens.sequencerMaxTrackLength();
  if (cycleLength < 1U) cycleLength = 1U;
  if (++_songPatternStepCounter < cycleLength) return;

  _songPatternStepCounter = 0;
  uint8_t nextPattern = _screens.sequencerPattern();
  if (_songManager.onPatternFinished(nextPattern)) {
    setSequencerPatternRobust(nextPattern, true);
    syncSongToScreens();
  } else {
    _screens.setSequencerRunning(false);
    stopSequencerVoices();
    _seqWasRunning = false;
    syncSongToScreens();
  }
}

void PhoenixOS::stopSequencerVoices() {
  if (!_audio.queueSequencerStop()) ++_seqForcedGateReleaseCount;
}

void PhoenixOS::triggerSequencerStep(uint8_t /*globalStep*/, uint32_t stepUs) {
  stepUs=constrain(stepUs,20000UL,2000000UL);
  for(uint8_t t=0;t<4;++t){ const uint8_t length=(uint8_t)constrain((int)_screens.sequencerTrackLength(t),1,16); const uint8_t st=_screens.sequencerTrackPlayhead(t)%length; if(_screens.sequencerStepOn(t,st)){ const uint8_t note=(uint8_t)constrain((int)_screens.sequencerStepNote(t,st),0,127); const uint8_t vel=(uint8_t)constrain((int)_screens.sequencerStepVelocity(t,st),1,127); const uint8_t gate=(uint8_t)constrain((int)_screens.sequencerStepGate(t,st),1,100); uint32_t gateUs=(uint32_t)(((uint64_t)stepUs*gate)/100ULL); if(gateUs<4000UL)gateUs=4000UL; const uint32_t gateFrames=(uint32_t)(((uint64_t)gateUs*32000ULL+999999ULL)/1000000ULL); if(!_audio.queueSequencerNote(t,note,vel,gateFrames)) ++_seqGateRecycleCount; } _screens.setSequencerTrackPlayhead(t,(uint8_t)((st+1U)%length)); } ++_seqStepEventCount; advanceSongPatternCycle();
}

void PhoenixOS::processSequencerMidiTransport() {
  // C036d: external F8/transport is owned by PhoenixSeqRT/P8. Control only
  // mirrors status into the UI; it never drains the realtime clock queue.
  uint32_t periodUs=0,lastClockUs=0;
  portENTER_CRITICAL(&_seqMidiMux);
  periodUs=_seqMidiClockPeriodUs; lastClockUs=_seqMidiLastClockUs;
  portEXIT_CRITICAL(&_seqMidiMux);
  const uint32_t nowUs=micros();
  const bool present=lastClockUs!=0 && (uint32_t)(nowUs-lastClockUs)<600000UL;
  uint16_t extBpm=120;
  if(periodUs){ const uint32_t denom=periodUs*24UL; if(denom) extBpm=(uint16_t)constrain((int)(60000000UL/denom),20,300); }
  _screens.setSequencerExternalStatus(present,extBpm);
}

void PhoenixOS::refreshRealtimeSequencerSnapshot() {
  const bool ext=_screens.sequencerExternalClock();
  uint8_t len[4], on[4][16], note[4][16], vel[4][16], gate[4][16];
  for(uint8_t t=0;t<4;++t){
    len[t]=(uint8_t)constrain((int)_screens.sequencerTrackLength(t),1,16);
    for(uint8_t st=0;st<16;++st){
      on[t][st]=_screens.sequencerStepOn(t,st)?1U:0U;
      note[t][st]=(uint8_t)constrain((int)_screens.sequencerStepNote(t,st),0,127);
      vel[t][st]=(uint8_t)constrain((int)_screens.sequencerStepVelocity(t,st),1,127);
      gate[t][st]=(uint8_t)constrain((int)_screens.sequencerStepGate(t,st),1,100);
    }
  }
  portENTER_CRITICAL(&_seqMidiMux);
  _seqRtExternalEnabled=ext;
  for(uint8_t t=0;t<4;++t){
    _seqRtTrackLength[t]=len[t];
    for(uint8_t st=0;st<16;++st){ _seqRtStepOn[t][st]=on[t][st]; _seqRtStepNote[t][st]=note[t][st]; _seqRtStepVelocity[t][st]=vel[t][st]; _seqRtStepGate[t][st]=gate[t][st]; }
  }
  ++_seqRtConfigGeneration;
  portEXIT_CRITICAL(&_seqMidiMux);
}

void PhoenixOS::scheduleRealtimeSequencerStep(uint32_t targetUs, uint32_t stepUs) {
  struct E { uint8_t t,n,v,g; bool on; uint32_t id; } e[4];
  stepUs=constrain(stepUs,20000UL,2000000UL);
  portENTER_CRITICAL(&_seqMidiMux);
  for(uint8_t t=0;t<4;++t){
    const uint8_t length=(uint8_t)constrain((int)_seqRtTrackLength[t],1,16);
    const uint8_t st=_seqRtPlayhead[t]%length;
    e[t].t=t; e[t].on=_seqRtStepOn[t][st]!=0; e[t].n=_seqRtStepNote[t][st]; e[t].v=_seqRtStepVelocity[t][st]; e[t].g=_seqRtStepGate[t][st]; e[t].id=++_seqRtEventId;
    _seqRtPlayhead[t]=(uint8_t)((st+1U)%length);
  }
  portEXIT_CRITICAL(&_seqMidiMux);
  for(uint8_t i=0;i<4;++i){
    if(!e[i].on) continue;
    uint32_t gateUs=(uint32_t)(((uint64_t)stepUs*e[i].g)/100ULL); if(gateUs<1000UL)gateUs=1000UL;
    const uint32_t gateFrames=(uint32_t)(((uint64_t)gateUs*32000ULL+999999ULL)/1000000ULL);
    if(!_audio.queueSequencerNoteScheduled(e[i].t,e[i].n,e[i].v,gateFrames,targetUs,e[i].id)) ++_seqGateRecycleCount;
  }
  ++_seqStepEventCount;
}

void PhoenixOS::realtimeSequencerTaskThunk(void *arg) { static_cast<PhoenixOS*>(arg)->realtimeSequencerTask(); }

void PhoenixOS::realtimeSequencerTask() {
  Serial.printf("V1.0 SEQRT task core=%d priority=%u\n", xPortGetCoreID(), (unsigned)uxTaskPriorityGet(nullptr));
  uint8_t phase=0; uint32_t lastStepClockUs=0; bool preScheduled=false; uint32_t predictedTargetUs=0;
  for(;;){
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5));
    uint32_t ticks[32]; uint8_t count=0, transport=0; uint32_t periodUs=0;
    portENTER_CRITICAL(&_seqMidiMux);
    transport=_seqMidiTransportPending; _seqMidiTransportPending=0U;
    while(_seqClockRead!=_seqClockWrite && count<32U){ ticks[count++]=_seqClockUs[_seqClockRead]; _seqClockRead=(uint8_t)((_seqClockRead+1U)%kSeqClockQueueSize); }
    periodUs=_seqMidiClockPeriodUs;
    portEXIT_CRITICAL(&_seqMidiMux);
    if(count>_seqRtBatchMax)_seqRtBatchMax=count;

    if(transport==3U){
      _seqRtRunning=false; preScheduled=false; lastStepClockUs=0; phase=0; _audio.queueSequencerStop(); ++_seqTransportStopCount;
    } else if(transport==1U){
      _audio.queueSequencerStop();
      portENTER_CRITICAL(&_seqMidiMux); for(uint8_t t=0;t<4;++t)_seqRtPlayhead[t]=0; portEXIT_CRITICAL(&_seqMidiMux);
      _seqRtRunning=true; phase=5; preScheduled=false; lastStepClockUs=0; ++_seqTransportStartCount;
    } else if(transport==2U){
      _seqRtRunning=true; phase=5; preScheduled=false; lastStepClockUs=0; ++_seqTransportContinueCount;
    }

    if(!_seqRtExternalEnabled){ continue; }
    for(uint8_t i=0;i<count;++i){
      const uint32_t tickUs=ticks[i];
      const uint32_t late=(uint32_t)(micros()-tickUs);
      if(late>_seqRtLateMaxUs)_seqRtLateMaxUs=late; _seqRtLateAccumUs+=late; ++_seqRtLateCount;
      if(!_seqRtRunning){ ++_seqIdleClockTicksIgnored; continue; }
      phase=(uint8_t)((phase+1U)%6U);
      if(phase==0U){
        ++_seqRtCompletedSteps;
        if(preScheduled){
          const int32_t err=(int32_t)(tickUs-predictedTargetUs); const uint32_t ae=(uint32_t)(err<0?-err:err); if(ae>_seqRtPredictionErrorMaxUs)_seqRtPredictionErrorMaxUs=ae;
          preScheduled=false;
        } else {
          uint32_t stepUs=periodUs?periodUs*6UL:125000UL;
          if(lastStepClockUs){ const uint32_t m=tickUs-lastStepClockUs; if(m>=20000UL&&m<=2000000UL)stepUs=m; }
          scheduleRealtimeSequencerStep(tickUs,stepUs);
        }
        lastStepClockUs=tickUs;
      } else if(phase==5U && periodUs>=1000U && periodUs<=200000U){
        // One F8 early: enough lead time for AudioTask to place the onset at
        // the exact frame of the block containing the predicted sixth tick.
        predictedTargetUs=tickUs+periodUs;
        scheduleRealtimeSequencerStep(predictedTargetUs,periodUs*6UL);
        preScheduled=true;
      }
    }
  }
}



void PhoenixOS::resetDiagnostics() {
  _audio.resetDiagnostics();
  _diagStartMs = millis();
  _diagLoopAvgUs = 0;
  _diagLoopPeakUs = 0;
  _diagDrawAvgUs = 0;
  _diagDrawPeakUs = 0;
  _diagGuiFps10 = 0;
  _diagLastPublishMs = 0;
  _perfWindowStartMs = millis();
  _perfLoopAccumUs = 0;
  _perfLoopCount = 0;
  _perfLoopMaxUs = 0;
  _perfDrawAccumUs = 0;
  _perfDrawCount = 0;
  _perfDrawMaxUs = 0;
  _seqDroppedClockTicks = 0; _seqClockQueueDrops=0; _seqClockBatchMax=0; _seqStepLateMaxUs=0; _seqStepEventCount=0; _seqRtLateMaxUs=0; _seqRtLateAccumUs=0; _seqRtLateCount=0; _seqRtBatchMax=0; _seqRtPredictionErrorMaxUs=0;
  _seqIdleClockTicksIgnored = 0;
  _seqTransportStartCount = 0;
  _seqTransportContinueCount = 0;
  _seqTransportStopCount = 0;
  _seqPatternTransitionCount = 0;
  _seqGateRecycleCount = 0;
  _seqForcedGateReleaseCount = 0;
  _integrationBankQuiesceCount = 0;
  _integrationBankLoadFailCount = 0;
  _integrationBankSaveFailCount = 0;
  _integrationStaleClockTicksCleared = 0;
  _integrationActiveVoiceGuardCount = 0;
  printDiagnosticsSnapshot("RESET");
}

void PhoenixOS::printDiagnosticsSnapshot(const char *reason) {
  const uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t psramFree = ESP.getFreePsram();
  const uint32_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("V1.0 SNAP reason=%s time=%lus voices=%u/%u vpeak=%u steals=%lu audio_avg=%luus audio_peak=%luus audio_last=%luus risk=%lu overrun=%lu dma=%lu midi_cmd_drop=%lu midi_ctl_drop=%lu gui_avg=%luus gui_peak=%luus fps=%lu.%lu loop_avg=%luus loop_peak=%luus heap_free=%luKB heap_min=%luKB heap_largest=%luKB psram_free=%luKB psram_largest=%luKB | seq_start=%lu seq_cont=%lu seq_stop=%lu seq_pat=%lu seq_tick_drop=%lu seq_idle_clk=%lu seq_gate_recycle=%lu seq_gate_release=%lu | bank_q=%lu load_fail=%lu save_fail=%lu stale_clk=%lu guard=%lu\n",
                reason ? reason : "MANUAL",
                (unsigned long)((millis() - _diagStartMs) / 1000UL),
                (unsigned)_audio.activeVoiceCount(), (unsigned)PhoenixAudioManager::VOICE_COUNT,
                (unsigned)_audio.activeVoicePeak(), (unsigned long)_audio.voiceStealCount(),
                (unsigned long)_audio.audioLoadAvgUs(), (unsigned long)_audio.audioLoadPeakUs(),
                (unsigned long)_audio.audioLoadLastUs(), (unsigned long)_audio.audioRiskCount(),
                (unsigned long)_audio.audioOverrunCount(), (unsigned long)_audio.underruns(),
                (unsigned long)_audio.midiCommandDrops(), (unsigned long)_midiControlDrops,
                (unsigned long)_diagDrawAvgUs, (unsigned long)_diagDrawPeakUs,
                (unsigned long)(_diagGuiFps10 / 10UL), (unsigned long)(_diagGuiFps10 % 10UL),
                (unsigned long)_diagLoopAvgUs, (unsigned long)_diagLoopPeakUs,
                (unsigned long)(internalFree / 1024UL), (unsigned long)(internalMin / 1024UL),
                (unsigned long)(internalLargest / 1024UL), (unsigned long)(psramFree / 1024UL),
                (unsigned long)(psramLargest / 1024UL),
                (unsigned long)_seqTransportStartCount, (unsigned long)_seqTransportContinueCount,
                (unsigned long)_seqTransportStopCount, (unsigned long)_seqPatternTransitionCount,
                (unsigned long)_seqDroppedClockTicks, (unsigned long)_seqIdleClockTicksIgnored, (unsigned long)_seqGateRecycleCount,
                (unsigned long)_seqForcedGateReleaseCount);
}

void PhoenixOS::publishDiagnostics() {
  if (!_screens.diagnosticsVisible()) return;
  const uint32_t now = millis();
  if (_diagLastPublishMs != 0U && (uint32_t)(now - _diagLastPublishMs) < 200UL) return;
  _diagLastPublishMs = now;
  const uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  _screens.setDiagnosticsStatus(now - _diagStartMs,
                                _audio.activeVoiceCount(), PhoenixAudioManager::VOICE_COUNT,
                                _audio.activeVoicePeak(), _audio.audioLoadAvgUs(),
                                _audio.audioLoadPeakUs(), _audio.audioRiskCount(),
                                _audio.audioOverrunCount(), _audio.underruns(),
                                _audio.voiceStealCount(), internalFree / 1024UL,
                                internalMin / 1024UL, ESP.getFreePsram() / 1024UL);
}

void PhoenixOS::updateSequencer() {
  processSequencerMidiTransport();
  refreshRealtimeSequencerSnapshot();
  const uint32_t now = millis();

  const uint8_t currentPattern = _screens.sequencerPattern();
  if (currentPattern != _seqLastPattern) {
    if (_screens.sequencerRunning()) stopSequencerVoices();
    _seqLastPattern = currentPattern;
    portENTER_CRITICAL(&_seqMidiMux); for(uint8_t t=0;t<4;++t)_seqRtPlayhead[t]=0; portEXIT_CRITICAL(&_seqMidiMux);
    _songPatternStepCounter = 0;
    ++_seqPatternTransitionCount;
  }

  if (_screens.sequencerExternalClock()) {
    const bool rtRun=_seqRtRunning;
    _screens.setSequencerRunning(rtRun);
    for(uint8_t t=0;t<4;++t) _screens.setSequencerTrackPlayhead(t,_seqRtPlayhead[t]);
    if(rtRun && !_seqWasRunning){ startSongTestTransport(); _seqWasRunning=true; }
    else if(!rtRun && _seqWasRunning){ pauseSongTransport(); _seqWasRunning=false; }
    const uint32_t done=_seqRtCompletedSteps;
    while(_seqRtLastUiCompletedSteps<done){ ++_seqRtLastUiCompletedSteps; advanceSongPatternCycle(); }
    return;
  }

  const bool running = _screens.sequencerRunning();
  if (!running) {
    if (_seqWasRunning) {
      stopSequencerVoices();
      stopSongTransport();
      _screens.setSequencerPlayhead(0);
    }
    _seqWasRunning = false;
    return;
  }

  uint16_t bpm = _screens.sequencerInternalBpm();
  if (bpm < 40) bpm = 40;
  if (bpm > 240) bpm = 240;
  const uint32_t stepMs = 15000UL / bpm;
  if (!_seqWasRunning) {
    startSongTestTransport();
    _seqLastStepMs = now - stepMs;
    _seqWasRunning = true;
  }
  const uint32_t elapsed = (uint32_t)(now - _seqLastStepMs);
  if (elapsed >= stepMs) {
    // v0.7.36: Recover cleanly after a long blocking operation instead of
    // replaying a burst of overdue steps or carrying permanent timing drift.
    if (elapsed > stepMs * 4UL) _seqLastStepMs = now - stepMs;
    _seqLastStepMs += stepMs;
    const uint8_t st = (uint8_t)(_screens.sequencerPlayhead() & 15U);
    triggerSequencerStep(st, stepMs * 1000UL);
  }
}

void PhoenixOS::update() {
  const uint32_t perfLoopStartUs = micros();
  // C029: apply queued MIDI CC/control changes only from the control task.
  processMidiControlQueue();
  _input.update(_inputState, _events);
  _audio.update(_events);
  routeEvents();

  _screens.update(_inputState);
  {
    const int8_t usbAction = _screens.consumeUsbStorageAction();
    if (usbAction == 0 || usbAction == 2) {
      _audio.stopTransport();
      _audio.allNotesOff();
      stopSequencerVoices();
      stopSongTransport();
      _screens.setSequencerRunning(false);
      const bool rw = (usbAction == 2);
      // begin() performs the exclusive FAT -> raw-block handoff itself.
      const bool ok = rw ? _usbStorage.beginReadWrite() : _usbStorage.beginReadOnly();
      _screens.setUsbStorageStatus(ok, ok, rw, false,
          ok ? (rw ? "READ/WRITE ACTIVE" : "READ ONLY ACTIVE") : "START FAILED");
    } else if (usbAction == 1 || usbAction == 3) {
      if (usbAction == 3) Serial.println(F("C028e USB MSC manual eject confirmation"));
      const bool changed = _usbStorage.writeOccurred();
      const bool ok = _usbStorage.endAndRemount();
      _screens.finishUsbStorageExit(ok, changed);
      if (ok) _screens.setDiskStatus(changed ? "SD UPDATED - LOAD" : "SD READY");
    }
  }

  // While USB owns the card, no Phoenix filesystem, browser, session-save or
  // sample operation may run. Keep only input, the MSC status page and a very
  // low-rate display refresh alive. This also minimizes contention on the SPI
  // bus shared by the SD card and OLED.
  if (_usbStorage.active()) {
    static uint32_t usbLastDrawMs = 0;
    static char usbLastStatus[24] = {0};
    const char *st = _usbStorage.statusText();
    _screens.setUsbStorageStatus(true, true, _usbStorage.writable(), _usbStorage.ejected(), st);
    const uint32_t usbNow = millis();
    if (strncmp(usbLastStatus, st, sizeof(usbLastStatus)) != 0 ||
        (uint32_t)(usbNow - usbLastDrawMs) >= 500UL) {
      strncpy(usbLastStatus, st, sizeof(usbLastStatus));
      usbLastStatus[sizeof(usbLastStatus)-1] = 0;
      usbLastDrawMs = usbNow;
      publishDiagnostics();
      drawScreensTimed();
    }
    updatePerformanceAudit(perfLoopStartUs);
    return;
  }

  if (_screens.consumeDiagnosticsResetRequest()) resetDiagnostics();
  if (_screens.consumeDiagnosticsLogRequest()) printDiagnosticsSnapshot("MANUAL");
  if (!_screens.midiLearnActive()) { _midiLearnActive=false; _midiLearnCcPending=-1; }

  PhoenixParameterId learnId;
  if (_screens.consumeMidiLearnRequest(learnId)) { _midiLearnParameter=(uint8_t)learnId; _midiLearnCcPending=-1; _midiLearnActive=true; }
  if (_midiLearnActive && _midiLearnCcPending >= 0) {
    const uint8_t learnedCc=(uint8_t)_midiLearnCcPending; _midiLearnCcPending=-1;
    const bool ok=phxMidiMapAssign((PhoenixParameterId)_midiLearnParameter,learnedCc);
    _screens.setMidiLearnResult(learnedCc,ok); _midiLearnActive=false;
  }

  if (_screens.consumeSongDataChanged()) { syncScreensToSong(); syncSongToScreens(); }
  if (_screens.consumeSongStopRequest()) { _screens.setSequencerRunning(false); stopSequencerVoices(); stopSongTransport(); _seqWasRunning=false; }
  if (_screens.consumeSongPlayRequest()) {
    if (!_screens.sequencerExternalClock()) { stopSequencerVoices(); startSongTestTransport(); _screens.setSequencerRunning(true); _seqWasRunning=false; }
  }
  if (_screens.consumeSongSaveRequest()) {
    syncScreensToSong();
    char songDir[40];
    if (phxEnsureSD() && phxBuildBankDir(_screens.selectedBank(),songDir,sizeof(songDir))) { SD.mkdir(songDir); _songManager.saveConfig(songDir); }
  }
  _screens.setSongPlaybackStatus(_songManager.playing(),_songManager.position(),_songManager.repeatIndex(),_songManager.currentRepeatTarget());

  uint8_t previewTrack = 0, previewNote = 60, previewVelocity = 100;
  if (_screens.consumeSequencerPreviewRequest(previewTrack, previewNote, previewVelocity)) {
    if (_seqPreviewHeld) _audio.noteOffMidi(_seqPreviewTrack, _seqPreviewNote);
    _audio.startPlaybackMidi(previewTrack, previewNote, previewVelocity);
    _seqPreviewHeld = true;
    _seqPreviewTrack = previewTrack;
    _seqPreviewNote = previewNote;
    _seqPreviewOffMs = millis() + 140UL;
  }
  if (_seqPreviewHeld && (int32_t)(millis() - _seqPreviewOffMs) >= 0) {
    _audio.noteOffMidi(_seqPreviewTrack, _seqPreviewNote);
    _seqPreviewHeld = false;
  }

  updateSequencer();

  int8_t tone = _screens.consumeToneRequest();
  if (tone >= 0) _audio.setTestTone((uint8_t)tone);
  if (_screens.consumeAudioTestChanged()) {
    _audio.setTestGenerator(_screens.audioTestWaveform(),
                            _screens.audioTestFrequencyHz(),
                            _screens.audioTestLevelDb(),
                            _screens.audioTestOutput());
  }

  int8_t slotReq = _screens.consumeSelectSlotRequest();
  if (slotReq >= 0) _audio.selectSlot((uint8_t)slotReq);

  if (_screens.consumePitchChanged()) { _audio.setInstrumentParams(_screens.pitchSlot(), _screens.pitchSemitone(), _screens.pitchFineCent(), _screens.pitchRootNote()); _audio.setKeyboardParams(_screens.pitchSlot(), _screens.keyboardOctave(), _screens.pitchTrackEnabled()); _audio.setPitchBendRange(_screens.pitchSlot(), _screens.pitchBendRange()); }
  if (_screens.consumeEnvelopeChanged()) _audio.setEnvelopeParams(_screens.pitchSlot(), _screens.envelopeAttackMs(), _screens.envelopeDecayMs(), _screens.envelopeSustainPct(), _screens.envelopeReleaseMs());
  if (_screens.consumeQuattroChanged()) { uint8_t lo[4],hi[4],ch[4]; for(uint8_t i=0;i<4;++i){lo[i]=_screens.quattroKeyLow(i);hi[i]=_screens.quattroKeyHigh(i);ch[i]=_screens.quattroMidiChannel(i);} _audio.setQuattroConfig(_screens.quattroMode(),lo,hi,ch); }
  if (_screens.consumeVoiceChanged()) _audio.setVoiceConfig(_screens.pitchSlot(), _screens.voiceMode(), _screens.voiceLimit(), _screens.notePriority(), _screens.glideMs());
  if (_screens.consumeMixerChanged()) { uint8_t sl=_screens.pitchSlot(); _audio.setMixerParams(sl,_screens.mixerLevel(),_screens.mixerPan()); _audio.setEchoSend(sl,_screens.echoSend(sl)); }
  if (_screens.consumeFilterChanged()) _audio.setFilterParams(_screens.pitchSlot(),_screens.filterCutoff(),_screens.filterResonance(),_screens.filterEnvAmount(),_screens.filterVelocityAmount(),_screens.filterKeytrack());
  if (_screens.consumeFilterEnvelopeChanged()) _audio.setFilterEnvelopeParams(_screens.pitchSlot(),_screens.filterAttackMs(),_screens.filterDecayMs(),_screens.filterSustainPct(),_screens.filterReleaseMs());
  if (_screens.consumeMultisampleChanged()) { uint8_t sl=_screens.multisampleSlot(),gr=_screens.multisampleGroup(),ly=_screens.multisampleLayer(); _audio.setKeygroupMapping(sl,gr,_screens.multisampleEnabled(),_screens.multisampleLow(),_screens.multisampleHigh(),_screens.multisampleRoot(),_audio.keygroupLayerLevel(sl,gr,0),_audio.keygroupLayerPan(sl,gr,0)); _audio.setKeygroupLayerMapping(sl,gr,ly,_screens.multisampleLayerEnabled(),_screens.multisampleVelocityLow(),_screens.multisampleVelocityHigh(),_screens.multisampleLevel(),_screens.multisamplePan()); _audio.setKeygroupLayerRoundRobinMode(sl,gr,ly,_screens.multisampleRoundRobinMode()); _audio.setKeygroupLayerLoop(sl,gr,ly,_screens.multisampleLoopMode(),_screens.multisampleLoopStartPct(),_screens.multisampleLoopEndPct(),_screens.multisampleLoopXfadeMs()); _audio.setKeygroupLayerVelocityResponse(sl,gr,ly,_screens.multisampleVelocityToLevelPct(),_screens.multisampleVelocityToFilterPct()); _audio.setKeygroupChokeGroup(sl,gr,_screens.multisampleChokeGroup()); _audio.setKeygroupOneShot(sl,gr,_screens.multisampleOneShot()); _audio.setKeygroupChokeFadeMs(sl,gr,_screens.multisampleChokeFadeMs()); _audio.setKeygroupPlayMode(sl,gr,_screens.multisamplePlayMode()); _audio.setKeygroupExclusiveGroup(sl,gr,_screens.multisampleExclusiveGroup()); _audio.setKeygroupRetriggerLegato(sl,gr,_screens.multisampleRetriggerLegato()); _audio.setKeygroupStartPct(sl,gr,_screens.multisampleStartPct()); _audio.setKeygroupEndPct(sl,gr,_screens.multisampleEndPct()); _audio.setKeygroupReverse(sl,gr,_screens.multisampleReverse()); _audio.setKeygroupTranspose(sl,gr,_screens.multisampleTranspose()); _audio.setKeygroupFineCent(sl,gr,_screens.multisampleFineCent()); _audio.setKeygroupKeytrackPct(sl,gr,_screens.multisampleKeytrackPct()); char dir[40]; if(phxEnsureSD()&&phxBuildBankDir(_screens.selectedBank(),dir,sizeof(dir))){SD.mkdir(dir);_audio.saveMultisampleConfig(dir);} }
  if (_screens.consumeVintageChanged()) _audio.setVintageParams(_screens.pitchSlot(), _screens.vintagePreset(), _screens.vintageSampleRateIndex(), _screens.vintageBitDepthIndex(), _screens.vintageFilterMode(), _screens.vintageJitter());
  if (_screens.consumeEchoChanged()) {
    _audio.setEchoParams(_screens.echoDelayMs(), _screens.echoFeedback(), _screens.echoMix());
    for (uint8_t i = 0; i < PhoenixAudioManager::SLOT_COUNT; ++i) _audio.setEchoSend(i, _screens.echoSend(i));
  }
  if (_screens.consumeLoopChanged()) { _audio.setLoopMode(_screens.loopSlot(), _screens.loopMode()); _audio.setLoopCrossfadeMs(_screens.loopSlot(), _screens.loopCrossfadeMs()); }
  {
    uint8_t snapSlot = 0, snapMarker = 0;
    uint32_t snapFrame = 0;
    if (_screens.consumeZeroCrossSnapRequest(snapSlot, snapMarker, snapFrame)) {
      const int16_t *buf = _audio.slotSampleBuffer(snapSlot);
      const uint32_t n = _audio.slotFrames(snapSlot);
      if (buf && n > 1U) {
        if (snapFrame >= n) snapFrame = n - 1U;
        const uint32_t radius = 2048U;
        uint32_t best = snapFrame;
        bool found = false;
        for (uint32_t d = 0; d <= radius && !found; ++d) {
          const uint32_t cand[2] = { (snapFrame >= d) ? snapFrame - d : 0U, snapFrame + d };
          for (uint8_t k = 0; k < 2U; ++k) {
            const uint32_t c = cand[k];
            if (c == 0U || c >= n) continue;
            const int16_t a = buf[c - 1U], b = buf[c];
            if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) { best = c; found = true; break; }
          }
        }
        if (found) _screens.applyZeroCrossSnap(snapSlot, snapMarker, best);
      }
    }
  }
  {
    uint8_t markerSlot = 0;
    uint32_t sampleStart = 0, loopStart = 0, loopEnd = 0, sampleEnd = 0;
    if (_screens.consumeSampleMarkerEdit(markerSlot, sampleStart, loopStart, loopEnd, sampleEnd)) {
      // C011: one coherent update with the slot captured by the editor. Clear
      // the legacy split flags so the same movement is not applied twice.
      _audio.setSampleMarkers(markerSlot, sampleStart, loopStart, loopEnd, sampleEnd);
      _screens.setBankDirty(true);
      // C018: DC/normalization analysis follows marker editing after a short
      // quiet period, avoiding a full-sample scan for every encoder detent.
      if (_audio.dcCorrectionEnabled(markerSlot) || _audio.normalizeEnabled(markerSlot))
        _sampleProcessingRecalcDue[markerSlot & 3U] = millis() + 150UL;
      (void)_screens.consumeSampleRangeChanged();
      (void)_screens.consumeLoopRangeChanged();
    } else {
      if (_screens.consumeSampleRangeChanged()) _audio.setSampleRange(_screens.loopSlot(), _screens.sampleStart(), _screens.sampleEnd());
      if (_screens.consumeLoopRangeChanged()) _audio.setLoopRange(_screens.loopSlot(), _screens.loopStart(), _screens.loopEnd());
    }
  }
  {
    uint8_t processingSlot = 0;
    const int8_t action = _screens.consumeSampleProcessingAction(processingSlot);
    if (action == 0) {
      _screens.setBankDirty(true);
      if (_audio.trimEnabled(processingSlot)) {
        const bool ok = _audio.undoAutoTrim(processingSlot);
        Serial.printf("SAMPLE EDITOR S%u AUTO TRIM undo=%u\n",
                      (unsigned)(processingSlot + 1U), ok ? 1U : 0U);
      } else {
        const int16_t *buffer = _audio.slotSampleBuffer(processingSlot);
        const uint32_t frames = _audio.slotFrames(processingSlot);
        PhoenixSampleAnalysisResult result;
        bool applied = false;
        if (PhoenixSampleAnalysis::analyze(buffer, frames, result)) {
          // Manual F1 trim treats the two edges independently. A detected
          // leading pause may be removed even when no long trailing silence
          // exists, and vice versa. The complete PCM data stays untouched.
          const uint32_t start = result.startConfirmed ? result.suggestedStart : _audio.sampleStart(processingSlot);
          const uint32_t end = result.endConfirmed ? result.suggestedEnd : _audio.sampleEnd(processingSlot);
          applied = _audio.applyAutoTrim(processingSlot, start, end);
          Serial.printf("SAMPLE EDITOR S%u AUTO TRIM applied=%u start=%lu end=%lu leadConfirmed=%u tailConfirmed=%u threshold=%u confidence=%u\n",
                        (unsigned)(processingSlot + 1U), applied ? 1U : 0U,
                        (unsigned long)start, (unsigned long)end,
                        result.startConfirmed ? 1U : 0U, result.endConfirmed ? 1U : 0U,
                        (unsigned)result.detectionThreshold, (unsigned)result.trimConfidence);
        } else {
          Serial.printf("SAMPLE EDITOR S%u AUTO TRIM analysis failed\n",
                        (unsigned)(processingSlot + 1U));
        }
      }
    } else if (action == 1) {
      _audio.setDcCorrectionEnabled(processingSlot, !_audio.dcCorrectionEnabled(processingSlot));
    } else if (action == 2) {
      _audio.setNormalizeEnabled(processingSlot, !_audio.normalizeEnabled(processingSlot));
    }
  }
  {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < 4U; ++i) {
      const uint32_t due = _sampleProcessingRecalcDue[i];
      if (due != 0U && (int32_t)(now - due) >= 0) {
        _sampleProcessingRecalcDue[i] = 0U;
        _audio.refreshSlotProcessing(i);
      }
    }
  }
  if (_screens.consumeTriggerChanged()) _audio.setTriggerSettings(_screens.triggerAuto(), _screens.triggerLevel());

  int8_t diskAction = _screens.consumeDiskAction();
  if (diskAction >= 0) {
    char bankDir[40];
    const uint8_t currentBank = _screens.selectedBank();
    char msg[24];
    if (!phxEnsureSD()) {
      _screens.setDiskBankUsed(false);
      _screens.setDiskStatus("SD NOT READY");
      if (diskAction == 0) _screens.setRecordingCompleteSaveResult(false);
    } else if (!phxBuildBankDir(currentBank, bankDir, sizeof(bankDir))) {
      _screens.setDiskBankUsed(false);
      _screens.setDiskStatus("BANK PATH ERR");
      if (diskAction == 0) _screens.setRecordingCompleteSaveResult(false);
    } else if (diskAction == 0) {
      _audio.stopTransport();
      snprintf(msg, sizeof(msg), "BANK %02u SAVING...", (unsigned)currentBank);
      _screens.setDiskStatus(msg);
      drawScreensTimed();
      quiesceForBankIo("BANK_SAVE");
      SD.mkdir(bankDir);
      delay(1);
      Serial.printf("V1.0 SAVE PHASE=AUDIO begin bank=%02u\n", (unsigned)currentBank);
      bool ok = _audio.saveBankToSD(bankDir);
      Serial.printf("V1.0 SAVE PHASE=AUDIO %s\n", ok ? "OK" : "FAIL");
      delay(1);
      if (ok) {
        Serial.println("V1.0 SAVE PHASE=PATTERNS begin");
        ok = _screens.saveSequencerConfig(bankDir);
        Serial.printf("V1.0 SAVE PHASE=PATTERNS %s\n", ok ? "OK" : "FAIL");
        delay(1);
      }
      if (ok) {
        syncScreensToSong();
        Serial.println("V1.0 SAVE PHASE=SONG begin");
        ok = _songManager.saveConfig(bankDir);
        Serial.printf("V1.0 SAVE PHASE=SONG %s\n", ok ? "OK" : "FAIL");
        delay(1);
      }
      if (!ok) ++_integrationBankSaveFailCount;
      _screens.setDiskBankUsed(ok || phxBankExists(bankDir));
      snprintf(msg, sizeof(msg), ok ? "BANK %02u SAVED" : "SAVE FAILED", (unsigned)currentBank);
      _screens.setDiskStatus(msg);
      _screens.setRecordingCompleteSaveResult(ok);
      if (ok) _screens.setBankDirty(false);
    } else if (diskAction == 1) {
      if (!phxBankExists(bankDir)) {
        _screens.setDiskBankUsed(false);
        _screens.setDiskStatus("BANK EMPTY");
      } else {
        quiesceForBankIo("BANK_LOAD");
        snprintf(_progressLabel, sizeof(_progressLabel), "LOADING");
        _lastProgressPercent = 255; _lastProgressDrawMs = 0;
        _audio.setProgressCallback(&PhoenixOS::progressCallback, this);
        _screens.setDiskStatus("LOADING 0%");
        drawScreensTimed();
        vTaskDelay(pdMS_TO_TICKS(2));
        Serial.printf("V1.0.2 LOAD PHASE=AUDIO begin bank=%02u\n", (unsigned)currentBank);
        bool ok = _audio.loadBankFromSD(bankDir);
        Serial.printf("V1.0.2 LOAD PHASE=AUDIO %s\n", ok ? "OK" : "FAIL");
        _audio.setProgressCallback(nullptr, nullptr);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (ok) { Serial.println("V1.0.2 LOAD PHASE=PATTERNS begin"); ok = _screens.loadSequencerConfig(bankDir); Serial.printf("V1.0.2 LOAD PHASE=PATTERNS %s\n",ok?"OK":"FAIL"); vTaskDelay(pdMS_TO_TICKS(2)); }
        if (ok) { Serial.println("V1.0.2 LOAD PHASE=SONG begin"); ok = _songManager.loadConfig(bankDir); Serial.printf("V1.0.2 LOAD PHASE=SONG %s\n",ok?"OK":"FAIL"); vTaskDelay(pdMS_TO_TICKS(2)); }
        if (!ok) ++_integrationBankLoadFailCount;
        if (ok) {
          syncSongToScreens();
          _screens.setBankDirty(false);
          { uint8_t send[4]; for (uint8_t i=0;i<4;++i) send[i]=_audio.echoSend(i); _screens.setEchoStatus(_audio.echoDelayMs(), _audio.echoFeedback(), _audio.echoMix(), send); }
          _screens.setTriggerStatus(_audio.triggerAuto(), _audio.triggerLevel());
          _screens.setPlaybackReverseMode(_audio.lastReplayReverse());
          { uint8_t lo[4],hi[4],ch[4]; for(uint8_t i=0;i<4;++i){lo[i]=_audio.quattroKeyLow(i);hi[i]=_audio.quattroKeyHigh(i);ch[i]=_audio.quattroMidiChannel(i);} _screens.setQuattroStatus(_audio.quattroMode(),lo,hi,ch); }
          for (uint8_t i = 0; i < PhoenixAudioManager::SLOT_COUNT; ++i) {
            _screens.setPitchSlotStatus(i, _audio.slotCoarse(i), _audio.slotFine(i), _audio.slotRoot(i), _audio.slotOctave(i), _audio.slotPitchTracking(i), _audio.pitchBendRange(i));
            _screens.setEnvelopeSlotStatus(i, _audio.slotAttackMs(i), _audio.slotDecayMs(i), _audio.slotSustainPct(i), _audio.slotReleaseMs(i));
            _screens.setVintageSlotStatus(i, _audio.slotVintagePreset(i), _audio.slotVintageSampleRate(i), _audio.slotVintageBitDepth(i), _audio.slotVintageFilter(i), _audio.slotVintageJitter(i));
            _screens.setVoiceSlotStatus(i, _audio.slotVoiceMode(i), _audio.slotVoiceLimit(i), _audio.slotNotePriority(i), _audio.slotGlideMs(i));
            _screens.setMixerSlotStatus(i, _audio.slotLevel(i), _audio.slotPan(i), _audio.echoSend(i));
            _screens.setFilterSlotStatus(i,_audio.slotFilterCutoff(i),_audio.slotFilterResonance(i),_audio.slotFilterEnvAmount(i),_audio.slotFilterVelocityAmount(i),_audio.slotFilterKeytrack(i),_audio.slotFilterAttackMs(i),_audio.slotFilterDecayMs(i),_audio.slotFilterSustainPct(i),_audio.slotFilterReleaseMs(i));
          }
          for(uint8_t ms=0;ms<4;++ms){for(uint8_t mg=0;mg<16;++mg){_screens.setMultisampleKeygroupStatus(ms,mg,_audio.keygroupEnabled(ms,mg),_audio.keygroupLow(ms,mg),_audio.keygroupHigh(ms,mg),_audio.keygroupRoot(ms,mg),_audio.keygroupChokeGroup(ms,mg),_audio.keygroupOneShot(ms,mg),_audio.keygroupChokeFadeMs(ms,mg),_audio.keygroupPlayMode(ms,mg),_audio.keygroupExclusiveGroup(ms,mg),_audio.keygroupRetriggerLegato(ms,mg),_audio.keygroupStartPct(ms,mg),_audio.keygroupEndPct(ms,mg),_audio.keygroupReverse(ms,mg),_audio.keygroupTranspose(ms,mg),_audio.keygroupFineCent(ms,mg),_audio.keygroupKeytrackPct(ms,mg));for(uint8_t ml=0;ml<3;++ml){_screens.setMultisampleLayerStatus(ms,mg,ml,_audio.keygroupLayerEnabled(ms,mg,ml),_audio.keygroupLayerVelocityLow(ms,mg,ml),_audio.keygroupLayerVelocityHigh(ms,mg,ml),_audio.keygroupLayerLevel(ms,mg,ml),_audio.keygroupLayerPan(ms,mg,ml),_audio.keygroupLayerPath(ms,mg,ml),_audio.keygroupLayerRoundRobinMode(ms,mg,ml),_audio.keygroupLayerRoundRobinCount(ms,mg,ml),_audio.keygroupLayerLoopMode(ms,mg,ml),_audio.keygroupLayerLoopStartPct(ms,mg,ml),_audio.keygroupLayerLoopEndPct(ms,mg,ml),_audio.keygroupLayerLoopXfadeMs(ms,mg,ml),_audio.keygroupLayerVelocityToLevelPct(ms,mg,ml),_audio.keygroupLayerVelocityToFilterPct(ms,mg,ml));for(uint8_t mr=0;mr<4;++mr)_screens.setMultisampleVariantPath(ms,mg,ml,mr,_audio.keygroupLayerVariantPath(ms,mg,ml,mr));}}}
        }
        _screens.setDiskBankUsed(true);
        snprintf(msg, sizeof(msg), ok ? "BANK %02u LOADED" : "LOAD FAILED", (unsigned)currentBank);
        _screens.setDiskStatus(msg);
      }
    } else if (diskAction == 2) {
      if (!phxBankExists(bankDir)) {
        _screens.setDiskBankUsed(false);
        _screens.setDiskStatus("BANK EMPTY");
      } else {
        _audio.stopTransport();
        snprintf(msg, sizeof(msg), "BANK %02u DELETING", (unsigned)currentBank);
        _screens.setDiskStatus(msg);
        drawScreensTimed();
        bool ok = _audio.eraseBankFromSD(bankDir);
        _screens.setDiskBankUsed(!ok && phxBankExists(bankDir));
        snprintf(msg, sizeof(msg), ok ? "BANK %02u DELETED" : "DELETE FAILED", (unsigned)currentBank);
        _screens.setDiskStatus(msg);
      }
    } else if (diskAction == 3) {
      bool used = phxBankExists(bankDir);
      _screens.setDiskBankUsed(used);
      snprintf(msg, sizeof(msg), "BANK %02u %s", (unsigned)currentBank, used ? "USED" : "EMPTY");
      _screens.setDiskStatus(msg);
    }
  }



  {
    char folder[180]; uint8_t sl=0;
    if(_screens.consumeAutoMapRequest(folder,sizeof(folder),sl)){
      _audio.stopTransport(); _screens.setBrowserStatus("AUTO MAPPING..."); drawScreensTimed();
      char st[24]; bool ok=_audio.autoMapFolder(folder,sl,st,sizeof(st));
      if(ok){char dir[40];if(phxEnsureSD()&&phxBuildBankDir(_screens.selectedBank(),dir,sizeof(dir))){SD.mkdir(dir);_audio.saveMultisampleConfig(dir);}
        for(uint8_t g=0;g<16;++g){_screens.setMultisampleKeygroupStatus(sl,g,_audio.keygroupEnabled(sl,g),_audio.keygroupLow(sl,g),_audio.keygroupHigh(sl,g),_audio.keygroupRoot(sl,g),_audio.keygroupChokeGroup(sl,g),_audio.keygroupOneShot(sl,g),_audio.keygroupChokeFadeMs(sl,g),_audio.keygroupPlayMode(sl,g),_audio.keygroupExclusiveGroup(sl,g),_audio.keygroupRetriggerLegato(sl,g),_audio.keygroupStartPct(sl,g),_audio.keygroupEndPct(sl,g),_audio.keygroupReverse(sl,g),_audio.keygroupTranspose(sl,g),_audio.keygroupFineCent(sl,g),_audio.keygroupKeytrackPct(sl,g));for(uint8_t ml=0;ml<3;++ml){_screens.setMultisampleLayerStatus(sl,g,ml,_audio.keygroupLayerEnabled(sl,g,ml),_audio.keygroupLayerVelocityLow(sl,g,ml),_audio.keygroupLayerVelocityHigh(sl,g,ml),_audio.keygroupLayerLevel(sl,g,ml),_audio.keygroupLayerPan(sl,g,ml),_audio.keygroupLayerPath(sl,g,ml),_audio.keygroupLayerRoundRobinMode(sl,g,ml),_audio.keygroupLayerRoundRobinCount(sl,g,ml),_audio.keygroupLayerLoopMode(sl,g,ml),_audio.keygroupLayerLoopStartPct(sl,g,ml),_audio.keygroupLayerLoopEndPct(sl,g,ml),_audio.keygroupLayerLoopXfadeMs(sl,g,ml),_audio.keygroupLayerVelocityToLevelPct(sl,g,ml),_audio.keygroupLayerVelocityToFilterPct(sl,g,ml));for(uint8_t mr=0;mr<4;++mr)_screens.setMultisampleVariantPath(sl,g,ml,mr,_audio.keygroupLayerVariantPath(sl,g,ml,mr));}}}
      _screens.setBrowserStatus(st[0]?st:(ok?"AUTO MAP DONE":"AUTO MAP FAIL"));
    }
  }

  {
    char folder[180]; uint8_t sl=0;
    if(_screens.consumeMultisampleAutoMapRequest(folder,sizeof(folder),sl)){
      _audio.stopTransport();
      char st[32];
      bool ok=_audio.autoMapMultisampleFolder(folder,sl,st,sizeof(st));
      _screens.setBrowserStatus(st);
      if(ok){
        char dir[40];
        if(phxEnsureSD()&&phxBuildBankDir(_screens.selectedBank(),dir,sizeof(dir))){SD.mkdir(dir);_audio.saveMultisampleConfig(dir);}
        for(uint8_t g=0;g<PhoenixAudioManager::MAX_KEYGROUPS;++g){
          _screens.setMultisampleKeygroupStatus(sl, g,
              _audio.keygroupEnabled(sl, g),
              _audio.keygroupLow(sl, g),
              _audio.keygroupHigh(sl, g),
              _audio.keygroupRoot(sl, g),
              _audio.keygroupChokeGroup(sl, g),
              _audio.keygroupOneShot(sl, g),
              _audio.keygroupChokeFadeMs(sl, g),
              _audio.keygroupPlayMode(sl, g),
              _audio.keygroupExclusiveGroup(sl, g),
              _audio.keygroupRetriggerLegato(sl, g),
              _audio.keygroupStartPct(sl, g),
              _audio.keygroupEndPct(sl, g),
              _audio.keygroupReverse(sl, g),
              _audio.keygroupTranspose(sl, g),
              _audio.keygroupFineCent(sl, g),
              _audio.keygroupKeytrackPct(sl, g));
          for (uint8_t ml = 0; ml < 3; ++ml) {
            _screens.setMultisampleLayerStatus(sl, g, ml,
                _audio.keygroupLayerEnabled(sl, g, ml),
                _audio.keygroupLayerVelocityLow(sl, g, ml),
                _audio.keygroupLayerVelocityHigh(sl, g, ml),
                _audio.keygroupLayerLevel(sl, g, ml),
                _audio.keygroupLayerPan(sl, g, ml),
                _audio.keygroupLayerPath(sl, g, ml),
                _audio.keygroupLayerRoundRobinMode(sl, g, ml),
                _audio.keygroupLayerRoundRobinCount(sl, g, ml),
                _audio.keygroupLayerLoopMode(sl, g, ml),
                _audio.keygroupLayerLoopStartPct(sl, g, ml),
                _audio.keygroupLayerLoopEndPct(sl, g, ml),
                _audio.keygroupLayerLoopXfadeMs(sl, g, ml),
                _audio.keygroupLayerVelocityToLevelPct(sl, g, ml),
                _audio.keygroupLayerVelocityToFilterPct(sl, g, ml));
            for (uint8_t mr = 0; mr < 4; ++mr) {
              _screens.setMultisampleVariantPath(sl, g, ml, mr,
                  _audio.keygroupLayerVariantPath(sl, g, ml, mr));
            }
          }
        }
      }
    }
  }

  {
    uint8_t sl=0,gr=0,ly=0; char p[180];
    if(_screens.consumeKeygroupDeleteRequest(sl,gr)){_audio.removeKeygroup(sl,gr);char dir[40];if(phxEnsureSD()&&phxBuildBankDir(_screens.selectedBank(),dir,sizeof(dir)))_audio.saveMultisampleConfig(dir);_screens.setMultisampleKeygroupStatus(sl,gr,false,0,127,60);for(uint8_t ml=0;ml<3;++ml)_screens.setMultisampleLayerStatus(sl,gr,ml,false,1,0,100,0,"");}
    if(_screens.consumeKeygroupImportRequest(p,sizeof(p),sl,gr,ly)){_audio.stopTransport();char st[32];bool ok=_audio.importWavToKeygroupVariant(p,sl,gr,ly,_screens.multisampleRoundRobinVariant(),st,sizeof(st));if(ok){_audio.setKeygroupMapping(sl,gr,true,_screens.multisampleLow(),_screens.multisampleHigh(),_screens.multisampleRoot(),_audio.keygroupLayerLevel(sl,gr,0),_audio.keygroupLayerPan(sl,gr,0));_audio.setKeygroupLayerMapping(sl,gr,ly,true,_screens.multisampleVelocityLow(),_screens.multisampleVelocityHigh(),_screens.multisampleLevel(),_screens.multisamplePan());char dir[40];if(phxEnsureSD()&&phxBuildBankDir(_screens.selectedBank(),dir,sizeof(dir))){SD.mkdir(dir);_audio.saveMultisampleConfig(dir);}_screens.setMultisampleKeygroupStatus(sl,gr,true,_audio.keygroupLow(sl,gr),_audio.keygroupHigh(sl,gr),_audio.keygroupRoot(sl,gr),_audio.keygroupChokeGroup(sl,gr),_audio.keygroupOneShot(sl,gr),_audio.keygroupChokeFadeMs(sl,gr),_audio.keygroupPlayMode(sl,gr),_audio.keygroupExclusiveGroup(sl,gr),_audio.keygroupRetriggerLegato(sl,gr),_audio.keygroupStartPct(sl,gr),_audio.keygroupEndPct(sl,gr),_audio.keygroupReverse(sl,gr),_audio.keygroupTranspose(sl,gr),_audio.keygroupFineCent(sl,gr),_audio.keygroupKeytrackPct(sl,gr));_screens.setMultisampleLayerStatus(sl,gr,ly,_audio.keygroupLayerEnabled(sl,gr,ly),_audio.keygroupLayerVelocityLow(sl,gr,ly),_audio.keygroupLayerVelocityHigh(sl,gr,ly),_audio.keygroupLayerLevel(sl,gr,ly),_audio.keygroupLayerPan(sl,gr,ly),_audio.keygroupLayerPath(sl,gr,ly),_audio.keygroupLayerRoundRobinMode(sl,gr,ly),_audio.keygroupLayerRoundRobinCount(sl,gr,ly),_audio.keygroupLayerLoopMode(sl,gr,ly),_audio.keygroupLayerLoopStartPct(sl,gr,ly),_audio.keygroupLayerLoopEndPct(sl,gr,ly),_audio.keygroupLayerLoopXfadeMs(sl,gr,ly),_audio.keygroupLayerVelocityToLevelPct(sl,gr,ly),_audio.keygroupLayerVelocityToFilterPct(sl,gr,ly));for(uint8_t mr=0;mr<4;++mr)_screens.setMultisampleVariantPath(sl,gr,ly,mr,_audio.keygroupLayerVariantPath(sl,gr,ly,mr));}
    }
  }

  {
    char importPath[180];
    uint8_t importSlot = 0;
    if (_screens.consumeWavImportRequest(importPath, sizeof(importPath), importSlot)) {
      _audio.stopTransport();
      snprintf(_progressLabel, sizeof(_progressLabel), "IMPORTING S%u", (unsigned)(importSlot + 1));
      _lastProgressPercent = 255; _lastProgressDrawMs = 0;
      _audio.setProgressCallback(&PhoenixOS::progressCallback, this);
      _screens.setBrowserStatus("IMPORTING 0%");
      drawScreensTimed();
      char result[24];
      bool ok = _audio.importWavToSlot(importPath, importSlot, result, sizeof(result));
      _audio.setProgressCallback(nullptr, nullptr);
      _screens.setBrowserStatus(ok ? result : result[0] ? result : "IMPORT FAILED");
      if (ok) _screens.setBankDirty(true);
      _audio.selectSlot(importSlot);
      _screens.setPitchSlotStatus(importSlot, _audio.slotCoarse(importSlot), _audio.slotFine(importSlot), _audio.slotRoot(importSlot), _audio.slotOctave(importSlot), _audio.slotPitchTracking(importSlot));
      _screens.setEnvelopeSlotStatus(importSlot, _audio.slotAttackMs(importSlot), _audio.slotDecayMs(importSlot), _audio.slotSustainPct(importSlot), _audio.slotReleaseMs(importSlot));
      _screens.setVintageSlotStatus(importSlot, _audio.slotVintagePreset(importSlot), _audio.slotVintageSampleRate(importSlot), _audio.slotVintageBitDepth(importSlot), _audio.slotVintageFilter(importSlot), _audio.slotVintageJitter(importSlot));
    }
  }

  // v0.7.13i Sample Preview: non-destructive preview from Sample Browser.
  // F7 toggles preview. File loading is deliberately explicit, never during encoder navigation.
  {
    if (_screens.consumeWavPreviewStopRequest()) {
      _audio.stopPreview();
    }
    char previewPath[180];
    if (_screens.consumeWavPreviewRequest(previewPath, sizeof(previewPath))) {
      if (_audio.previewing()) {
        _audio.stopPreview();
        _screens.setBrowserStatus("PREVIEW STOP");
      } else {
        _screens.setBrowserStatus("LOADING PREV");
        drawScreensTimed();
        char result[24];
        bool ok = _audio.previewWav(previewPath, result, sizeof(result));
        _screens.setBrowserStatus(ok ? "PREVIEW PLAY" : (result[0] ? result : "PREVIEW FAIL"));
      }
    }
  }

  if (_screens.consumeStopRequest()) {
    const bool wasRecording = _audio.recording();
    const uint8_t recordedSlot = _audio.selectedSlot();
    _audio.stopTransport();
    if (wasRecording && _audio.slotRecorded(recordedSlot)) {
      // C017 / Phoenix 1.0: no destructive SAFE/SMART/FORCE processing.
      // The complete recording, including pre-trigger history, remains intact
      // and is edited non-destructively with S.START/L.START/L.END/S.END.
      _sampleAnalysisPending = false;
      _sampleAnalysisSlot = recordedSlot;
    }
  }
  if (_screens.consumeRecordRequest()) _audio.startRecording();
  if (_screens.consumePlayRequest()) _audio.startPlayback();
  {
    uint32_t playFromFrame = 0;
    if (_screens.consumePlayFromCursorRequest(playFromFrame)) _audio.startPlaybackFrom(playFromFrame);
  }
  {
    uint32_t playFromFrame = 0;
    if (_screens.consumeReversePlayFromCursorRequest(playFromFrame)) _audio.startPlaybackReverseFrom(playFromFrame);
  }
  {
    uint32_t st = 0, en = 0; bool rev = false;
    if (_screens.consumeSampleEditorPlayRequest(st, en, rev)) _audio.startPlaybackRange(_audio.selectedSlot(), st, en, rev);
  }
  if (_screens.consumeReversePlayRequest()) _audio.startPlaybackReverse();

  uint32_t midiBytes = _midiCounter ? *_midiCounter : 0;
  _screens.setHardwareStatus(_audio.psramOk(), _audio.psramFreeKb(), _audio.audioOk(), midiBytes, _audio.underruns(), _audio.clipCount());
  _screens.setTransportStatus(_audio.recording(), _audio.playing(), _audio.sampleReady(), _audio.sampleFrames(), _audio.recordFrames(), _audio.sampleCapacityFrames(), _audio.playFrames(), _audio.sampleRate(), _audio.dcOffsetL(), _audio.dcOffsetR(), _audio.selectedSlot());
  _screens.setSamplePlayheadStatus(_audio.samplePlayheadActive(_audio.selectedSlot()), _audio.samplePlayheadFrame(_audio.selectedSlot()));
  {
    const bool activeNow = _audio.activelyRecording();
    if (activeNow && !_recordActivePrev) _recordActiveSlot = _audio.selectedSlot() & 3U;
    if (!activeNow && _recordActivePrev && _audio.slotRecorded(_recordActiveSlot)) {
      _screens.showRecordingComplete(_recordActiveSlot);
    }
    _recordActivePrev = activeNow;
  }
  _screens.setRecorderUxStatus(_audio.waitingForTrigger(), _audio.activelyRecording());
  // C021: safe trigger test observes the exact AUTO threshold without arming
  // or recording. The screen manager only receives a boolean meter result.
  _screens.setTriggerTestInput(_audio.triggerThresholdReached());
  if (_sampleAnalysisPending) {
    _sampleAnalysisPending = false;
    analyzeRecordedSlot(_sampleAnalysisSlot);
  }
  _screens.setPlaybackReverse(_audio.playingReverse());
  {
    bool rec[4], rcd[4], ply[4]; uint32_t frames[4];
    for (uint8_t i=0;i<4;++i){ rcd[i]=_audio.slotRecorded(i); rec[i]=_audio.slotRecording(i); ply[i]=_audio.slotPlaying(i); frames[i]=_audio.slotFrames(i); }
    _screens.setSlotStatus(rcd, rec, ply, frames);
    uint8_t loopMode[4], loopXfade[4]; uint32_t ls[4], le[4], ss[4], se[4];
    for (uint8_t i=0;i<4;++i){ loopMode[i]=_audio.loopMode(i); loopXfade[i]=_audio.loopCrossfadeMs(i); ls[i]=_audio.loopStart(i); le[i]=_audio.loopEnd(i); ss[i]=_audio.sampleStart(i); se[i]=_audio.sampleEnd(i); }
    bool tr[4], dc[4], nm[4]; for(uint8_t i=0;i<4;++i){tr[i]=_audio.trimEnabled(i);dc[i]=_audio.dcCorrectionEnabled(i);nm[i]=_audio.normalizeEnabled(i);} _screens.setSampleProcessingStatus(tr,dc,nm);
    _screens.setLoopModeStatus(loopMode);
    _screens.setLoopCrossfadeStatus(loopXfade);
    _screens.setLoopRangeStatus(ls, le);
    _screens.setSampleRangeStatus(ss, se);
  }

  _gui.setMeter(_audio.peakLevel(), _audio.peakHoldLevel(), _audio.highActive(), _audio.clipActive());
  _gui.setTriggerMarker(_audio.triggerAuto() || _screens.triggerTestActive(), _audio.triggerLevel());
  _gui.setRecordStatus(_audio.waitingForTrigger() ? 1 : (_audio.activelyRecording() ? 2 : 0));
  _gui.setRecording(_audio.recording());
  _gui.setPlaying(_audio.playing());

  bool waveOk = _audio.buildWaveform(_waveBins, sizeof(_waveBins));
  _gui.setWaveform(_waveBins, sizeof(_waveBins), waveOk);

  const uint32_t now = millis();
  if (now - _lastDrawMs > 45) {
    _lastDrawMs = now;
    publishDiagnostics();
    drawScreensTimed();
  }
  updatePerformanceAudit(perfLoopStartUs);
}

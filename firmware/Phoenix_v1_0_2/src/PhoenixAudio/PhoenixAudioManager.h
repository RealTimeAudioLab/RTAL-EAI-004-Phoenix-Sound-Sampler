#pragma once
#include <Arduino.h>
#include "../PhoenixOS/PhoenixEventBus.h"
#include "../PhoenixSystem/PhoenixVersion.h"

// Phoenix 1.0 stability baseline: shared physical pool fixed at 12 voices.
#ifndef PHX_VOICE_COUNT
#define PHX_VOICE_COUNT 12
#endif

#if (PHX_VOICE_COUNT < 1) || (PHX_VOICE_COUNT > 12)
#error "PHX_VOICE_COUNT must be between 1 and 12"
#endif

class PhoenixAudioManager {
public:
  enum TestTone : uint8_t {
    TONE_THRU = 0,
    TONE_SINE,
    TONE_TRIANGLE,
    TONE_SAW,
    TONE_SQUARE,
    TONE_NOISE
  };

  enum AudioState : uint8_t {
    AUDIO_THRU = 0,
    AUDIO_ARMED,
    AUDIO_RECORD,
    AUDIO_PLAY,
    AUDIO_PLAY_REVERSE,
    AUDIO_PREVIEW
  };

  static const uint8_t SLOT_COUNT = 4;
  static const uint8_t VOICE_COUNT = PHX_VOICE_COUNT; // Phoenix 1.0: maximum 12 shared slot-aware voices
  static const uint8_t MAX_KEYGROUPS = 16;

  typedef void (*ProgressCallback)(uint8_t percent, void *context);

  // v0.7.42f: result of destructive Smart Audio Processing.
  struct SmartProcessResult {
    bool success;
    bool trimApplied;
    bool dcRemoved;
    bool normalized;
    bool dataChanged;
    uint32_t originalFrames;
    uint32_t finalFrames;
    uint32_t removedLeadingFrames;
    uint32_t removedTrailingFrames;
    int16_t removedDcOffset;
    uint32_t normalizeGainX1000;
    uint16_t finalPeak;

    SmartProcessResult()
    : success(false), trimApplied(false), dcRemoved(false), normalized(false),
      dataChanged(false), originalFrames(0), finalFrames(0),
      removedLeadingFrames(0), removedTrailingFrames(0),
      removedDcOffset(0), normalizeGainX1000(1000), finalPeak(0) {}
  };

  PhoenixAudioManager(int bclkPin, int lrckPin, int doutPin, int dinPin);
  void setProgressCallback(ProgressCallback cb, void *context) { _progressCallback = cb; _progressContext = context; }

  void begin();
  void update(PhoenixEventBus &events);

  uint8_t peakLevel() const { return _peakLevel; }
  uint8_t peakHoldLevel() const { return _peakHoldLevel; }
  bool clipActive() const { return _clipActive; }
  bool highActive() const { return _highActive; }
  uint32_t clipCount() const { return _clipCount; }

  bool audioOk() const { return _audioOk; }
  bool psramOk() const { return _psramOk; }
  uint32_t psramFreeKb() const { return _psramFreeKb; }
  uint32_t frameCounter() const { return _frameCounter; }
  uint32_t underruns() const { return _underruns; }
  uint8_t activeTestTone() const { return _testTone; }

  void setTestTone(uint8_t tone);
  void setTestGenerator(uint8_t waveform, uint16_t frequencyHz, int8_t levelDb, bool outputOn);
  void clearClip();

  void selectSlot(uint8_t slot);
  uint8_t selectedSlot() const { return _selectedSlot; }

  bool startRecording();                 // active slot
  bool startRecording(uint8_t slot);      // explicit slot
  bool startPlayback();                  // active slot, forward
  bool startPlayback(uint8_t slot);       // explicit slot, forward
  bool startPlaybackMidi(uint8_t slot, uint8_t midiNote, uint8_t velocity); // v0.7.13 chromatic MIDI playback
  bool startPlaybackMidi(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse); // v0.7.14: normal MIDI trigger
  // C029: SPSC MIDI->Audio command ring. The MIDI task never mutates voices directly.
  bool queueMidiNoteOn(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse = false);
  bool queueMidiNoteOff(uint8_t slot, uint8_t midiNote);
  bool queueMidiPitchBend(uint8_t slot, int16_t bend14);
  bool queueMidiSustain(uint8_t slot, bool down);
  bool queueMidiAllNotesOff(uint8_t slot, bool immediate);
  // C036d: realtime sequencer -> Audio/Core1 scheduled command queue.
  bool queueSequencerNote(uint8_t track, uint8_t note, uint8_t velocity, uint32_t gateFrames);
  bool queueSequencerNoteScheduled(uint8_t track, uint8_t note, uint8_t velocity, uint32_t gateFrames, uint32_t targetUs, uint32_t eventId);
  bool queueSequencerStop();
  uint32_t sequencerCommandDrops() const { return _seqCmdDrops; }
  uint32_t sequencerNoteOnFailCount() const { return _seqNoteOnFailCount; }
  uint32_t sequencerOwnerFailCount() const { return _seqOwnerFailCount; }
  uint32_t sequencerOwnerStaleCount() const { return _seqOwnerStaleCount; }
  uint32_t sequencerScheduleMissCount() const { return _seqScheduleMissCount; }
  uint32_t sequencerFrameOffsetMax() const { return _seqFrameOffsetMax; }
  uint32_t midiCommandDrops() const { return _midiCmdDrops; }
  // C029c: low-overhead hotpath profiler. The audio task only updates
  // 32-bit counters; Control/Core0 prints snapshots outside the audio path.
  uint32_t dspProfileSamples() const { return _dspProfileSamples; }
  uint32_t dspSetupEnvUs() const { return _dspSetupEnvUs; }
  uint32_t dspPrepUs() const { return _dspPrepUs; }
  uint32_t dspEnvelopeUs() const { return _dspEnvelopeUs; }
  uint32_t dspFetchXfadeUs() const { return _dspFetchXfadeUs; }
  uint32_t dspVintageUs() const { return _dspVintageUs; }
  uint32_t dspFilterUs() const { return _dspFilterUs; }
  uint32_t dspGainMixUs() const { return _dspGainMixUs; }
  uint32_t dspPositionUs() const { return _dspPositionUs; }
  uint32_t dspMixPosUs() const { return _dspMixPosUs; }
  uint32_t dspEchoOutUs() const { return _dspEchoOutUs; }
  uint32_t voiceHistogramCount(uint8_t n) const { return (n <= VOICE_COUNT) ? _voiceHistCount[n] : 0; }
  uint32_t voiceHistogramAvgUs(uint8_t n) const { return (n <= VOICE_COUNT && _voiceHistCount[n]) ? (_voiceHistSumUs[n] / _voiceHistCount[n]) : 0; }
  uint32_t voiceHistogramMaxUs(uint8_t n) const { return (n <= VOICE_COUNT) ? _voiceHistMaxUs[n] : 0; }
  int8_t startPlaybackMidiOwned(uint8_t slot, uint8_t midiNote, uint8_t velocity); // v0.7.37a: returns concrete voice ID
  int8_t startPlaybackMidiOwned(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse);
  uint32_t voiceOwnershipToken(int8_t voiceId) const; // v0.7.37b: identifies the exact voice instance
  bool releaseOwnedVoice(int8_t voiceId, uint32_t ownershipToken); // release only the owned voice instance
  void noteOffMidi(uint8_t midiNote);
  void noteOffMidi(uint8_t slot, uint8_t midiNote);
  void setPitchBend(uint8_t slot, int16_t bend14); // -8192..8191
  void setPitchBendRange(uint8_t slot, uint8_t semitones);
  uint8_t pitchBendRange(uint8_t slot) const { return _slotPitchBendRange[safeSlot(slot)]; }
  void allNotesOff();
  void allNotesOff(uint8_t slot, bool immediate = false);
  void setSustain(uint8_t slot, bool down);
  bool sustainDown(uint8_t slot) const { return _slotSustainDown[safeSlot(slot)]; }
  uint8_t activeVoiceCount() const { return _activeVoiceCount; }
  uint32_t audioLoadAvgUs() const { return _audioLoadAvgUs; }
  uint32_t audioLoadMaxUs() const { return _audioLoadMaxUs; }
  uint32_t audioLoadLastUs() const { return _audioLoadLastUs; }
  uint32_t audioRiskCount() const { return _audioRiskCount; }
  uint32_t audioOverrunCount() const { return _audioOverrunCount; }
  uint32_t audioLoadPeakUs() const { return _audioLoadPeakUs; }
  uint8_t activeVoicePeak() const { return _activeVoicePeak; }
  uint32_t voiceStealCount() const { return _voiceStealCount; }
  // C031: MIDI/voice robustness diagnostics.
  uint32_t duplicateNoteOnCount() const { return _duplicateNoteOnCount; }
  uint32_t orphanNoteOffCount() const { return _orphanNoteOffCount; }
  uint32_t sustainDeferredNoteOffCount() const { return _sustainDeferredNoteOffCount; }
  uint32_t panicKillCount() const { return _panicKillCount; }
  // C034: Effects/DSP robustness diagnostics. These counters are cumulative
  // until resetDiagnostics() and are updated only by AudioTask/Core1.
  uint32_t echoLimitCount() const { return _echoLimitCount; }
  uint32_t filterGuardCount() const { return _filterGuardCount; }
  uint32_t echoParamSlewCount() const { return _echoParamSlewCount; }
  uint32_t multisampleNoteMissCount() const { return _msNoteMissCount; }
  uint32_t multisampleVelocityMissCount() const { return _msVelocityMissCount; }
  uint32_t multisampleRrFallbackCount() const { return _msRrFallbackCount; }
  uint32_t multisampleSanitizeCount() const { return _msSanitizeCount; }
  void resetDiagnostics();
  bool startPlaybackFrom(uint32_t frame); // active slot, forward from waveform cursor
  bool startPlaybackFrom(uint8_t slot, uint32_t frame); // explicit slot, forward from cursor
  bool startPlaybackReverse();           // active slot, backward
  bool startPlaybackReverse(uint8_t slot); // explicit slot, backward
  bool startPlaybackReverseFrom(uint32_t frame); // active slot, backward from waveform cursor
  bool startPlaybackReverseFrom(uint8_t slot, uint32_t frame); // explicit slot, backward from cursor
  void stopTransport();

  bool recording() const { return _state == AUDIO_RECORD || _state == AUDIO_ARMED; }
  bool waitingForTrigger() const { return _state == AUDIO_ARMED; }
  bool activelyRecording() const { return _state == AUDIO_RECORD; }
  bool playing() const { return _state == AUDIO_PLAY || _state == AUDIO_PLAY_REVERSE || _state == AUDIO_PREVIEW || _testTone != TONE_THRU || _activeVoiceCount > 0; }
  bool playingReverse() const { return _state == AUDIO_PLAY_REVERSE; }
  bool previewing() const { return _state == AUDIO_PREVIEW; }
  uint8_t playingSlot() const { return _playSlot; }
  bool sampleReady() const { return slotRecorded(_selectedSlot); }
  uint32_t sampleFrames() const { return slotFrames(_selectedSlot); }
  uint32_t sampleCapacityFrames() const { return slotCapacityFrames(_selectedSlot); }
  uint32_t recordFrames() const { return _recordPos; }
  uint32_t playFrames() const { return _playPos; }
  bool samplePlayheadActive(uint8_t slot) const;
  uint32_t samplePlayheadFrame(uint8_t slot) const;
  uint32_t sampleRate() const { return 32000UL; }
  void setPitchSemitone(int8_t semitone);
  void setInstrumentParams(uint8_t slot, int8_t semitone, int16_t fineCent, uint8_t rootNote);
  void setKeyboardParams(uint8_t slot, int8_t octaveOffset, bool pitchTracking);
  void setEnvelopeParams(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs);
  void setVintageParams(uint8_t slot, uint8_t preset, uint8_t sampleRateIndex, uint8_t bitDepthIndex, uint8_t filterMode, uint8_t jitter);
  void setQuattroConfig(uint8_t mode, const uint8_t low[4], const uint8_t high[4], const uint8_t channel[4]);
  void setVoiceConfig(uint8_t slot, uint8_t mode, uint8_t limit, uint8_t priority, uint16_t glideMs);
  uint8_t slotVoiceMode(uint8_t slot) const { return _slotVoiceMode[safeSlot(slot)]; }
  uint8_t slotVoiceLimit(uint8_t slot) const { return _slotVoiceLimit[safeSlot(slot)]; }
  uint8_t slotNotePriority(uint8_t slot) const { return _slotNotePriority[safeSlot(slot)]; }
  uint16_t slotGlideMs(uint8_t slot) const { return _slotGlideMs[safeSlot(slot)]; }
  uint8_t quattroMode() const { return _quattroMode; }
  uint8_t quattroKeyLow(uint8_t slot) const { return _quattroKeyLow[safeSlot(slot)]; }
  uint8_t quattroKeyHigh(uint8_t slot) const { return _quattroKeyHigh[safeSlot(slot)]; }
  uint8_t quattroMidiChannel(uint8_t slot) const { return _quattroMidiChannel[safeSlot(slot)]; }
  enum LoopMode : uint8_t {
    LOOP_OFF = 0,
    LOOP_FORWARD = 1,
    LOOP_ALTERNATE = 2
  };

  void setLoopEnabled(uint8_t slot, bool enabled); // compatibility wrapper: OFF/FORWARD
  void setLoopMode(uint8_t slot, uint8_t mode);
  uint8_t loopMode(uint8_t slot) const;
  void setLoopCrossfadeMs(uint8_t slot, uint8_t ms);
  uint8_t loopCrossfadeMs(uint8_t slot) const;
  void setTriggerSettings(bool automatic, uint8_t level);
  bool triggerAuto() const { return _triggerAuto; }
  uint8_t triggerLevel() const { return _triggerLevel; }
  bool triggerThresholdReached() const; // C021 safe trigger test, never starts recording
  bool loopEnabled(uint8_t slot) const;
  uint32_t loopStart(uint8_t slot) const;
  uint32_t loopEnd(uint8_t slot) const;
  void setLoopRange(uint8_t slot, uint32_t start, uint32_t end);
  uint32_t sampleStart(uint8_t slot) const;
  uint32_t sampleEnd(uint8_t slot) const;
  void setSampleRange(uint8_t slot, uint32_t start, uint32_t end);
  void setSampleMarkers(uint8_t slot, uint32_t sampleStart,
                        uint32_t loopStart, uint32_t loopEnd,
                        uint32_t sampleEnd);
  void setSampleGain(uint8_t slot, uint8_t gainPct);
  uint8_t sampleGain(uint8_t slot) const { return _slotSampleGainPct[safeSlot(slot)]; }
  void setVelocityAmount(uint8_t slot, uint8_t amountPct);
  uint8_t velocityAmount(uint8_t slot) const { return _slotVelocityAmount[safeSlot(slot)]; }
  bool normalizeSlot(uint8_t slot);
  // C019: reversible SAMPLE EDITOR processing. TRIM performs automatic
  // silence detection and stores an exact four-marker undo snapshot.
  void setTrimEnabled(uint8_t slot, bool enabled); // compatibility/state setter
  bool applyAutoTrim(uint8_t slot, uint32_t detectedStart, uint32_t detectedEnd);
  bool undoAutoTrim(uint8_t slot);
  bool getTrimUndoMarkers(uint8_t slot, uint32_t &sampleStart,
                          uint32_t &loopStart, uint32_t &loopEnd,
                          uint32_t &sampleEnd) const;
  void setDcCorrectionEnabled(uint8_t slot, bool enabled);
  void setNormalizeEnabled(uint8_t slot, bool enabled);
  bool trimEnabled(uint8_t slot) const;
  bool dcCorrectionEnabled(uint8_t slot) const;
  bool normalizeEnabled(uint8_t slot) const;
  int16_t slotDcOffset(uint8_t slot) const;
  uint32_t slotNormalizeGainQ16(uint8_t slot) const;
  void refreshSlotProcessing(uint8_t slot);
  bool smartProcessSlot(uint8_t slot, uint32_t trimStart, uint32_t trimEnd,
                        bool applyTrim, bool removeDc, bool normalize95,
                        SmartProcessResult &result);
  bool startPlaybackRange(uint8_t slot, uint32_t start, uint32_t end, bool reverse = false);
  int8_t pitchSemitone() const { return _slotCoarse[_selectedSlot & 3]; }
  int16_t pitchFineCent() const { return _slotFine[_selectedSlot & 3]; }
  uint8_t rootNote() const { return _slotRoot[_selectedSlot & 3]; }
  int8_t octaveOffset() const { return _slotOctave[_selectedSlot & 3]; }
  bool pitchTracking() const { return _slotPitchTrack[_selectedSlot & 3]; }
  int8_t slotCoarse(uint8_t slot) const { return _slotCoarse[safeSlot(slot)]; }
  int16_t slotFine(uint8_t slot) const { return _slotFine[safeSlot(slot)]; }
  uint8_t slotRoot(uint8_t slot) const { return _slotRoot[safeSlot(slot)]; }
  int8_t slotOctave(uint8_t slot) const { return _slotOctave[safeSlot(slot)]; }
  bool slotPitchTracking(uint8_t slot) const { return _slotPitchTrack[safeSlot(slot)]; }
  uint16_t slotAttackMs(uint8_t slot) const { return _slotAttackMs[safeSlot(slot)]; }
  uint16_t slotDecayMs(uint8_t slot) const { return _slotDecayMs[safeSlot(slot)]; }
  uint8_t slotSustainPct(uint8_t slot) const { return _slotSustainPct[safeSlot(slot)]; }
  uint16_t slotReleaseMs(uint8_t slot) const { return _slotReleaseMs[safeSlot(slot)]; }
  uint8_t slotVintagePreset(uint8_t slot) const { return _slotVintagePreset[safeSlot(slot)]; }
  uint8_t slotVintageSampleRate(uint8_t slot) const { return _slotVintageSampleRate[safeSlot(slot)]; }
  uint8_t slotVintageBitDepth(uint8_t slot) const { return _slotVintageBitDepth[safeSlot(slot)]; }
  uint8_t slotVintageFilter(uint8_t slot) const { return _slotVintageFilter[safeSlot(slot)]; }
  uint8_t slotVintageJitter(uint8_t slot) const { return _slotVintageJitter[safeSlot(slot)]; }
  bool lastReplayReverse() const { return _lastReplayReverse; }

  void setEchoParams(uint16_t delayMs, uint8_t feedback, uint8_t mix);
  void setEchoSend(uint8_t slot, uint8_t send);
  void setMixerParams(uint8_t slot, uint8_t level, int8_t pan);
  void setFilterParams(uint8_t slot, uint8_t cutoff, uint8_t resonance, int8_t envAmount, uint8_t velocityAmount, uint8_t keytrack);
  void setFilterEnvelopeParams(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs);
  uint16_t echoDelayMs() const { return _echoDelayMs; }
  uint8_t echoFeedback() const { return _echoFeedback; }
  uint8_t echoMix() const { return _echoMix; }
  uint8_t echoSend(uint8_t slot) const { return _slotEchoSend[safeSlot(slot)]; }
  uint8_t slotLevel(uint8_t slot) const { return _slotLevel[safeSlot(slot)]; }
  int8_t slotPan(uint8_t slot) const { return _slotPan[safeSlot(slot)]; }
  uint8_t slotFilterCutoff(uint8_t slot) const { return _slotFilterCutoff[safeSlot(slot)]; }
  uint8_t slotFilterResonance(uint8_t slot) const { return _slotFilterResonance[safeSlot(slot)]; }
  int8_t slotFilterEnvAmount(uint8_t slot) const { return _slotFilterEnvAmount[safeSlot(slot)]; }
  uint8_t slotFilterVelocityAmount(uint8_t slot) const { return _slotFilterVelocityAmount[safeSlot(slot)]; }
  uint8_t slotFilterKeytrack(uint8_t slot) const { return _slotFilterKeytrack[safeSlot(slot)]; }
  uint16_t slotFilterAttackMs(uint8_t slot) const { return _slotFilterAttackMs[safeSlot(slot)]; }
  uint16_t slotFilterDecayMs(uint8_t slot) const { return _slotFilterDecayMs[safeSlot(slot)]; }
  uint8_t slotFilterSustainPct(uint8_t slot) const { return _slotFilterSustainPct[safeSlot(slot)]; }
  uint16_t slotFilterReleaseMs(uint8_t slot) const { return _slotFilterReleaseMs[safeSlot(slot)]; }
  bool echoTailActive() const { return _echoTailActive; }
  AudioState state() const { return (AudioState)_state; }

  bool slotRecorded(uint8_t slot) const;
  bool slotRecording(uint8_t slot) const;
  bool slotPlaying(uint8_t slot) const;
  uint32_t slotFrames(uint8_t slot) const;
  const int16_t *slotSampleBuffer(uint8_t slot) const; // v0.7.42a: read-only analysis access
  uint32_t slotCapacityFrames(uint8_t slot) const;

  int16_t dcOffsetL() const { return _dcOffsetL; }
  int16_t dcOffsetR() const { return _dcOffsetR; }

  bool buildWaveform(uint8_t *dst, uint8_t count) const;
  bool buildWaveform(uint8_t slot, uint8_t *dst, uint8_t count) const;

  // v0.6.9 Disk Utilities, Classic Phase 1.
  bool saveBankToSD(const char *dir);
  bool loadBankFromSD(const char *dir);
  bool eraseBankFromSD(const char *dir);
  bool loadProjectMetadataFromSD(const char *dir); // v0.7.13c FIX4
  bool importWavToSlot(const char *path, uint8_t slot, char *status, size_t statusLen); // v0.7.13i
  bool importWavToKeygroup(const char *path, uint8_t slot, uint8_t group, char *status, size_t statusLen); // compatibility: layer 1
  bool importWavToKeygroupLayer(const char *path, uint8_t slot, uint8_t group, uint8_t layer, char *status, size_t statusLen);
  bool importWavToKeygroupVariant(const char *path, uint8_t slot, uint8_t group, uint8_t layer, uint8_t variant, char *status, size_t statusLen);
  bool autoMapMultisampleFolder(const char *folder, uint8_t slot, char *status, size_t statusLen);
  bool autoMapFolder(const char *folder, uint8_t slot, char *status, size_t statusLen);
  bool loadMultisampleConfig(const char *dir);
  bool saveMultisampleConfig(const char *dir) const;
  void clearMultisample(uint8_t slot);
  bool keygroupEnabled(uint8_t slot, uint8_t group) const;
  uint8_t keygroupCount(uint8_t slot) const;
  uint8_t keygroupLow(uint8_t slot, uint8_t group) const;
  uint8_t keygroupHigh(uint8_t slot, uint8_t group) const;
  uint8_t keygroupRoot(uint8_t slot, uint8_t group) const;
  uint8_t keygroupChokeGroup(uint8_t slot, uint8_t group) const;
  bool keygroupOneShot(uint8_t slot, uint8_t group) const;
  uint16_t keygroupChokeFadeMs(uint8_t slot, uint8_t group) const;
  uint8_t keygroupPlayMode(uint8_t slot, uint8_t group) const;
  uint8_t keygroupExclusiveGroup(uint8_t slot, uint8_t group) const;
  bool keygroupRetriggerLegato(uint8_t slot, uint8_t group) const;
  uint8_t keygroupStartPct(uint8_t slot, uint8_t group) const;
  uint8_t keygroupEndPct(uint8_t slot, uint8_t group) const;
  bool keygroupReverse(uint8_t slot, uint8_t group) const;
  uint32_t keygroupFrames(uint8_t slot, uint8_t group) const; // compatibility: layer 1
  uint8_t keygroupLevel(uint8_t slot, uint8_t group) const; // compatibility: layer 1
  int8_t keygroupPan(uint8_t slot, uint8_t group) const; // compatibility: layer 1
  const char* keygroupPath(uint8_t slot, uint8_t group) const; // compatibility: layer 1
  bool keygroupLayerEnabled(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerVelocityLow(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerVelocityHigh(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint32_t keygroupLayerFrames(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerLevel(uint8_t slot, uint8_t group, uint8_t layer) const;
  int8_t keygroupLayerPan(uint8_t slot, uint8_t group, uint8_t layer) const;
  const char* keygroupLayerPath(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerRoundRobinMode(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerRoundRobinCount(uint8_t slot, uint8_t group, uint8_t layer) const;
  uint8_t keygroupLayerLoopMode(uint8_t slot,uint8_t group,uint8_t layer) const;
  uint8_t keygroupLayerLoopStartPct(uint8_t slot,uint8_t group,uint8_t layer) const;
  uint8_t keygroupLayerLoopEndPct(uint8_t slot,uint8_t group,uint8_t layer) const;
  uint8_t keygroupLayerLoopXfadeMs(uint8_t slot,uint8_t group,uint8_t layer) const;
  uint8_t keygroupLayerVelocityToLevelPct(uint8_t slot,uint8_t group,uint8_t layer) const;
  uint8_t keygroupLayerVelocityToFilterPct(uint8_t slot,uint8_t group,uint8_t layer) const;
  int8_t keygroupTranspose(uint8_t slot,uint8_t group) const;
  int16_t keygroupFineCent(uint8_t slot,uint8_t group) const;
  uint8_t keygroupKeytrackPct(uint8_t slot,uint8_t group) const;
  const char* keygroupLayerVariantPath(uint8_t slot, uint8_t group, uint8_t layer, uint8_t variant) const;
  bool setKeygroupLayerRoundRobinMode(uint8_t slot, uint8_t group, uint8_t layer, uint8_t mode);
  void setKeygroupLayerLoop(uint8_t slot,uint8_t group,uint8_t layer,uint8_t mode,uint8_t startPct,uint8_t endPct,uint8_t xfadeMs);
  void setKeygroupLayerVelocityResponse(uint8_t slot,uint8_t group,uint8_t layer,uint8_t velToLevelPct,uint8_t velToFilterPct);
  void setKeygroupMapping(uint8_t slot, uint8_t group, bool enabled, uint8_t low, uint8_t high, uint8_t root, uint8_t level, int8_t pan); // compatibility: layer 1
  void setKeygroupChokeGroup(uint8_t slot, uint8_t group, uint8_t chokeGroup);
  void setKeygroupOneShot(uint8_t slot, uint8_t group, bool oneShot);
  void setKeygroupChokeFadeMs(uint8_t slot, uint8_t group, uint16_t fadeMs);
  void setKeygroupPlayMode(uint8_t slot, uint8_t group, uint8_t playMode);
  void setKeygroupExclusiveGroup(uint8_t slot, uint8_t group, uint8_t exclusiveGroup);
  void setKeygroupRetriggerLegato(uint8_t slot, uint8_t group, bool legato);
  void setKeygroupStartPct(uint8_t slot, uint8_t group, uint8_t pct);
  void setKeygroupEndPct(uint8_t slot, uint8_t group, uint8_t pct);
  void setKeygroupReverse(uint8_t slot, uint8_t group, bool reverse);
  void setKeygroupTranspose(uint8_t slot, uint8_t group, int8_t semitone);
  void setKeygroupFineCent(uint8_t slot, uint8_t group, int16_t cents);
  void setKeygroupKeytrackPct(uint8_t slot, uint8_t group, uint8_t pct);
  void setKeygroupLayerMapping(uint8_t slot, uint8_t group, uint8_t layer, bool enabled, uint8_t velocityLow, uint8_t velocityHigh, uint8_t level, int8_t pan);
  void removeKeygroup(uint8_t slot, uint8_t group);
  uint32_t sanitizeMultisampleSlot(uint8_t slot);
  bool previewWav(const char *path, char *status, size_t statusLen); // v0.7.13j fast preview
  void stopPreview();

private:
  bool writeMultisampleConfigFile(const char *path, bool *anyOut) const; // C030 transactional staging helper
  struct SampleSlot {
    int16_t *buffer;
    uint32_t capacityFrames;
    volatile uint32_t frames;
    volatile bool recorded;
    volatile bool loopEnabled;
    uint32_t sampleStart;
    uint32_t sampleEnd;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint8_t loopMode;
    uint8_t loopXfadeMs;
    bool trimEnabled;
    bool trimUndoValid;
    uint32_t trimUndoSampleStart;
    uint32_t trimUndoLoopStart;
    uint32_t trimUndoLoopEnd;
    uint32_t trimUndoSampleEnd;
    bool dcCorrectionEnabled;
    bool normalizeEnabled;
    int16_t dcOffset;
    uint32_t normalizeGainQ16;
    SampleSlot() : buffer(nullptr), capacityFrames(0), frames(0), recorded(false), loopEnabled(false), sampleStart(0), sampleEnd(0), loopStart(0), loopEnd(0), loopMode(0), loopXfadeMs(0), trimEnabled(false), trimUndoValid(false), trimUndoSampleStart(0), trimUndoLoopStart(0), trimUndoLoopEnd(0), trimUndoSampleEnd(0), dcCorrectionEnabled(false), normalizeEnabled(false), dcOffset(0), normalizeGainQ16(65536UL) {}
  };

  static const uint8_t MAX_VELOCITY_LAYERS = 3;
  static const uint8_t MAX_RR_VARIANTS = 4;

  struct RoundRobinVariant {
    int16_t *buffer;
    uint32_t frames;
    char path[128];
    RoundRobinVariant() : buffer(nullptr), frames(0) { path[0]=0; }
  };

  struct VelocityLayer {
    RoundRobinVariant rr[MAX_RR_VARIANTS];
    bool enabled;
    uint8_t velocityLow;
    uint8_t velocityHigh;
    uint8_t level;
    int8_t pan;
    uint16_t gainLQ15;
    uint16_t gainRQ15;
    uint8_t rrMode; // 0 OFF, 2/3/4 cyclic variants, 5 RANDOM
    uint8_t rrCounter;
    uint8_t loopMode;       // 0 OFF, 1 FORWARD, 2 PING-PONG
    uint8_t loopStartPct;   // relative to selected sample range
    uint8_t loopEndPct;     // relative to selected sample range
    uint8_t loopXfadeMs;    // 0..50 ms, used by FORWARD
    uint8_t velocityToLevelPct;  // 0..100, 100 = classic full velocity volume
    uint8_t velocityToFilterPct; // 0..100, additional keygroup/layer filter opening
    VelocityLayer() : enabled(false), velocityLow(1), velocityHigh(127), level(100), pan(0), gainLQ15(23170), gainRQ15(23170), rrMode(0), rrCounter(0), loopMode(0), loopStartPct(20), loopEndPct(80), loopXfadeMs(0), velocityToLevelPct(100), velocityToFilterPct(0) {}
  };

  struct MultisampleKeygroup {
    bool enabled;
    uint8_t lowNote;
    uint8_t highNote;
    uint8_t rootNote;
    uint8_t chokeGroup; // 0 OFF, 1..8 mutually exclusive voice groups
    bool oneShot;         // true: MIDI Note Off is ignored until sample end or choke
    uint16_t chokeFadeMs; // 0 immediate, otherwise short anti-click fade
    uint8_t playMode;      // 0 POLY, 1 MONO, 2 EXCLUSIVE
    uint8_t exclusiveGroup; // 0 OFF, 1..8
    bool retriggerLegato;  // true: active same keygroup is not restarted
    uint8_t startPct;       // 0..99 percent of source
    uint8_t endPct;         // 1..100 percent of source
    bool reversePlayback;   // play selected range backwards
    int8_t transposeSemitone; // -24..+24 semitones per keygroup
    int16_t fineCent;         // -100..+100 cents per keygroup
    uint8_t keytrackPct;      // 0..100 %, applied when slot pitch tracking is enabled
    VelocityLayer layer[MAX_VELOCITY_LAYERS];
    MultisampleKeygroup() : enabled(false), lowNote(0), highNote(127), rootNote(60), chokeGroup(0), oneShot(false), chokeFadeMs(0), playMode(0), exclusiveGroup(0), retriggerLegato(false), startPct(0), endPct(100), reversePlayback(false), transposeSemitone(0), fineCent(0), keytrackPct(100) {
      layer[0].velocityLow=1; layer[0].velocityHigh=127;
      layer[1].velocityLow=1; layer[1].velocityHigh=0;
      layer[2].velocityLow=1; layer[2].velocityHigh=0;
    }
  };

  struct InstrumentVoice {
    volatile bool active;
    volatile bool releasing;
    volatile bool keyHeld;
    volatile bool sustained;
    uint8_t envStage; // 0 attack, 1 decay, 2 sustain, 3 release
    uint8_t slot;
    uint8_t note;
    uint8_t velocity;
    uint8_t velocityLevel;
    uint8_t velocityFilterAmount;
    uint8_t keygroup;
    uint8_t velocityLayer;
    const int16_t *sourceBuffer;
    uint32_t sourceFrames;
    uint16_t sourceGainLQ15;
    uint16_t sourceGainRQ15;
    bool reverse;
    uint64_t posQ16;
    uint32_t incQ16;
    uint32_t targetIncQ16;
    int32_t glideStepQ16;
    uint32_t glideFramesLeft;
    uint32_t baseIncQ16;
    uint32_t rangeStart;
    uint32_t rangeEnd;
    uint32_t loopStartFrame;
    uint32_t loopEndFrame;
    uint16_t loopXfadeFrames;
    uint8_t loopMode;
    bool loopForward;
    uint32_t markerRevision;
    uint16_t envQ15;
    uint16_t sustainQ15;
    uint16_t attackStepQ15;
    uint16_t decayStepQ15;
    uint16_t releaseStepQ15;
    uint32_t age;
    uint32_t vintagePhase;
    int16_t vintageHold;
    int32_t vintageFilterState;
    uint32_t vintageNoise;
    uint8_t filterEnvStage;
    uint16_t filterEnvQ15;
    uint16_t filterEnvSustainQ15;
    uint16_t filterAttackStepQ15;
    uint16_t filterDecayStepQ15;
    uint16_t filterReleaseStepQ15;
    int32_t filterLow;
    int32_t filterBand;
    uint16_t seqStartDelayFrames; // C036d: sample-accurate scheduled onset inside current block
    InstrumentVoice() : active(false), releasing(false), keyHeld(false), sustained(false), envStage(0), slot(0), note(0), velocity(0), velocityLevel(127), velocityFilterAmount(0), keygroup(255), velocityLayer(0), sourceBuffer(nullptr), sourceFrames(0), sourceGainLQ15(32767), sourceGainRQ15(32767), reverse(false), posQ16(0), incQ16(65536), targetIncQ16(65536), glideStepQ16(0), glideFramesLeft(0), baseIncQ16(65536), rangeStart(0), rangeEnd(0), loopStartFrame(0), loopEndFrame(0), loopXfadeFrames(0), loopMode(0), loopForward(true), markerRevision(0), envQ15(0), sustainQ15(32767), attackStepQ15(205), decayStepQ15(1), releaseStepQ15(13), age(0), vintagePhase(0), vintageHold(0), vintageFilterState(0), vintageNoise(0xA5A55A5AUL), filterEnvStage(0), filterEnvQ15(0), filterEnvSustainQ15(0), filterAttackStepQ15(32767), filterDecayStepQ15(1), filterReleaseStepQ15(1), filterLow(0), filterBand(0), seqStartDelayFrames(0) {}
  };

  enum MidiAudioCommandType : uint8_t {
    MIDI_CMD_NOTE_ON = 1, MIDI_CMD_NOTE_OFF, MIDI_CMD_PITCH_BEND,
    MIDI_CMD_SUSTAIN, MIDI_CMD_ALL_NOTES_OFF
  };
  struct MidiAudioCommand {
    uint8_t type;
    uint8_t slot;
    uint8_t note;
    uint8_t velocity;
    int16_t value;
    uint8_t flags;
  };
  static const uint8_t MIDI_CMD_CAPACITY = 64;
  MidiAudioCommand _midiCmdRing[MIDI_CMD_CAPACITY];
  volatile uint8_t _midiCmdWrite = 0;
  volatile uint8_t _midiCmdRead = 0;
  volatile uint32_t _midiCmdDrops = 0;
  bool enqueueMidiCommand(const MidiAudioCommand &cmd);
  void processMidiCommandQueue();

  enum SequencerAudioCommandType : uint8_t { SEQ_CMD_NOTE = 1, SEQ_CMD_STOP = 2 };
  struct SequencerAudioCommand { uint8_t type, track, note, velocity; uint32_t gateFrames; uint32_t targetUs; uint32_t eventId; };
  struct SequencerAudioGate { bool active; int8_t voiceId; uint32_t ownershipToken; uint32_t framesLeft; uint32_t eventId; };
  static const uint8_t SEQ_CMD_CAPACITY = 64;
  static const uint8_t SEQ_GATES_PER_TRACK = 8;
  SequencerAudioCommand _seqCmdRing[SEQ_CMD_CAPACITY];
  volatile uint8_t _seqCmdWrite = 0, _seqCmdRead = 0;
  volatile uint32_t _seqCmdDrops = 0, _seqNoteOnFailCount = 0, _seqOwnerFailCount = 0;
  volatile uint32_t _seqOwnerStaleCount = 0, _seqScheduleMissCount = 0, _seqFrameOffsetMax = 0;
  SequencerAudioGate _seqAudioGate[4][SEQ_GATES_PER_TRACK];
  bool enqueueSequencerCommand(const SequencerAudioCommand &cmd);
  void processSequencerCommandQueue(uint32_t blockStartUs, uint32_t blockFrames);
  void advanceSequencerGates(uint32_t frames);
  void releaseAllSequencerGates();

  static void audioTaskThunk(void *arg);
  void audioTask();
  bool beginI2S();
  bool allocateKeygroupMetadata();
  bool allocateSampleBuffers();
  bool allocateEchoBuffer();
  bool allocatePreviewBuffer();
  void resetSlotForNewRecording(uint8_t slot);
  void finalizeRecordedSlot(uint8_t slot);
  void updatePeakAndClip(uint32_t raw);
  int32_t generateToneSample();
  int16_t processEcho(int16_t dry, int16_t send);
  void clearEchoBuffer();
  void updatePlaybackIncrement(uint8_t slot);
  uint32_t triggerThresholdRaw() const;
  bool startPlaybackInternal(uint8_t slot, uint32_t frame, bool reverse, bool clearDelay);
  void startEchoTail();
  void stopEchoTail();
  int findVoiceForNote(uint8_t slot, uint8_t midiNote) const;
  bool startMultisampleMidi(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse);
  int startMultisampleMidiOwned(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse);
  bool startKeygroupVoice(uint8_t slot, uint8_t group, uint8_t layer, uint8_t midiNote, uint8_t velocity, bool reverse);
  int startKeygroupVoiceOwned(uint8_t slot, uint8_t group, uint8_t layer, uint8_t midiNote, uint8_t velocity, bool reverse);
  int startPlaybackMidiVoice(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse, bool forceNewVoice);
  void updateVelocityLayerPan(VelocityLayer &layer);
  int allocateVoice();
  int allocateVoiceForSlot(uint8_t slot);
  int findActiveVoiceForSlot(uint8_t slot) const;
  int selectHeldNote(uint8_t slot) const;
  void retuneMonoVoice(uint8_t slot, uint8_t note, uint8_t velocity, bool retrigger);
  void refreshActiveVoiceCount();
  void updateSlotVoicePlayheads();
  void beginVoiceRelease(InstrumentVoice &voice);
  int findOldestHeldVoiceForNote(uint8_t slot, uint8_t midiNote) const;
  void normalizeSlotMarkers(SampleSlot &slot);
  uint32_t effectiveSampleStart(const SampleSlot &slot) const;
  uint32_t effectiveSampleEnd(const SampleSlot &slot) const;
  void resolveSlotPlaybackMarkers(uint8_t slot, uint32_t &sampleStart, uint32_t &sampleEnd,
                                  uint32_t &loopStart, uint32_t &loopEnd, uint8_t &loopMode) const;
  void calculateSlotProcessing(SampleSlot &slot);
  void applySlotMarkersToActivePlayback(uint8_t slot, bool restartAtSampleStart);
  void syncVoiceMarkersFromSlot(InstrumentVoice &voice, bool restartAtSampleStart);
  void bumpSlotMarkerRevision(uint8_t slot);
  void printVoiceAudit();
  int16_t processVintage(uint8_t slot, int16_t input, uint32_t &phase, int16_t &hold, int32_t &filterState, uint32_t &noise);
  int16_t processVoiceFilter(InstrumentVoice &v, int16_t input);
  uint8_t safeSlot(uint8_t slot) const { return (slot < SLOT_COUNT) ? slot : 0; }

  int _bclkPin;
  int _lrckPin;
  int _doutPin;
  int _dinPin;

  volatile uint8_t _testTone;
  volatile uint8_t _state;
  volatile uint8_t _peakLevel;
  volatile uint8_t _peakHoldLevel;
  uint8_t _lastPeakLevel;
  volatile bool _clipActive;
  volatile bool _highActive;
  volatile bool _audioOk;
  volatile bool _psramOk;
  volatile uint32_t _psramFreeKb;
  volatile uint32_t _frameCounter;
  volatile uint32_t _underruns;
  volatile uint32_t _lastPeakRaw;
  volatile uint32_t _clipCount;
  volatile int16_t _dcOffsetL;
  volatile int16_t _dcOffsetR;

  uint32_t _lastTickMs;
  uint32_t _clipHoldUntilMs;
  uint32_t _highHoldUntilMs;
  uint32_t _peakHoldUntilMs;
  uint32_t _peakReleaseMs;

  uint32_t _phase;
  uint32_t _noise;
  volatile uint16_t _testFrequencyHz;
  volatile int8_t _testLevelDb;
  volatile int32_t _testAmpQ15;

  SampleSlot _slots[SLOT_COUNT];
  // C014: large, non-real-time multisample metadata lives in PSRAM instead
  // of consuming about 110 kB of static internal DRAM. Indexing remains
  // _keygroups[slot][group]. Audio-rate voice state stays internal.
  MultisampleKeygroup (*_keygroups)[MAX_KEYGROUPS];
  volatile uint8_t _selectedSlot;
  volatile uint8_t _recordSlot;
  volatile uint8_t _playSlot;
  volatile uint32_t _recordPos;
  static const uint16_t PRE_TRIGGER_FRAMES = 1024; // 32 ms at 32 kHz
  int16_t _preTrigger[PRE_TRIGGER_FRAMES];
  uint16_t _preTriggerWrite;
  uint16_t _preTriggerCount;
  volatile uint32_t _playPos;
  volatile uint64_t _playPosQ16;
  volatile uint32_t _playIncQ16;
  volatile bool _playRangeActive;
  volatile uint32_t _playRangeStart;
  volatile uint32_t _playRangeEnd;
  volatile bool _transportLoopForward;
  volatile bool _lastReplayReverse;
  volatile bool _triggerAuto;
  volatile uint8_t _triggerLevel;
  volatile int8_t _pitchSemitone;
  int8_t _slotCoarse[SLOT_COUNT];
  int16_t _slotFine[SLOT_COUNT];
  uint8_t _slotRoot[SLOT_COUNT];
  uint8_t _slotPitchBendRange[SLOT_COUNT];
  int16_t _slotPitchBend[SLOT_COUNT];
  int8_t _slotOctave[SLOT_COUNT];
  bool _slotPitchTrack[SLOT_COUNT];
  uint16_t _slotAttackMs[SLOT_COUNT];
  uint16_t _slotDecayMs[SLOT_COUNT];
  uint8_t _slotSustainPct[SLOT_COUNT];
  uint16_t _slotReleaseMs[SLOT_COUNT];
  bool _slotSustainDown[SLOT_COUNT];
  uint8_t _slotVintagePreset[SLOT_COUNT];
  uint8_t _slotVintageSampleRate[SLOT_COUNT];
  uint8_t _slotVintageBitDepth[SLOT_COUNT];
  uint8_t _slotVintageFilter[SLOT_COUNT];
  uint8_t _slotVintageJitter[SLOT_COUNT];
  uint8_t _slotEchoSend[SLOT_COUNT];
  uint8_t _slotSampleGainPct[SLOT_COUNT];
  uint8_t _slotVelocityAmount[SLOT_COUNT];
  uint8_t _slotLevel[SLOT_COUNT];
  int8_t _slotPan[SLOT_COUNT];
  uint16_t _slotGainLQ15[SLOT_COUNT];
  uint16_t _slotGainRQ15[SLOT_COUNT];
  uint8_t _slotFilterCutoff[SLOT_COUNT];
  uint8_t _slotFilterResonance[SLOT_COUNT];
  int8_t _slotFilterEnvAmount[SLOT_COUNT];
  uint8_t _slotFilterVelocityAmount[SLOT_COUNT];
  uint8_t _slotFilterKeytrack[SLOT_COUNT];
  uint16_t _slotFilterAttackMs[SLOT_COUNT];
  uint16_t _slotFilterDecayMs[SLOT_COUNT];
  uint8_t _slotFilterSustainPct[SLOT_COUNT];
  uint16_t _slotFilterReleaseMs[SLOT_COUNT];
  uint8_t _slotVoiceMode[SLOT_COUNT];       // 0 POLY, 1 MONO, 2 LEGATO
  uint8_t _slotVoiceLimit[SLOT_COUNT];      // 1..12, POLY only
  uint8_t _slotNotePriority[SLOT_COUNT];    // 0 LAST, 1 LOW, 2 HIGH
  uint16_t _slotGlideMs[SLOT_COUNT];        // 0..2000 ms
  bool _heldNotes[SLOT_COUNT][128];
  // C031: MIDI key-down depth for overlapping equal Note On events.
  uint8_t _noteHoldCount[SLOT_COUNT][128];
  uint8_t _heldVelocity[SLOT_COUNT][128];
  uint32_t _heldAge[SLOT_COUNT][128];
  uint32_t _heldAgeCounter;
  uint8_t _quattroMode;
  uint8_t _quattroKeyLow[SLOT_COUNT];
  uint8_t _quattroKeyHigh[SLOT_COUNT];
  uint8_t _quattroMidiChannel[SLOT_COUNT];
  uint32_t _transportVintagePhase;
  int16_t _transportVintageHold;
  int32_t _transportVintageFilterState;
  uint32_t _transportVintageNoise;

  InstrumentVoice _voices[VOICE_COUNT];
  volatile bool _slotVoicePlayheadActive[SLOT_COUNT];
  volatile uint32_t _slotVoicePlayheadFrame[SLOT_COUNT];
  // C010: identify the exact newest base-sample MIDI voice that owns the
  // SAMPLE EDITOR playhead. The age token prevents a stolen/reused voice
  // index from being mistaken for the previous note.
  volatile int8_t _slotVoicePlayheadVoice[SLOT_COUNT];
  volatile uint32_t _slotVoicePlayheadAge[SLOT_COUNT];
  volatile uint32_t _slotMarkerRevision[SLOT_COUNT];
  volatile uint8_t _activeVoiceCount;
  volatile uint8_t _activeVoicePeak;
  uint32_t _voiceAgeCounter;

  // v0.7.39j Voice Allocation Audit
  uint32_t _voiceAllocCount[VOICE_COUNT];
  uint32_t _voiceStealCount;
  uint32_t _duplicateNoteOnCount;
  uint32_t _orphanNoteOffCount;
  uint32_t _sustainDeferredNoteOffCount;
  uint32_t _panicKillCount;
  uint8_t _lastAllocatorScanMax;
  uint8_t _highestAllocatorScanMax;
  uint8_t _lastAllocatedVoice;
  uint8_t _lastAllocationReason; // 0 free, 1 release steal, 2 oldest steal, 3 slot-limit reuse
  uint32_t _voiceAuditPrintMs;

  volatile uint32_t _audioLoadAvgUs;
  volatile uint32_t _audioLoadMaxUs;
  volatile uint32_t _audioLoadLastUs;
  volatile uint32_t _audioRiskCount;
  volatile uint32_t _audioOverrunCount;
  volatile uint32_t _audioLoadPeakUs;
  uint64_t _audioLoadAccumUs;
  uint32_t _audioLoadBlocks;
  uint32_t _audioLoadWindowStartMs;
  uint32_t _audioStatsPrintMs;

  // C029d profiler fields. Component values are the latest sampled block
  // converted from CPU cycles to microseconds. Histogram values are cumulative.
  volatile uint32_t _dspProfileSamples;
  volatile uint32_t _dspSetupEnvUs; // compatibility aggregate: prep+env+fetch/xfade
  volatile uint32_t _dspPrepUs;
  volatile uint32_t _dspEnvelopeUs;
  volatile uint32_t _dspFetchXfadeUs;
  volatile uint32_t _dspVintageUs;
  volatile uint32_t _dspFilterUs;
  volatile uint32_t _dspGainMixUs;
  volatile uint32_t _dspPositionUs;
  volatile uint32_t _dspMixPosUs; // compatibility aggregate: gain/mix+position
  volatile uint32_t _dspEchoOutUs;
  volatile uint32_t _voiceHistCount[VOICE_COUNT + 1];
  volatile uint32_t _voiceHistSumUs[VOICE_COUNT + 1];
  volatile uint32_t _voiceHistMaxUs[VOICE_COUNT + 1];
  uint32_t _dspProfilerPrintMs;
  uint8_t _dspProfileDivider;

  int16_t *_previewBuffer;
  uint32_t _previewCapacityFrames;
  volatile uint32_t _previewFrames;
  volatile uint32_t _previewPos;

  int16_t *_echoBuffer;
  uint32_t _echoCapacityFrames;
  volatile uint16_t _echoDelayMs;
  volatile uint8_t _echoFeedback;
  volatile uint8_t _echoMix;
  uint32_t _echoWritePos;
  // C034: AudioTask-owned smoothed echo state. Control/UI only writes the
  // target parameters above, so parameter edits cannot tear the DSP path.
  uint32_t _echoDelayFramesCurrent;
  // C034a: Delay-time changes use two stable read taps and a short crossfade.
  // This avoids discontinuities caused by moving a single delay read head.
  uint32_t _echoDelayFramesXfadeTo;
  uint16_t _echoDelayXfadePos;
  bool _echoDelayXfadeActive;
  int32_t _echoFeedbackQ8Current;
  int32_t _echoMixQ8Current;
  volatile uint32_t _echoLimitCount;
  volatile uint32_t _filterGuardCount;
  volatile uint32_t _echoParamSlewCount;
  volatile uint32_t _msNoteMissCount;
  volatile uint32_t _msVelocityMissCount;
  volatile uint32_t _msRrFallbackCount;
  volatile uint32_t _msSanitizeCount;
  volatile bool _echoTailActive;
  uint16_t _echoTailQuietFrames;

  ProgressCallback _progressCallback;
  void *_progressContext;
  uint8_t _progressBase;
  uint8_t _progressSpan;
  void reportProgress(uint8_t percent);

  TaskHandle_t _audioTaskHandle;
};

/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include "../PhoenixGUI/PhoenixGUI.h"
#include "PhoenixInput.h"
#include "../PhoenixMidi/PhoenixMidiParameters.h"
#include "../PhoenixMidi/PhoenixMidiMonitor.h"

class PhoenixScreenManager {
public:
  PhoenixScreenManager();

  void begin(bool serviceMode = false);
  void update(const PhoenixInputState &in);
  void draw(PhoenixGUI &gui);

  bool sequencerRunning() const { return _seqRun; }
  void setSequencerRunning(bool running) { _seqRun = running; }
  bool sequencerExternalClock() const { return _seqExternalClock; }
  uint16_t sequencerBpm() const { return _seqExternalClock && _seqExternalClockPresent ? _seqExternalBpm : _seqBpm; }
  uint16_t sequencerInternalBpm() const { return _seqBpm; }
  void setSequencerExternalStatus(bool present, uint16_t bpm) { _seqExternalClockPresent = present; if (bpm >= 20 && bpm <= 300) _seqExternalBpm = bpm; }
  uint8_t sequencerPlayhead() const { return _seqPlayhead[_seqTrack & 3] % _seqLength[_seqPattern][_seqTrack & 3]; }
  uint8_t sequencerTrackPlayhead(uint8_t track) const { const uint8_t t = track & 3; return _seqPlayhead[t] % _seqLength[_seqPattern][t]; }
  void setSequencerTrackPlayhead(uint8_t track, uint8_t step) { const uint8_t t = track & 3; _seqPlayhead[t] = step % _seqLength[_seqPattern][t]; }
  void setSequencerPlayhead(uint8_t step) { for (uint8_t t = 0; t < 4; ++t) _seqPlayhead[t] = step % _seqLength[_seqPattern][t]; }
  uint8_t sequencerTrackLength(uint8_t track) const { return _seqLength[_seqPattern][track & 3]; }
  uint8_t sequencerPattern() const { return _seqPattern & 3U; }
  void setSequencerPattern(uint8_t pattern) {
    // v0.7.39e: Pattern/playhead changes must never move the edit cursor.
    // Keep _seqStep independent so a selected step remains editable while
    // transport, song mode and pattern loops restart from step 1.
    _seqPattern = pattern & 3U;
    const uint8_t tr = _seqTrack & 3U;
    const uint8_t len = _seqLength[_seqPattern][tr] ? _seqLength[_seqPattern][tr] : 1U;
    if (_seqStep >= len) _seqStep = (uint8_t)(len - 1U);
    setSequencerPlayhead(0);
  }
  uint8_t sequencerMaxTrackLength() const { uint8_t m=1; for(uint8_t t=0;t<4;++t){ const uint8_t l=_seqLength[_seqPattern][t]; if(l>m)m=l; } return m; }
  bool sequencerStepOn(uint8_t track, uint8_t step) const { return _seqOn[_seqPattern][track & 3][step & 15]; }
  uint8_t sequencerStepNote(uint8_t track, uint8_t step) const { return _seqNote[_seqPattern][track & 3][step & 15]; }
  uint8_t sequencerStepVelocity(uint8_t track, uint8_t step) const { return _seqVelocity[_seqPattern][track & 3][step & 15]; }
  uint8_t sequencerStepGate(uint8_t track, uint8_t step) const { return _seqGate[_seqPattern][track & 3][step & 15]; }
  bool consumeSequencerPreviewRequest(uint8_t &track, uint8_t &note, uint8_t &velocity);
  bool saveSequencerConfig(const char *bankDir) const;
  bool loadSequencerConfig(const char *bankDir);
  void clearSequencerPattern();

  // v0.7.39c Song Editor bridge. PhoenixOS owns the real SongManager;
  // PhoenixScreens mirrors its data for editing and display.
  void setSongEntryStatus(uint8_t index, uint8_t pattern, uint8_t repeats, bool end);
  void setSongLoopStatus(uint8_t mode, uint8_t start, uint8_t end);
  void setSongPlaybackStatus(bool playing, uint8_t position, uint8_t repeatIndex, uint8_t repeatTarget);
  bool consumeSongDataChanged();
  bool consumeSongPlayRequest();
  bool consumeSongStopRequest();
  bool consumeSongSaveRequest();
  uint8_t songPattern(uint8_t index) const { return _songPattern[index & 15U] & 3U; }
  uint8_t songRepeats(uint8_t index) const { return _songRepeats[index & 15U]; }
  bool songEnd(uint8_t index) const { return _songEnd[index & 15U]; }
  uint8_t songLoopMode() const { return _songLoopMode; }
  bool songLoopEnabled() const { return _songLoopMode != 0; }
  uint8_t songLoopStart() const { return _songLoopStart; }
  uint8_t songLoopEnd() const { return _songLoopEnd; }

  bool consumeRecordRequest();
  bool consumePlayRequest();
  bool consumePlayFromCursorRequest(uint32_t &frame);
  bool consumeReversePlayFromCursorRequest(uint32_t &frame);
  bool consumeReversePlayRequest();
  bool consumeStopRequest();
  int8_t consumeToneRequest();
  bool consumeAudioTestChanged();
  uint8_t audioTestWaveform() const { return _audioTestWaveform; }
  uint16_t audioTestFrequencyHz() const { return _audioTestFreqHz; }
  int8_t audioTestLevelDb() const { return _audioTestLevelDb; }
  bool audioTestOutput() const { return _audioTestOutput; }
  int8_t consumeSelectSlotRequest();
  bool consumePitchChanged();
  bool consumeEnvelopeChanged();
  bool consumeVintageChanged();
  bool consumeQuattroChanged();
  bool consumeVoiceChanged();
  bool consumeMixerChanged();
  bool consumeFilterChanged();
  bool consumeFilterEnvelopeChanged();
  bool consumeMultisampleChanged();
  bool consumeKeygroupImportRequest(char *path, size_t pathLen, uint8_t &slot, uint8_t &group, uint8_t &layer);
  bool consumeKeygroupDeleteRequest(uint8_t &slot, uint8_t &group);
  bool consumeMultisampleAutoMapRequest(char *folder, size_t folderLen, uint8_t &slot);
  bool consumeAutoMapRequest(char *folder, size_t folderLen, uint8_t &slot);
  int8_t pitchSemitone() const { return _pitchSemitone[_selectedSlot & 3]; }
  int16_t pitchFineCent() const { return _pitchFineCent[_selectedSlot & 3]; }
  uint8_t pitchRootNote() const { return _pitchRootNote[_selectedSlot & 3]; }
  int8_t keyboardOctave() const { return _keyboardOctave[_selectedSlot & 3]; }
  bool pitchTrackEnabled() const { return _pitchTrack[_selectedSlot & 3]; }
  uint8_t pitchBendRange() const { return _pitchBendRange[_selectedSlot & 3]; }
  uint8_t pitchSlot() const { return _selectedSlot & 3; }
  uint16_t envelopeAttackMs() const { return _envAttackMs[_selectedSlot & 3]; }
  uint16_t envelopeDecayMs() const { return _envDecayMs[_selectedSlot & 3]; }
  uint8_t envelopeSustainPct() const { return _envSustainPct[_selectedSlot & 3]; }
  uint16_t envelopeReleaseMs() const { return _envReleaseMs[_selectedSlot & 3]; }
  uint8_t vintagePreset() const { return _vintagePreset[_selectedSlot & 3]; }
  uint8_t vintageSampleRateIndex() const { return _vintageRate[_selectedSlot & 3]; }
  uint8_t vintageBitDepthIndex() const { return _vintageBits[_selectedSlot & 3]; }
  uint8_t vintageFilterMode() const { return _vintageFilter[_selectedSlot & 3]; }
  uint8_t vintageJitter() const { return _vintageJitter[_selectedSlot & 3]; }
  bool consumeEchoChanged();
  int8_t consumeDiskAction();
  // USB actions: 0=start read-only, 1=exit/remount, 2=start read/write.
  int8_t consumeUsbStorageAction();
  void setUsbStorageStatus(bool active, bool available, bool writable, bool ejected, const char *status);
  void finishUsbStorageExit(bool remountOk, bool mediaChanged);
  bool usbStorageVisible() const { return _screen == SCR_USB_STORAGE; }
  uint8_t selectedBank() const { return _diskBank; }
  void setBankDirty(bool dirty) { _bankDirty = dirty; }
  bool bankDirty() const { return _bankDirty; }

  // v0.7.40 session/shortcut bridge
  uint8_t sessionScreen() const { return (uint8_t)_screen; }
  uint8_t sessionSlot() const { return _selectedSlot & 3U; }
  uint8_t sequencerTrack() const { return _seqTrack & 3U; }
  void setSelectedBank(uint8_t bank) { _diskBank = constrain(bank, (uint8_t)1, (uint8_t)99); }
  void restoreSessionView(uint8_t screen, uint8_t slot);
  void selectSequencerTrack(uint8_t track);
  void setDiskBankUsed(bool used);
  bool consumeWavImportRequest(char *path, size_t pathLen, uint8_t &slot);
  bool consumeWavPreviewRequest(char *path, size_t pathLen);
  bool consumeWavPreviewStopRequest();
  void setDiskStatus(const char *text);
  void setBrowserStatus(const char *text);
  uint16_t echoDelayMs() const { return _echoDelayMs; }
  uint8_t echoFeedback() const { return _echoFeedback; }
  uint8_t echoMix() const { return _echoMix; }
  uint8_t echoSend(uint8_t slot) const { return _echoSend[slot & 3]; }
  uint8_t midiChannel() const { return _midiChannel; }
  uint8_t midiType() const { return _midiType; }

  // v0.7.41b direct MIDI Learn bridge
  bool consumeMidiLearnRequest(PhoenixParameterId &id);
  void setMidiLearnResult(uint8_t cc, bool assigned);
  void cancelMidiLearn();
  bool midiLearnActive() const { return _midiLearnActive; }
  bool consumeLoopChanged();
  bool consumeLoopRangeChanged();
  bool consumeSampleRangeChanged();
  bool consumeSampleMarkerEdit(uint8_t &slot, uint32_t &sampleStart,
                               uint32_t &loopStart, uint32_t &loopEnd,
                               uint32_t &sampleEnd);
  bool consumeZeroCrossSnapRequest(uint8_t &slot, uint8_t &marker, uint32_t &frame);
  int8_t consumeSampleProcessingAction(uint8_t &slot); // 0 trim, 1 DC, 2 normalize
  void setSampleProcessingStatus(const bool trim[4], const bool dc[4], const bool norm[4]);
  void applyZeroCrossSnap(uint8_t slot, uint8_t marker, uint32_t frame);
  bool loopEnabled() const { return _slotLoopMode[_selectedSlot & 3] != 0; }
  uint8_t loopMode() const { return _slotLoopMode[_selectedSlot & 3]; }
  uint8_t loopCrossfadeMs() const { return _slotLoopXfadeMs[_selectedSlot & 3]; }
  uint8_t loopSlot() const { return _selectedSlot & 3; }
  uint32_t loopStart() const { return _slotLoopStart[_selectedSlot & 3]; }
  uint32_t loopEnd() const { return _slotLoopEnd[_selectedSlot & 3]; }
  uint32_t sampleStart() const { return _slotSampleStart[_selectedSlot & 3]; }
  uint32_t sampleEnd() const { return _slotSampleEnd[_selectedSlot & 3]; }
  bool consumeSampleEditorPlayRequest(uint32_t &start, uint32_t &end, bool &reverse);
  bool consumeTriggerChanged();
  bool triggerAuto() const { return _triggerAuto; }
  uint8_t triggerLevel() const { return _triggerLevel; }
  bool triggerTestActive() const { return _triggerTestActive; }
  void setTriggerTestInput(bool thresholdReached);
  uint8_t smartProcessingMode() const { return _smartProcessingMode; } // 0=SAFE,1=SMART,2=FORCE
  bool midiEnabled() const { return _midiEnabled; }
  bool midiOmni() const { return _midiOmni; }
  bool midiClockEnabled() const { return _midiClockEnabled; }
  bool midiNoteTriggerEnabled() const { return _midiNoteEnabled; }
  bool midiCcControlEnabled() const { return _midiCcEnabled; }
  bool midiProgramChangeEnabled() const { return _midiPcEnabled; }
  bool playbackReverseMode() const { return _waveReverseMode; }
  uint8_t quattroMode() const { return _quattroMode; } // 0=KEYZONE, 1=MULTI
  uint8_t quattroKeyLow(uint8_t slot) const { return _quattroKeyLow[slot & 3]; }
  uint8_t quattroKeyHigh(uint8_t slot) const { return _quattroKeyHigh[slot & 3]; }
  uint8_t quattroMidiChannel(uint8_t slot) const { return _quattroMidiChannel[slot & 3]; }
  void setQuattroStatus(uint8_t mode, const uint8_t low[4], const uint8_t high[4], const uint8_t channel[4]);
  void setVoiceSlotStatus(uint8_t slot, uint8_t mode, uint8_t limit, uint8_t priority, uint16_t glideMs);
  void setMixerSlotStatus(uint8_t slot, uint8_t level, int8_t pan, uint8_t echoSend);
  void setFilterSlotStatus(uint8_t slot, uint8_t cutoff, uint8_t resonance, int8_t envAmount, uint8_t velocityAmount, uint8_t keytrack, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs);
  uint8_t voiceMode() const { return _voiceMode[_selectedSlot & 3]; }
  uint8_t voiceLimit() const { return _voiceLimit[_selectedSlot & 3]; }
  uint8_t notePriority() const { return _notePriority[_selectedSlot & 3]; }
  uint16_t glideMs() const { return _glideMs[_selectedSlot & 3]; }
  uint8_t mixerLevel() const { return _mixerLevel[_selectedSlot & 3]; }
  int8_t mixerPan() const { return _mixerPan[_selectedSlot & 3]; }
  uint8_t filterCutoff() const { return _filterCutoff[_selectedSlot&3]; }
  uint8_t filterResonance() const { return _filterResonance[_selectedSlot&3]; }
  int8_t filterEnvAmount() const { return _filterEnvAmount[_selectedSlot&3]; }
  uint8_t filterVelocityAmount() const { return _filterVelocity[_selectedSlot&3]; }
  uint8_t filterKeytrack() const { return _filterKeytrack[_selectedSlot&3]; }
  uint16_t filterAttackMs() const { return _filterAttackMs[_selectedSlot&3]; }
  uint16_t filterDecayMs() const { return _filterDecayMs[_selectedSlot&3]; }
  uint8_t filterSustainPct() const { return _filterSustainPct[_selectedSlot&3]; }
  uint16_t filterReleaseMs() const { return _filterReleaseMs[_selectedSlot&3]; }

  void setHardwareStatus(bool psramOk, uint32_t psramKb, bool audioOk, uint32_t midiBytes, uint32_t underruns, uint32_t clipCount);
  void setDiagnosticsStatus(uint32_t elapsedMs, uint8_t voices, uint8_t voiceCapacity, uint8_t voicePeak,
                            uint32_t audioAvgUs, uint32_t audioPeakUs, uint32_t riskCount,
                            uint32_t overrunCount, uint32_t underruns, uint32_t voiceSteals,
                            uint32_t heapFreeKb, uint32_t heapMinKb, uint32_t psramFreeKb);
  bool consumeDiagnosticsResetRequest();
  bool consumeDiagnosticsLogRequest();
  bool diagnosticsVisible() const;
  void setTransportStatus(bool recording, bool playing, bool sampleReady, uint32_t frames, uint32_t recordFrames, uint32_t recordCapacityFrames, uint32_t playFrames, uint32_t sampleRate, int16_t dcL, int16_t dcR, uint8_t selectedSlot);
  void setSamplePlayheadStatus(bool active, uint32_t frame);
  void setPlaybackReverse(bool reverse) { _playingReverse = reverse; }
  void setRecorderUxStatus(bool waitingForTrigger, bool activelyRecording);
  void showRecordingComplete(uint8_t slot);
  void setRecordingCompleteSaveResult(bool ok);
  void setSampleAnalysisSummary(uint8_t peakPercent, bool dcDetected,
                                uint32_t suggestedStart, uint32_t suggestedEnd,
                                bool trimApplied);
  void setSlotStatus(const bool recorded[4], const bool recording[4], const bool playing[4], const uint32_t frames[4]);
  void setLoopStatus(const bool loopEnabled[4]); // legacy compatibility
  void setLoopModeStatus(const uint8_t loopMode[4]);
  void setLoopCrossfadeStatus(const uint8_t xfadeMs[4]);
  void setLoopRangeStatus(const uint32_t loopStart[4], const uint32_t loopEnd[4]);
  void setSampleRangeStatus(const uint32_t sampleStart[4], const uint32_t sampleEnd[4]);
  void setEchoStatus(uint16_t delayMs, uint8_t feedback, uint8_t mix, const uint8_t send[4]);
  void setTriggerStatus(bool automatic, uint8_t level);
  void setPitchSlotStatus(uint8_t slot, int8_t semitone, int16_t fineCent, uint8_t rootNote, int8_t octaveOffset, bool pitchTrack, uint8_t bendRange = 2);
  void setEnvelopeSlotStatus(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs);
  void setVintageSlotStatus(uint8_t slot, uint8_t preset, uint8_t rate, uint8_t bits, uint8_t filter, uint8_t jitter);
  void setPlaybackReverseMode(bool reverse);
  void setMultisampleKeygroupStatus(uint8_t slot,uint8_t group,bool enabled,uint8_t low,uint8_t high,uint8_t root,uint8_t chokeGroup=0,bool oneShot=false,uint16_t chokeFadeMs=0,uint8_t playMode=0,uint8_t exclusiveGroup=0,bool retriggerLegato=false,uint8_t startPct=0,uint8_t endPct=100,bool reversePlayback=false,int8_t transpose=0,int16_t fineCent=0,uint8_t keytrackPct=100);
  void setMultisampleLayerStatus(uint8_t slot,uint8_t group,uint8_t layer,bool enabled,uint8_t velocityLow,uint8_t velocityHigh,uint8_t level,int8_t pan,const char *path,uint8_t rrMode=0,uint8_t rrCount=1,uint8_t loopMode=0,uint8_t loopStartPct=20,uint8_t loopEndPct=80,uint8_t loopXfadeMs=0,uint8_t velToLevelPct=100,uint8_t velToFilterPct=0);
  void setMultisampleVariantPath(uint8_t slot,uint8_t group,uint8_t layer,uint8_t variant,const char *path);
  uint8_t multisampleSlot() const { return _msSlot & 3; }
  uint8_t multisampleGroup() const { return _msGroup & 15; }
  uint8_t multisampleLayer() const { return _msLayer % 3; }
  bool multisampleEnabled() const { return _msEnabled[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleLow() const { return _msLow[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleHigh() const { return _msHigh[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleRoot() const { return _msRoot[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleChokeGroup() const { return _msChoke[_msSlot & 3][_msGroup & 15]; }
  bool multisampleOneShot() const { return _msOneShot[_msSlot & 3][_msGroup & 15]; }
  uint16_t multisampleChokeFadeMs() const { return _msChokeFadeMs[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisamplePlayMode() const { return _msPlayMode[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleExclusiveGroup() const { return _msExclusiveGroup[_msSlot & 3][_msGroup & 15]; }
  bool multisampleRetriggerLegato() const { return _msRetriggerLegato[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleStartPct() const { return _msStartPct[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleEndPct() const { return _msEndPct[_msSlot & 3][_msGroup & 15]; }
  bool multisampleReverse() const { return _msReverse[_msSlot & 3][_msGroup & 15]; }
  int8_t multisampleTranspose() const { return _msTranspose[_msSlot & 3][_msGroup & 15]; }
  int16_t multisampleFineCent() const { return _msFineCent[_msSlot & 3][_msGroup & 15]; }
  uint8_t multisampleKeytrackPct() const { return _msKeytrackPct[_msSlot & 3][_msGroup & 15]; }
  bool multisampleLayerEnabled() const { return _msLayerEnabled[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleVelocityLow() const { return _msVelLow[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleVelocityHigh() const { return _msVelHigh[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleLevel() const { return _msLevel[_msSlot&3][_msGroup&15][_msLayer%3]; }
  int8_t multisamplePan() const { return _msPan[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleRoundRobinMode() const { return _msRrMode[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleLoopMode() const { return _msLoopMode[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleLoopStartPct() const { return _msLoopStartPct[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleLoopEndPct() const { return _msLoopEndPct[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleLoopXfadeMs() const { return _msLoopXfadeMs[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleVelocityToLevelPct() const { return _msVelToLevel[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleVelocityToFilterPct() const { return _msVelToFilter[_msSlot&3][_msGroup&15][_msLayer%3]; }
  uint8_t multisampleRoundRobinVariant() const { return _msRrVariant & 3; }

private:
  bool allocateMultisamplePathCache();
  void loadSystemSettings();
  void saveSystemSettings();
  void saveMidiControlSettings();
  void saveMidiChannelSetting();
  void saveTriggerSettings();

  bool currentMidiLearnParameter(PhoenixParameterId &id) const;
  void drawMidiLearnPopup(PhoenixGUI &gui);
  void drawEnvelope(PhoenixGUI &gui);
  void drawVoiceAllocation(PhoenixGUI &gui);
  void drawQuattroMixer(PhoenixGUI &gui);
  void drawQuattroFilter(PhoenixGUI &gui);
  void drawFilterEnvelope(PhoenixGUI &gui);
  void drawMultisampleEditor(PhoenixGUI &gui);
  void drawSequencer(PhoenixGUI &gui);
  void drawPatternManager(PhoenixGUI &gui);
  void drawSongEditor(PhoenixGUI &gui);
  bool loadSequencerFile(const char *path);
  enum ScreenID : uint8_t {
    SCR_BOOT = 0,
    SCR_SELFTEST,
    SCR_AUDIO_OUT,
    SCR_AUDIO_IN,
    SCR_BUTTONS,
    SCR_MAIN,
    SCR_SOUND,
    SCR_WAVE,
    SCR_SAMPLE_EDITOR,
    SCR_PITCH,
    SCR_ENVELOPE,
    SCR_ECHO,
    SCR_LOOP,
    SCR_TRIGGER,
    SCR_DISK,
    SCR_SAMPLE_BROWSER,
    SCR_QUATTRO,
    SCR_KEYBOARD,
    SCR_VOICE,
    SCR_MIXER,
    SCR_FILTER,
    SCR_FILTER_ENV,
    SCR_MULTISAMPLE,
    SCR_VINTAGE,
    SCR_MIDI_TYPE,
    SCR_MIDI_CH,
    SCR_MIDI_MONITOR,
    SCR_SEQUENCER,
    SCR_PATTERN_MANAGER,
    SCR_SONG_EDITOR,
    SCR_RECORD_COMPLETE,
    SCR_DIAGNOSTICS,
    SCR_USB_STORAGE
  };

  ScreenID _screen;
  ScreenID _diagnosticsReturnScreen;
  uint8_t _sel;
  uint32_t _bootStart;
  bool _about;
  bool _overwriteConfirm;
  uint8_t _overwriteSlot;
  ScreenID _recordReturnScreen;
  uint8_t _recordCompleteSlot;
  int8_t _recordCompleteSaveState; // -1 idle, 0 failed, 1 saved
  bool _serviceMode;
  int8_t _toneRequest;
  uint8_t _audioTestParam;
  bool _audioTestEditMode;
  uint8_t _audioTestWaveform;
  uint16_t _audioTestFreqHz;
  int8_t _audioTestLevelDb;
  bool _audioTestOutput;
  bool _audioTestChanged;
  bool _recordRequest;
  bool _playRequest;
  bool _reversePlayRequest;
  bool _playFromCursorRequest;
  bool _reversePlayFromCursorRequest;
  uint32_t _playFromCursorFrame;
  bool _waveReverseMode;
  bool _stopRequest;
  int8_t _selectSlotRequest;
  int8_t _pitchSemitone[4];
  int16_t _pitchFineCent[4];
  uint8_t _pitchRootNote[4];
  int8_t _keyboardOctave[4];
  bool _pitchTrack[4];
  uint8_t _pitchBendRange[4];
  uint8_t _pitchParam;
  bool _pitchEditMode;
  bool _pitchChanged;
  uint16_t _envAttackMs[4];
  uint16_t _envDecayMs[4];
  uint8_t _envSustainPct[4];
  uint16_t _envReleaseMs[4];
  uint8_t _envParam;
  bool _envEditMode;
  bool _envChanged;
  uint8_t _echoParam;
  uint16_t _echoDelayMs;
  uint8_t _echoFeedback;
  uint8_t _echoMix;
  uint8_t _echoSend[4];
  bool _echoChanged;
  bool _echoEditMode;
  uint8_t _vintagePreset[4];
  uint8_t _vintageRate[4];
  uint8_t _vintageBits[4];
  uint8_t _vintageFilter[4];
  uint8_t _vintageJitter[4];
  uint8_t _vintageParam;
  bool _vintageEditMode;
  bool _vintageChanged;
  uint8_t _quattroMode;
  uint8_t _quattroKeyLow[4];
  uint8_t _quattroKeyHigh[4];
  uint8_t _quattroMidiChannel[4];
  uint8_t _quattroParam;
  bool _quattroEditMode;
  bool _quattroChanged;
  uint8_t _voiceMode[4];
  uint8_t _voiceLimit[4];
  uint8_t _notePriority[4];
  uint16_t _glideMs[4];
  uint8_t _voiceParam;
  bool _voiceEditMode;
  bool _voiceChanged;
  uint8_t _mixerLevel[4];
  int8_t _mixerPan[4];
  uint8_t _mixerParam;
  bool _mixerEditMode;
  bool _mixerChanged;
  uint8_t _filterCutoff[4]; uint8_t _filterResonance[4]; int8_t _filterEnvAmount[4]; uint8_t _filterVelocity[4]; uint8_t _filterKeytrack[4];
  uint16_t _filterAttackMs[4]; uint16_t _filterDecayMs[4]; uint8_t _filterSustainPct[4]; uint16_t _filterReleaseMs[4];
  uint8_t _filterParam; bool _filterEditMode; bool _filterChanged;
  uint8_t _filterEnvParam; bool _filterEnvEditMode; bool _filterEnvChanged;

  // v0.7.24b Multisample Keygroup Editor state
  uint8_t _msSlot;
  uint8_t _msGroup;
  uint8_t _msLayer;
  uint8_t _msParam;
  bool _msEditMode;
  bool _msChanged;
  bool _msEnabled[4][16];
  uint8_t _msLow[4][16];
  uint8_t _msHigh[4][16];
  uint8_t _msRoot[4][16];
  uint8_t _msChoke[4][16];
  bool _msOneShot[4][16];
  uint16_t _msChokeFadeMs[4][16];
  uint8_t _msPlayMode[4][16];
  uint8_t _msExclusiveGroup[4][16];
  bool _msRetriggerLegato[4][16];
  uint8_t _msStartPct[4][16];
  uint8_t _msEndPct[4][16];
  bool _msReverse[4][16];
  int8_t _msTranspose[4][16];
  int16_t _msFineCent[4][16];
  uint8_t _msKeytrackPct[4][16];
  bool _msLayerEnabled[4][16][3];
  uint8_t _msVelLow[4][16][3];
  uint8_t _msVelHigh[4][16][3];
  uint8_t _msLevel[4][16][3];
  int8_t _msPan[4][16][3];
  // C014: the 120 kB multisample path cache is UI/storage metadata and
  // therefore allocated in PSRAM during begin().
  char (*_msPath)[16][3][4][160];
  uint8_t _msRrMode[4][16][3];
  uint8_t _msLoopMode[4][16][3];
  uint8_t _msLoopStartPct[4][16][3];
  uint8_t _msLoopEndPct[4][16][3];
  uint8_t _msLoopXfadeMs[4][16][3];
  uint8_t _msVelToLevel[4][16][3];
  uint8_t _msVelToFilter[4][16][3];
  uint8_t _msRrCount[4][16][3];
  uint8_t _msRrVariant;

  bool _keygroupImportMode;
  bool _keygroupImportRequest;
  bool _keygroupDeleteRequest;
  bool _multisampleAutoMapMode;
  bool _multisampleAutoMapRequest;
  uint8_t _multisampleAutoMapSlot;
  char _multisampleAutoMapFolder[160];
  bool _autoMapBrowserMode;
  bool _autoMapRequest;
  uint8_t _autoMapSlot;
  char _autoMapFolder[160];
  uint8_t _keygroupTargetSlot;
  uint8_t _keygroupTargetGroup;
  uint8_t _keygroupTargetLayer;
  uint8_t _keygroupTargetVariant;
  char _keygroupImportPath[160];
  uint8_t _midiType;
  uint8_t _midiChannel;
  bool _midiEnabled;
  bool _midiOmni;
  bool _midiClockEnabled;
  bool _midiNoteEnabled;
  bool _midiCcEnabled;
  bool _midiPcEnabled;
  bool _midiEditMode;
  bool _loopChanged;
  bool _loopRangeChanged;
  bool _sampleRangeChanged;
  bool _sampleMarkerEditPending;
  uint8_t _sampleMarkerEditSlot;
  uint32_t _sampleMarkerEditSampleStart;
  uint32_t _sampleMarkerEditLoopStart;
  uint32_t _sampleMarkerEditLoopEnd;
  uint32_t _sampleMarkerEditSampleEnd;
  bool _zeroCrossSnapPending;
  int8_t _sampleProcessingAction;
  uint8_t _sampleProcessingSlot;
  uint8_t _zeroCrossSnapSlot;
  uint8_t _zeroCrossSnapMarker;
  uint32_t _zeroCrossSnapFrame;
  bool _slotLoop[4];
  uint8_t _slotLoopMode[4]; // 0 OFF, 1 FORWARD, 2 ALTERNATE
  uint8_t _slotLoopXfadeMs[4]; // OFF, 2, 4, 8, 16, 32 ms
  uint8_t _loopParam; // 0 MODE, 1 XFADE
  uint32_t _slotLoopStart[4];
  uint32_t _slotLoopEnd[4];
  uint32_t _slotSampleStart[4];
  uint32_t _slotSampleEnd[4];
  bool _slotTrimEnabled[4];
  bool _slotDcEnabled[4];
  bool _slotNormalizeEnabled[4];
  uint32_t _editorCursor;
  uint8_t _editorMarker; // 0 S.START, 1 L.START, 2 L.END, 3 S.END
  bool _sampleEditorPlayRequest;
  uint32_t _sampleEditorPlayStart;
  uint32_t _sampleEditorPlayEnd;
  bool _sampleEditorPlayReverse;
  bool _triggerChanged;
  bool _triggerAuto;
  uint8_t _triggerLevel;
  uint8_t _smartProcessingMode;
  uint8_t _triggerParam;
  bool _triggerEditMode;
  // C021: safe trigger test is UI-only. It observes the live threshold state
  // but never enters AUDIO_ARMED/AUDIO_RECORD and never touches a sample slot.
  bool _triggerTestActive;
  bool _triggerTestAbove;
  bool _triggerTestPrevAbove;
  uint32_t _triggerTestTrigUntilMs;
  uint8_t _recUxStatus;
  uint32_t _recUxUntilMs;
  bool _recUxPrevWaiting;
  bool _recUxPrevActive;
  uint8_t _analysisPeakPercent;
  bool _analysisDcDetected;
  uint32_t _analysisSuggestedStart;
  uint32_t _analysisSuggestedEnd;
  bool _analysisTrimApplied;
  int8_t _diskAction;
  int8_t _usbStorageAction;
  bool _usbStorageActive;
  bool _usbStorageAvailable;
  bool _usbStorageWritable;
  bool _usbStorageEjected;
  bool _usbStorageReadWriteSelected;
  bool _usbStorageExitConfirm;
  char _usbStorageStatus[24];
  uint8_t _diskBank;
  bool _diskBankUsed;
  bool _bankDirty;
  int8_t _diskConfirmAction;
  char _diskStatus[24];

  static const uint8_t PHX_BROWSER_MAX = 64;
  struct BrowserEntry {
    char name[64];
    bool isDir;
    uint32_t size;
    bool wavInfoValid;
    uint32_t sampleRate;
    uint16_t bitsPerSample;
    uint16_t channels;
    uint32_t dataOffset;
    uint32_t dataLength;
    uint32_t sampleCount;
    uint16_t durationTenths;
  };
  BrowserEntry _browserEntries[PHX_BROWSER_MAX];
  uint8_t _browserCount;
  uint8_t _browserSel;
  char _browserStatus[24];
  char _browserPath[160];
  uint8_t _browserImportState; // 0 normal, 1 target slot, 2 overwrite confirm
  uint8_t _browserImportSlot;
  char _browserImportPath[160];
  bool _browserImportRequest;
  bool _browserPreviewRequest;
  bool _browserPreviewStopRequest;
  char _browserPreviewPath[160];

  // v0.7.34 Step Sequencer Phase 1
  bool _seqRun;
  bool _seqEditMode;
  uint8_t _seqTrack;
  uint8_t _seqStep;
  uint8_t _seqParam;
  uint8_t _seqPlayhead[4];
  uint8_t _seqPattern;
  uint8_t _patternAction;
  uint8_t _patternTarget;
  uint8_t _seqLength[4][4];
  uint16_t _seqBpm;
  bool _seqExternalClock;
  bool _seqExternalClockPresent;
  uint16_t _seqExternalBpm;
  bool _seqOn[4][4][16];
  uint8_t _seqNote[4][4][16];
  uint8_t _seqVelocity[4][4][16];
  uint8_t _seqGate[4][4][16];
  bool _seqPreviewRequest;
  uint8_t _seqPreviewTrack;
  uint8_t _seqPreviewNote;
  uint8_t _seqPreviewVelocity;

  // v0.7.39c Song Editor mirror and requests
  uint8_t _songPattern[16];
  uint8_t _songRepeats[16];
  bool _songEnd[16];
  uint8_t _songEditPos;
  uint8_t _songEditField; // 0 PAT/END, 1 REP, 2 ACTION, 3 LOOP START, 4 LOOP END
  uint8_t _songAction;    // 0 INSERT, 1 DELETE
  uint8_t _songLoopMode; // 0 OFF, 1 SONG range, 2 PATTERN infinite
  uint8_t _songLoopStart;
  uint8_t _songLoopEnd;
  bool _songDataChanged;
  bool _songPlayRequest;
  bool _songStopRequest;
  bool _songSaveRequest;
  bool _songPlaying;
  uint8_t _songPlayPos;
  uint8_t _songPlayRepeat;
  uint8_t _songPlayRepeatTarget;

  bool _diagnosticsResetRequest;
  bool _diagnosticsLogRequest;
  uint32_t _diagnosticsStatusUntilMs;
  uint8_t _diagnosticsLastAction; // 1 RESET, 2 LOG
  uint32_t _diagElapsedMs;
  uint8_t _diagVoices;
  uint8_t _diagVoiceCapacity;
  uint8_t _diagVoicePeak;
  uint32_t _diagAudioAvgUs;
  uint32_t _diagAudioPeakUs;
  uint32_t _diagRiskCount;
  uint32_t _diagOverrunCount;
  uint32_t _diagUnderruns;
  uint32_t _diagVoiceSteals;
  uint32_t _diagHeapFreeKb;
  uint32_t _diagHeapMinKb;
  uint32_t _diagPsramFreeKb;

  bool _psramOk;
  bool _audioOk;
  uint32_t _psramKb;
  uint8_t _seqPendingClick;
  uint32_t _seqPendingClickMs;
  uint32_t _seqQuickTrackUntilMs;
  uint32_t _midiBytes;
  uint32_t _underruns;
  uint32_t _clipCount;
  bool _btnSeen[8];
  bool _encSeen;
  bool _encSwSeen;


  bool _midiLearnActive;
  bool _midiLearnRequest;
  PhoenixParameterId _midiLearnParam;
  uint8_t _midiLearnResultCc;
  bool _midiLearnResultAssigned;
  uint32_t _midiLearnResultUntilMs;
  bool _midiLearnMenu;
  uint8_t _midiLearnMenuSel;
  bool _midiResetConfirm;
  bool _recording;
  uint8_t _selectedSlot;
  bool _slotRecorded[4];
  bool _slotRecording[4];
  bool _slotPlaying[4];
  uint32_t _slotFrames[4];
  bool _playing;
  bool _playingReverse;
  bool _sampleReady;
  uint32_t _sampleFrames;
  uint32_t _recordFrames;
  uint32_t _recordCapacityFrames;
  uint32_t _playFrames;
  bool _samplePlayheadActive;
  uint32_t _samplePlayheadFrame;
  uint32_t _waveCursorFrame;
  uint8_t _waveZoom;
  uint8_t _editorZoom;
  uint32_t _sampleRate;
  int16_t _dcL;
  int16_t _dcR;

  int itemCountFor(ScreenID s) const;
  void moveSel(int8_t d);
  void enterSelection();
  void requestRecordingForSelectedSlot();
  void go(ScreenID s, uint8_t sel = 0);
  void drawSoundSampling(PhoenixGUI &gui);
  void drawQuattro(PhoenixGUI &gui);
  void drawQuattroKeyboard(PhoenixGUI &gui);
  void drawPitchConverter(PhoenixGUI &gui);
  void drawEcho(PhoenixGUI &gui);
  void drawLoop(PhoenixGUI &gui);
  void drawSampleEditor(PhoenixGUI &gui);
  void drawTriggerLevel(PhoenixGUI &gui);
  void drawVintageSampler(PhoenixGUI &gui);
  void drawMidiType(PhoenixGUI &gui);
  void drawMidiChannel(PhoenixGUI &gui);
  void drawMidiMonitor(PhoenixGUI &gui);
  void drawDiskUtilities(PhoenixGUI &gui);
  void drawSampleBrowser(PhoenixGUI &gui);
  void scanSampleBrowser();
  void browserGoRoot();
  bool browserGoUp();
  bool browserOpenSelected();
  void moveBrowserSel(int8_t d);
  bool readWavInfo(const char *fullPath, BrowserEntry &entry);
  void drawOverwritePopup(PhoenixGUI &gui);
  void drawRecordingComplete(PhoenixGUI &gui);
};

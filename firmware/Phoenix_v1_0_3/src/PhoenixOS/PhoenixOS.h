/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "PhoenixEventBus.h"
#include "PhoenixInputManager.h"
#include "../PhoenixGUI/PhoenixGUI.h"
#include "../PhoenixApp/PhoenixScreens.h"
#include "../PhoenixAudio/PhoenixAudioManager.h"
#include "../PhoenixSong/PhoenixSongManager.h"
#include "../PhoenixMidi/PhoenixMidiParameters.h"
#include "../PhoenixSampling/PhoenixSampleAnalysis.h"
#include "../PhoenixSmart/PhoenixSmartReport.h"
#include "../PhoenixSystem/PhoenixUSBStorage.h"
class PhoenixOS {
public:
  PhoenixOS(PhoenixGUI &gui, PhoenixScreenManager &screens, PhoenixInputManager &input, PhoenixAudioManager &audio);

  void begin(bool serviceMode = false);
  void update();
  void setMidiCounterSource(volatile uint32_t *counter) { _midiCounter = counter; }
  void notifyMidiClock();
  void notifyMidiStart();
  void notifyMidiContinue();
  void notifyMidiStop();
  void notifyMidiControlChange(uint8_t slot, uint8_t cc, uint8_t value);
  // C029: MIDI task posts CC events; PhoenixOS consumes them in the control task.
  bool queueMidiControlChange(uint8_t slot, uint8_t cc, uint8_t value);
  uint32_t midiControlDrops() const { return _midiControlDrops; }

private:
  PhoenixGUI &_gui;
  PhoenixScreenManager &_screens;
  PhoenixInputManager &_input;
  PhoenixAudioManager &_audio;
  PhoenixEventBus _events;
  PhoenixUSBStorage _usbStorage;
  PhoenixInputState _inputState;
  uint32_t _lastDrawMs;
  uint32_t _perfWindowStartMs;
  uint32_t _perfLoopAccumUs;
  uint32_t _perfLoopCount;
  uint32_t _perfLoopMaxUs;
  uint32_t _perfDrawAccumUs;
  uint32_t _perfDrawCount;
  uint32_t _perfDrawMaxUs;
  uint32_t _diagStartMs;
  uint32_t _diagLoopAvgUs;
  uint32_t _diagLoopPeakUs;
  uint32_t _diagDrawAvgUs;
  uint32_t _diagDrawPeakUs;
  uint32_t _diagGuiFps10;
  uint32_t _diagLastPublishMs;
  volatile uint32_t *_midiCounter;
  uint8_t _waveBins[96];
  uint32_t _seqLastStepMs;
  // C036c: timestamp ring preserves every F8 arrival time.
  static constexpr uint8_t kSeqClockQueueSize = 128;
  volatile uint32_t _seqClockUs[kSeqClockQueueSize];
  volatile uint8_t _seqClockWrite = 0;
  volatile uint8_t _seqClockRead = 0;
  bool _seqWasRunning;
  bool _seqPreviewHeld;
  uint8_t _seqPreviewTrack;
  uint8_t _seqPreviewNote;
  uint32_t _seqPreviewOffMs;
  portMUX_TYPE _seqMidiMux = portMUX_INITIALIZER_UNLOCKED;
  volatile uint8_t _seqMidiTransportPending; // 0 none, 1 START, 2 CONTINUE, 3 STOP
  volatile uint32_t _seqMidiLastClockUs;
  volatile uint32_t _seqMidiClockPeriodUs;
  uint8_t _seqExtTickPhase;
  uint32_t _seqExtLastSeenMs;
  uint8_t _seqLastPattern;
  uint32_t _seqDroppedClockTicks; // active-run backlog only; idle F8 is intentionally ignored
  uint32_t _seqIdleClockTicksIgnored; // C034b: F8 received while transport is stopped/not active
  uint32_t _seqTransportStartCount;
  uint32_t _seqTransportContinueCount;
  uint32_t _seqTransportStopCount;
  uint32_t _seqPatternTransitionCount;
  uint32_t _seqGateRecycleCount;
  uint32_t _seqForcedGateReleaseCount;
  uint32_t _seqClockQueueDrops;
  uint32_t _seqClockBatchMax;
  uint32_t _seqStepLateMaxUs;
  uint32_t _seqStepEventCount;
  uint32_t _seqLastStepClockUs;
  // C036d dedicated realtime sequencer state. Control only publishes snapshots;
  // the P8 task owns external-clock phase/playheads and never calls GUI code.
  TaskHandle_t _seqRtTaskHandle = nullptr;
  volatile bool _seqRtExternalEnabled = false;
  volatile bool _seqRtRunning = false;
  volatile uint8_t _seqRtPlayhead[4] = {0,0,0,0};
  volatile uint8_t _seqRtTrackLength[4] = {16,16,16,16};
  volatile uint8_t _seqRtStepOn[4][16];
  volatile uint8_t _seqRtStepNote[4][16];
  volatile uint8_t _seqRtStepVelocity[4][16];
  volatile uint8_t _seqRtStepGate[4][16];
  volatile uint32_t _seqRtConfigGeneration = 0;
  volatile uint32_t _seqRtCompletedSteps = 0;
  volatile uint32_t _seqRtLateMaxUs = 0;
  volatile uint64_t _seqRtLateAccumUs = 0;
  volatile uint32_t _seqRtLateCount = 0;
  volatile uint32_t _seqRtBatchMax = 0;
  volatile uint32_t _seqRtPredictionErrorMaxUs = 0;
  volatile uint32_t _seqRtEventId = 1;
  uint32_t _seqRtLastUiCompletedSteps = 0;
  // C036: cross-subsystem integration guards around bank I/O.
  uint32_t _integrationBankQuiesceCount;
  uint32_t _integrationBankLoadFailCount;
  uint32_t _integrationBankSaveFailCount;
  uint32_t _integrationStaleClockTicksCleared;
  uint32_t _integrationActiveVoiceGuardCount;
  PhoenixSongManager _songManager;
  uint8_t _songPatternStepCounter;
  char _progressLabel[20];
  uint8_t _lastProgressPercent;
  uint32_t _lastProgressDrawMs;
  uint32_t _lastSessionSaveMs;
  uint8_t _sessionBankCache;
  uint8_t _sessionScreenCache;
  uint8_t _sessionSlotCache;
  bool _midiEchoSync;
  uint8_t _midiEchoTimeValue;
  uint8_t _midiModulationValue;
  bool _midiNormalizeArmed[4];
  volatile bool _midiLearnActive;
  volatile uint8_t _midiLearnParameter;
  volatile int16_t _midiLearnCcPending;
  bool _sampleAnalysisPending;
  uint8_t _sampleAnalysisSlot;
  uint32_t _sampleProcessingRecalcDue[4];
  bool _recordActivePrev;
  uint8_t _recordActiveSlot;

  struct MidiControlEvent { uint8_t slot; uint8_t cc; uint8_t value; };
  static constexpr uint8_t kMidiControlQueueSize = 64;
  MidiControlEvent _midiControlQueue[kMidiControlQueueSize];
  volatile uint8_t _midiControlWrite = 0;
  volatile uint8_t _midiControlRead = 0;
  volatile uint32_t _midiControlDrops = 0;
  void processMidiControlQueue();

  void quiesceForBankIo(const char *reason);
  bool loadBankForSession(uint8_t bank);
  void restoreLastSession();
  void saveSessionIfChanged();
  void stopSequencerVoices();
  void pauseSongTransport();
  void setSequencerPatternRobust(uint8_t pattern, bool releaseOldGates);
  void triggerSequencerStep(uint8_t step, uint32_t stepMs);
  void processSequencerMidiTransport(); // internal/control compatibility; external clock is C036d RT task
  void refreshRealtimeSequencerSnapshot();
  void scheduleRealtimeSequencerStep(uint32_t targetUs, uint32_t stepUs);
  void realtimeSequencerTask();
  static void realtimeSequencerTaskThunk(void *arg);
  void startSongTestTransport();
  void stopSongTransport();
  void advanceSongPatternCycle();
  void syncSongToScreens();
  void syncScreensToSong();

  static void progressCallback(uint8_t percent, void *context);
  void showProgress(uint8_t percent);
  void drawScreensTimed();
  void updatePerformanceAudit(uint32_t loopStartUs);
  void resetDiagnostics();
  void printDiagnosticsSnapshot(const char *reason);
  void publishDiagnostics();
  void analyzeRecordedSlot(uint8_t slot);
  void updateSequencer();
  void routeEvents();
};

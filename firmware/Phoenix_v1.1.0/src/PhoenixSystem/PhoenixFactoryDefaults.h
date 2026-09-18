#pragma once
#include <Arduino.h>

// Project Phoenix v1.1.0 DEV1
// Safe code-defined fallback for DISK UTILITIES -> NEW BANK.
// /PHOENIX/INIT_SOUND.CFG may override these values, but it never contains
// sample/WAV paths. Device-global configuration remains separate.
namespace PhoenixFactoryDefaults {

static constexpr uint16_t ECHO_DELAY_MS = 250;
static constexpr uint8_t  ECHO_FEEDBACK = 35;
static constexpr uint8_t  ECHO_MIX = 20;
static constexpr uint8_t  REVERB_SIZE = 55;
static constexpr uint8_t  REVERB_DECAY = 55;
static constexpr uint8_t  REVERB_DAMP = 45;
static constexpr uint8_t  REVERB_MIX = 20;
static constexpr uint16_t SEQUENCER_BPM = 120;
static constexpr uint8_t  SEQUENCER_TRACK_LENGTH = 16;
static constexpr uint8_t  SEQUENCER_STEP_NOTE[4] = {60,62,64,66};
static constexpr uint8_t  SEQUENCER_STEP_VELOCITY = 100;
static constexpr uint8_t  SEQUENCER_STEP_GATE = 50;

struct SlotDefaults {
  bool multisampleEnabled;
  uint8_t multisampleLow, multisampleHigh, multisampleRoot;
  int8_t coarse; int16_t fineCent; uint8_t rootNote; int8_t octave;
  bool pitchTracking; uint8_t pitchBendRange;
  uint16_t attackMs, decayMs; uint8_t sustainPct; uint16_t releaseMs;
  uint8_t level; int8_t pan; uint8_t sampleGainPct, velocityAmountPct;
  uint8_t filterCutoff, filterResonance; int8_t filterEnvAmount;
  uint8_t filterVelocityAmount, filterKeytrack;
  uint16_t filterAttackMs, filterDecayMs; uint8_t filterSustainPct; uint16_t filterReleaseMs;
  uint8_t vintagePreset, vintageSampleRate, vintageBitDepth, vintageFilter, vintageJitter;
  uint8_t voiceMode, voiceLimit, notePriority; uint16_t glideMs;
  uint8_t echoSend;
  uint8_t reverbSend;
  uint8_t quattroLow, quattroHigh, quattroMidiChannel;
};

// Neutral/open sound. MIDI channels are S1=1 ... S4=4. The prepared Quattro
// ranges are full-range so Librarian can use the same template for MULTI or
// KEYZONE work without creating sample assignments.
static constexpr SlotDefaults SLOT[4] = {
  {false,0,127,60, 0,0,60,0,true,2, 5,80,100,80, 100,0,100,100, 100,0,0,0,0, 0,250,0,300, 0,0,0,0,0, 0,12,0,0, 0, 0, 0,127,1},
  {false,0,127,60, 0,0,60,0,true,2, 5,80,100,80, 100,0,100,100, 100,0,0,0,0, 0,250,0,300, 0,0,0,0,0, 0,12,0,0, 0, 0, 0,127,2},
  {false,0,127,60, 0,0,60,0,true,2, 5,80,100,80, 100,0,100,100, 100,0,0,0,0, 0,250,0,300, 0,0,0,0,0, 0,12,0,0, 0, 0, 0,127,3},
  {false,0,127,60, 0,0,60,0,true,2, 5,80,100,80, 100,0,100,100, 100,0,0,0,0, 0,250,0,300, 0,0,0,0,0, 0,12,0,0, 0, 0, 0,127,4}
};
static constexpr uint8_t QUATTRO_MODE = 1;
}

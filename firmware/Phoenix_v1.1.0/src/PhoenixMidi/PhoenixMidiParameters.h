#pragma once
#include <Arduino.h>

enum PhoenixParameterId : uint8_t {
  PHX_PAR_SLOT_VOLUME = 0,
  PHX_PAR_SLOT_PAN,
  PHX_PAR_SAMPLE_START,
  PHX_PAR_SAMPLE_END,
  PHX_PAR_LOOP_START,
  PHX_PAR_LOOP_END,
  PHX_PAR_ROOT_KEY,
  PHX_PAR_TRANSPOSE,
  PHX_PAR_FINE_TUNE,
  PHX_PAR_REVERSE,
  PHX_PAR_NORMALIZE,
  PHX_PAR_SAMPLE_GAIN,
  PHX_PAR_AMP_ATTACK,
  PHX_PAR_AMP_DECAY,
  PHX_PAR_AMP_SUSTAIN,
  PHX_PAR_AMP_RELEASE,
  PHX_PAR_VELOCITY_AMOUNT,
  PHX_PAR_FILTER_CUTOFF,
  PHX_PAR_FILTER_RESONANCE,
  PHX_PAR_ECHO_SEND,
  PHX_PAR_ECHO_TIME,
  PHX_PAR_ECHO_FEEDBACK,
  PHX_PAR_ECHO_MIX,
  PHX_PAR_ECHO_SYNC,
  PHX_PAR_GLIDE,
  PHX_PAR_MODULATION,
  PHX_PARAMETER_COUNT
};

enum PhoenixMidiParameterType : uint8_t {
  PHX_MIDI_CONTINUOUS = 0,
  PHX_MIDI_TOGGLE,
  PHX_MIDI_ACTION,
  PHX_MIDI_RESERVED
};

struct PhoenixMidiParameterDef {
  PhoenixParameterId id;
  uint8_t defaultCc;
  const char *name;
  PhoenixMidiParameterType type;
  bool learnable;
};

void phxMidiMapBegin();
void phxMidiMapResetDefaults();
bool phxMidiMapResetParameter(PhoenixParameterId id);
bool phxMidiMapAssign(PhoenixParameterId id, uint8_t cc);
bool phxMidiMapClear(PhoenixParameterId id);
uint8_t phxMidiMapCc(PhoenixParameterId id);
bool phxMidiMapIsLearned(PhoenixParameterId id);
const PhoenixMidiParameterDef *phxMidiParameterForCc(uint8_t cc, uint8_t startIndex = 0);
const PhoenixMidiParameterDef *phxMidiParameterById(PhoenixParameterId id);

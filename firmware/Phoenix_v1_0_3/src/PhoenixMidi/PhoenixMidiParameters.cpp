/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "PhoenixMidiParameters.h"
#include <Preferences.h>

static const PhoenixMidiParameterDef kPhoenixMidiParameters[] = {
  { PHX_PAR_SLOT_VOLUME,       28,  "SLOT VOLUME",    PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_SLOT_PAN,          52,  "SLOT PAN",       PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_SAMPLE_START,      26,  "SAMPLE START",   PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_SAMPLE_END,        27,  "SAMPLE END",     PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_LOOP_START,        22,  "LOOP START",     PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_LOOP_END,          23,  "LOOP END",       PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ROOT_KEY,          30,  "ROOT KEY",       PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_TRANSPOSE,         31,  "TRANSPOSE",      PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_FINE_TUNE,        109,  "FINE TUNE",      PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_REVERSE,          110,  "REVERSE",        PHX_MIDI_TOGGLE, true },
  { PHX_PAR_NORMALIZE,        111,  "NORMALIZE",      PHX_MIDI_ACTION, false },
  { PHX_PAR_SAMPLE_GAIN,      112,  "SAMPLE GAIN",    PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_AMP_ATTACK,       118,  "AMP ATTACK",     PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_AMP_DECAY,        119,  "AMP DECAY",      PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_AMP_SUSTAIN,       75,  "AMP SUSTAIN",    PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_AMP_RELEASE,       76,  "AMP RELEASE",    PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_VELOCITY_AMOUNT,  115,  "VELOCITY AMT",   PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_FILTER_CUTOFF,    102,  "FILTER CUTOFF",  PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_FILTER_RESONANCE, 103,  "FILTER RESO",    PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ECHO_SEND,         86,  "ECHO SEND",      PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ECHO_TIME,         89,  "ECHO TIME",      PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ECHO_FEEDBACK,     90,  "ECHO FEEDBACK",  PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ECHO_MIX,          77,  "ECHO MIX",       PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_ECHO_SYNC,         78,  "ECHO SYNC",      PHX_MIDI_TOGGLE, true },
  { PHX_PAR_GLIDE,            106,  "GLIDE",          PHX_MIDI_CONTINUOUS, true },
  { PHX_PAR_MODULATION,         1,  "MODULATION",     PHX_MIDI_RESERVED, false }
};

static uint8_t gPhoenixMidiCc[PHX_PARAMETER_COUNT];
static bool gPhoenixMidiLearned[PHX_PARAMETER_COUNT];
static bool gPhoenixMidiMapReady = false;

static void phxMidiKey(char *dst, size_t len, uint8_t id) { snprintf(dst, len, "p%02u", (unsigned)id); }

void phxMidiMapBegin() {
  Preferences prefs; prefs.begin("phx-midi", true);
  for (uint8_t i=0; i<PHX_PARAMETER_COUNT; ++i) {
    char key[8]; phxMidiKey(key,sizeof(key),i);
    const uint8_t stored=prefs.getUChar(key, kPhoenixMidiParameters[i].defaultCc);
    gPhoenixMidiCc[i]=stored;
    gPhoenixMidiLearned[i]=(stored != kPhoenixMidiParameters[i].defaultCc);
  }
  prefs.end(); gPhoenixMidiMapReady=true;
}

void phxMidiMapResetDefaults() {
  Preferences prefs; prefs.begin("phx-midi", false);
  for (uint8_t i=0; i<PHX_PARAMETER_COUNT; ++i) {
    char key[8]; phxMidiKey(key,sizeof(key),i);
    gPhoenixMidiCc[i]=kPhoenixMidiParameters[i].defaultCc;
    gPhoenixMidiLearned[i]=false; prefs.putUChar(key,gPhoenixMidiCc[i]);
  }
  prefs.end(); gPhoenixMidiMapReady=true;
}

bool phxMidiMapResetParameter(PhoenixParameterId id) {
  if ((uint8_t)id >= PHX_PARAMETER_COUNT) return false;
  if (!gPhoenixMidiMapReady) phxMidiMapBegin();
  const PhoenixMidiParameterDef *def=phxMidiParameterById(id);
  if (!def) return false;
  gPhoenixMidiCc[(uint8_t)id]=def->defaultCc;
  gPhoenixMidiLearned[(uint8_t)id]=false;
  Preferences prefs; prefs.begin("phx-midi",false); char key[8];
  phxMidiKey(key,sizeof(key),(uint8_t)id); prefs.putUChar(key,def->defaultCc); prefs.end();
  return true;
}

bool phxMidiMapAssign(PhoenixParameterId id, uint8_t cc) {
  if ((uint8_t)id >= PHX_PARAMETER_COUNT || cc > 127) return false;
  const PhoenixMidiParameterDef *def=phxMidiParameterById(id);
  if (!def || !def->learnable) return false;
  if (!gPhoenixMidiMapReady) phxMidiMapBegin();
  gPhoenixMidiCc[(uint8_t)id]=cc; gPhoenixMidiLearned[(uint8_t)id]=(cc!=def->defaultCc);
  Preferences prefs; prefs.begin("phx-midi",false); char key[8]; phxMidiKey(key,sizeof(key),(uint8_t)id); prefs.putUChar(key,cc); prefs.end();
  return true;
}

bool phxMidiMapClear(PhoenixParameterId id) {
  if ((uint8_t)id >= PHX_PARAMETER_COUNT) return false;
  if (!gPhoenixMidiMapReady) phxMidiMapBegin();
  gPhoenixMidiCc[(uint8_t)id]=255; gPhoenixMidiLearned[(uint8_t)id]=true;
  Preferences prefs; prefs.begin("phx-midi",false); char key[8]; phxMidiKey(key,sizeof(key),(uint8_t)id); prefs.putUChar(key,255); prefs.end();
  return true;
}

uint8_t phxMidiMapCc(PhoenixParameterId id) { if(!gPhoenixMidiMapReady) phxMidiMapBegin(); return ((uint8_t)id<PHX_PARAMETER_COUNT)?gPhoenixMidiCc[(uint8_t)id]:255; }
bool phxMidiMapIsLearned(PhoenixParameterId id) { if(!gPhoenixMidiMapReady) phxMidiMapBegin(); return ((uint8_t)id<PHX_PARAMETER_COUNT)?gPhoenixMidiLearned[(uint8_t)id]:false; }

const PhoenixMidiParameterDef *phxMidiParameterForCc(uint8_t cc, uint8_t startIndex) {
  if (!gPhoenixMidiMapReady) phxMidiMapBegin();
  for (uint8_t i=startIndex; i<PHX_PARAMETER_COUNT; ++i) if (gPhoenixMidiCc[i]==cc) return &kPhoenixMidiParameters[i];
  return nullptr;
}

const PhoenixMidiParameterDef *phxMidiParameterById(PhoenixParameterId id) {
  for (uint8_t i=0; i<PHX_PARAMETER_COUNT; ++i) if (kPhoenixMidiParameters[i].id==id) return &kPhoenixMidiParameters[i];
  return nullptr;
}

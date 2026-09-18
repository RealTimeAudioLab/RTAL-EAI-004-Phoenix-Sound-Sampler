#include "PhoenixMidiMonitor.h"
#include <Preferences.h>

static portMUX_TYPE gMidiMonitorMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t gType = PHX_MMON_NONE;
static volatile uint8_t gChannel = 0;
static volatile uint8_t gData1 = 0;
static volatile int16_t gValue = 0;
static volatile uint32_t gCounter = 0;
static volatile uint32_t gTimestampMs = 0;
static volatile uint32_t gActivityTimestampMs = 0;

static volatile uint32_t gClockCounter = 0;
static volatile uint32_t gLastClockUs = 0;
static volatile uint32_t gLastClockMs = 0;
static volatile uint32_t gClockPeriodUs = 0;
static bool gClockVisible = false;
static bool gSettingsLoaded = false;

static void loadSettingsOnce() {
  if (gSettingsLoaded) return;
  Preferences prefs;
  prefs.begin("phx-monitor", true);
  gClockVisible = prefs.getBool("showClock", false);
  prefs.end();
  gSettingsLoaded = true;
}

static bool createsActivity(PhoenixMidiMonitorType type) {
  // Continuous MIDI Clock is deliberately excluded. Transport commands remain
  // visible as genuine user/musical activity.
  return type != PHX_MMON_NONE && type != PHX_MMON_CLOCK;
}

void phxMidiMonitorRecord(PhoenixMidiMonitorType type, uint8_t channel, uint8_t data1, int16_t value) {
  loadSettingsOnce();
  const uint32_t nowMs = millis();

  if (type == PHX_MMON_CLOCK) {
    const uint32_t nowUs = micros();
    portENTER_CRITICAL(&gMidiMonitorMux);
    if (gLastClockUs != 0) {
      const uint32_t delta = nowUs - gLastClockUs;
      // Reject impossible outliers, then apply a light IIR average.
      if (delta >= 5000U && delta <= 200000U) {
        if (gClockPeriodUs == 0) gClockPeriodUs = delta;
        else gClockPeriodUs = (gClockPeriodUs * 7U + delta) / 8U;
      }
    }
    gLastClockUs = nowUs;
    gLastClockMs = nowMs;
    ++gClockCounter;
    const bool show = gClockVisible;
    portEXIT_CRITICAL(&gMidiMonitorMux);

    if (!show) return;
  }

  portENTER_CRITICAL(&gMidiMonitorMux);
  gType = (uint8_t)type;
  gChannel = channel;
  gData1 = data1;
  gValue = value;
  ++gCounter;
  gTimestampMs = nowMs;
  if (createsActivity(type)) gActivityTimestampMs = nowMs;
  portEXIT_CRITICAL(&gMidiMonitorMux);
}

PhoenixMidiMonitorSnapshot phxMidiMonitorSnapshot() {
  loadSettingsOnce();
  PhoenixMidiMonitorSnapshot s;
  const uint32_t nowMs = millis();
  uint32_t periodUs;
  uint32_t lastClockMs;
  portENTER_CRITICAL(&gMidiMonitorMux);
  s.type = (PhoenixMidiMonitorType)gType;
  s.channel = gChannel;
  s.data1 = gData1;
  s.value = gValue;
  s.counter = gCounter;
  s.timestampMs = gTimestampMs;
  s.clockCounter = gClockCounter;
  periodUs = gClockPeriodUs;
  lastClockMs = gLastClockMs;
  s.clockVisible = gClockVisible;
  portEXIT_CRITICAL(&gMidiMonitorMux);

  s.clockAgeMs = lastClockMs ? (uint32_t)(nowMs - lastClockMs) : 0xFFFFFFFFUL;
  s.clockPresent = lastClockMs != 0 && s.clockAgeMs < 500U;
  if (s.clockPresent && periodUs != 0) {
    // MIDI clock is 24 pulses per quarter note.
    const uint32_t bpm10 = 600000000UL / (periodUs * 24UL);
    s.clockBpm10 = (uint16_t)((bpm10 > 9999UL) ? 9999UL : bpm10);
  } else {
    s.clockBpm10 = 0;
  }
  return s;
}

bool phxMidiActivityActive(uint32_t nowMs) {
  uint32_t stamp;
  portENTER_CRITICAL(&gMidiMonitorMux);
  stamp = gActivityTimestampMs;
  portEXIT_CRITICAL(&gMidiMonitorMux);
  return stamp != 0 && (uint32_t)(nowMs - stamp) < 140U;
}

bool phxMidiMonitorClockVisible() {
  loadSettingsOnce();
  portENTER_CRITICAL(&gMidiMonitorMux);
  const bool visible = gClockVisible;
  portEXIT_CRITICAL(&gMidiMonitorMux);
  return visible;
}

void phxMidiMonitorSetClockVisible(bool visible) {
  loadSettingsOnce();
  portENTER_CRITICAL(&gMidiMonitorMux);
  gClockVisible = visible;
  portEXIT_CRITICAL(&gMidiMonitorMux);
  Preferences prefs;
  prefs.begin("phx-monitor", false);
  prefs.putBool("showClock", visible);
  prefs.end();
}

void phxMidiMonitorToggleClockVisible() {
  phxMidiMonitorSetClockVisible(!phxMidiMonitorClockVisible());
}

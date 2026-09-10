#pragma once
#include <stdint.h>

struct PhoenixLoopMarkerEditResult {
  uint32_t start;
  uint32_t end;      // exclusive
  uint32_t cursor;   // visible frame, Loop End uses end-1
  bool changed;
};

inline PhoenixLoopMarkerEditResult phxMoveLoopMarker(uint32_t frames,
                                                      uint32_t currentStart,
                                                      uint32_t currentEnd,
                                                      bool editStart,
                                                      uint32_t cursor,
                                                      int32_t move) {
  PhoenixLoopMarkerEditResult r = { currentStart, currentEnd, cursor, false };
  if (frames == 0U || move == 0) return r;

  uint32_t start = currentStart;
  uint32_t end = currentEnd;
  if (end == 0U || end > frames) end = frames;
  if (start >= frames) start = 0U;
  if (end <= start + 1U) {
    start = 0U;
    end = (frames > 1U) ? frames : 1U;
  }

  if (cursor >= frames) cursor = frames - 1U;
  uint32_t next = cursor;
  if (move > 0) {
    const uint32_t add = (uint32_t)move;
    next = (add >= frames || next >= frames - add) ? (frames - 1U) : (next + add);
  } else {
    const uint32_t sub = (uint32_t)(-move);
    next = (next > sub) ? (next - sub) : 0U;
  }

  if (editStart) {
    const uint32_t maxStart = (end > 1U) ? (end - 2U) : 0U;
    if (next > maxStart) next = maxStart;
    start = next;
  } else {
    const uint32_t minEndCursor = (start + 1U < frames) ? (start + 1U) : (frames - 1U);
    if (next < minEndCursor) next = minEndCursor;
    end = next + 1U;
    if (end > frames) end = frames;
  }

  r.start = start;
  r.end = end;
  r.cursor = next;
  r.changed = (start != currentStart || end != currentEnd);
  return r;
}


struct PhoenixSampleMarkerEditResult {
  uint32_t sampleStart; // inclusive
  uint32_t loopStart;   // inclusive
  uint32_t loopEnd;     // exclusive
  uint32_t sampleEnd;   // exclusive
  uint32_t cursor;      // visible frame; end markers use end-1
  bool sampleChanged;
  bool loopChanged;
};

inline PhoenixSampleMarkerEditResult phxMoveSampleMarker(uint32_t frames,
                                                          uint32_t currentSampleStart,
                                                          uint32_t currentLoopStart,
                                                          uint32_t currentLoopEnd,
                                                          uint32_t currentSampleEnd,
                                                          uint8_t marker,
                                                          uint32_t cursor,
                                                          int32_t move) {
  PhoenixSampleMarkerEditResult r = {
    currentSampleStart, currentLoopStart, currentLoopEnd, currentSampleEnd,
    cursor, false, false
  };
  if (frames == 0U || move == 0) return r;
  if (frames < 3U) {
    r.sampleStart = 0U;
    r.loopStart = 0U;
    r.loopEnd = frames;
    r.sampleEnd = frames;
    r.cursor = 0U;
    r.sampleChanged = (currentSampleStart != 0U || currentSampleEnd != frames);
    r.loopChanged = (currentLoopStart != 0U || currentLoopEnd != frames);
    return r;
  }

  // C012: normalize to the four-marker invariant without discarding a valid
  // user range. End markers are exclusive; therefore the sustain loop keeps
  // a minimum length of two sample frames.
  uint32_t ss = currentSampleStart;
  uint32_t se = currentSampleEnd;
  if (se == 0U || se > frames) se = frames;
  if (ss > frames - 2U) ss = frames - 2U;

  uint32_t ls = currentLoopStart;
  uint32_t le = currentLoopEnd;
  if (ls < ss) ls = ss;
  if (ls > frames - 2U) ls = frames - 2U;
  if (le == 0U || le > frames) le = frames;
  if (le < ls + 2U) le = ls + 2U;
  if (se < le) se = le;
  if (se > frames) se = frames;

  // If clamping at the physical sample end compressed the chain, rebuild only
  // the minimum required spacing from right to left.
  if (le > se) le = se;
  if (le < 2U) le = 2U;
  if (ls + 2U > le) ls = le - 2U;
  if (ss > ls) ss = ls;

  if (cursor >= frames) cursor = frames - 1U;
  uint32_t next = cursor;
  if (move > 0) {
    const uint32_t add = (uint32_t)move;
    next = (add >= frames || next >= frames - add) ? (frames - 1U) : (next + add);
  } else {
    const uint32_t sub = (uint32_t)(-move);
    next = (next > sub) ? (next - sub) : 0U;
  }

  // Cascading edit rule: the selected marker is always authoritative. When it
  // crosses a neighbour, that neighbour is pushed in the same direction. A
  // later movement in the opposite direction does not pull the other markers
  // back, so distances can be opened again naturally.
  switch (marker & 3U) {
    case 0: { // Sample Start (inclusive), pushes right
      if (next > frames - 2U) next = frames - 2U;
      ss = next;
      if (ls < ss) ls = ss;
      if (le < ls + 2U) le = ls + 2U;
      if (se < le) se = le;
      break;
    }

    case 1: { // Loop Start (inclusive), pushes left or right
      if (next > frames - 2U) next = frames - 2U;
      ls = next;
      if (ss > ls) ss = ls;
      if (le < ls + 2U) le = ls + 2U;
      if (se < le) se = le;
      break;
    }

    case 2: { // Loop End (exclusive); cursor denotes Loop End - 1
      if (next < 1U) next = 1U;
      le = next + 1U;
      if (se < le) se = le;
      if (ls + 2U > le) ls = le - 2U;
      if (ss > ls) ss = ls;
      break;
    }

    default: { // Sample End (exclusive); cursor denotes Sample End - 1
      if (next < 1U) next = 1U;
      se = next + 1U;
      if (le > se) le = se;
      if (ls + 2U > le) ls = le - 2U;
      if (ss > ls) ss = ls;
      break;
    }
  }

  // Final physical clamps. The cascade above guarantees the ordering; these
  // guards protect against arithmetic changes and malformed legacy values.
  if (se > frames) se = frames;
  if (le > se) le = se;
  if (le < 2U) le = 2U;
  if (ls + 2U > le) ls = le - 2U;
  if (ss > ls) ss = ls;

  if ((marker & 3U) == 0U) next = ss;
  else if ((marker & 3U) == 1U) next = ls;
  else if ((marker & 3U) == 2U) next = le - 1U;
  else next = se - 1U;

  r.sampleStart = ss;
  r.loopStart = ls;
  r.loopEnd = le;
  r.sampleEnd = se;
  r.cursor = next;
  r.sampleChanged = (ss != currentSampleStart || se != currentSampleEnd);
  r.loopChanged = (ls != currentLoopStart || le != currentLoopEnd);
  return r;
}

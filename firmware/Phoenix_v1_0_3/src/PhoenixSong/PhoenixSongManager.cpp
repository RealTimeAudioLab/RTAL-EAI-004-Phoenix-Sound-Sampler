#include "PhoenixSongManager.h"
#include <SD.h>

PhoenixSongManager::PhoenixSongManager()
: _playing(false), _position(0), _repeatIndex(0), _queued(false), _queuedPattern(0),
  _loopMode(LOOP_SONG), _loopStart(0), _loopEnd(0) {
  beginDefaultSong();
}

void PhoenixSongManager::sanitizeEntry(SongEntry &entry) {
  entry.pattern &= 3U;
  if (entry.repeats < 1U) entry.repeats = 1U;
  if (entry.repeats > 16U) entry.repeats = 16U;
}

void PhoenixSongManager::sanitizeLoop() {
  if (_loopStart >= kMaxEntries) _loopStart = 0;
  if (_loopEnd >= kMaxEntries) _loopEnd = kMaxEntries - 1;
  if (_loopEnd < _loopStart) _loopEnd = _loopStart;
}

void PhoenixSongManager::beginDefaultSong() {
  for (uint8_t i = 0; i < kMaxEntries; ++i) {
    _entries[i].pattern = 0;
    _entries[i].repeats = 1;
    _entries[i].end = true;
    _entries[i].reserved = 0;
  }
  _entries[0] = {0, 1, false, 0};
  _entries[1] = {0, 1, true, 0};
  _loopMode = LOOP_SONG;
  _loopStart = 0;
  _loopEnd = 0;
  stop();
}

void PhoenixSongManager::beginTestSong() {
  beginDefaultSong();
  _entries[0] = {0, 2, false, 0};
  _entries[1] = {1, 2, false, 0};
  _entries[2] = {2, 1, false, 0};
  _entries[3] = {0, 1, true, 0};
}

void PhoenixSongManager::setEntry(uint8_t index, uint8_t pattern, uint8_t repeats, bool end) {
  if (index >= kMaxEntries) return;
  _entries[index].pattern = pattern & 3U;
  _entries[index].repeats = (uint8_t)constrain((int)repeats, 1, 16);
  _entries[index].end = end;
  _entries[index].reserved = 0;
}

void PhoenixSongManager::insertEntry(uint8_t index) {
  if (index >= kMaxEntries) return;
  for (int i = kMaxEntries - 1; i > index; --i) _entries[i] = _entries[i - 1];
  _entries[index] = {0, 1, false, 0};
}

void PhoenixSongManager::deleteEntry(uint8_t index) {
  if (index >= kMaxEntries) return;
  for (uint8_t i = index; i + 1U < kMaxEntries; ++i) _entries[i] = _entries[i + 1U];
  _entries[kMaxEntries - 1] = {0, 1, true, 0};
  if (!_entries[0].end) return;
  _entries[0] = {0, 1, false, 0};
  _entries[1] = {0, 1, true, 0};
}

void PhoenixSongManager::setLoopMode(uint8_t mode, uint8_t start, uint8_t end) {
  _loopMode = mode > LOOP_PATTERN ? LOOP_OFF : mode;
  _loopStart = start;
  _loopEnd = end;
  sanitizeLoop();
}

void PhoenixSongManager::start() {
  _position = 0;
  _repeatIndex = 0;
  _queued = false;
  _queuedPattern = 0;
  _playing = !_entries[0].end;
}

void PhoenixSongManager::stop() {
  _playing = false;
  _position = 0;
  _repeatIndex = 0;
  _queued = false;
  _queuedPattern = 0;
}

void PhoenixSongManager::pause() {
  // C033: MIDI Stop pauses transport without rewinding song/repeat state.
  _playing = false;
}

void PhoenixSongManager::continuePlay() {
  if (!_playing && !_entries[_position].end) _playing = true;
}

uint8_t PhoenixSongManager::currentPattern() const {
  if (_position >= kMaxEntries || _entries[_position].end) return 0;
  return _entries[_position].pattern & 3U;
}

uint8_t PhoenixSongManager::currentRepeatTarget() const {
  if (_position >= kMaxEntries || _entries[_position].end) return 1;
  const uint8_t r = _entries[_position].repeats;
  return r < 1U ? 1U : (r > 16U ? 16U : r);
}

void PhoenixSongManager::queuePattern(uint8_t pattern) {
  _queued = true;
  _queuedPattern = pattern & 3U;
}

void PhoenixSongManager::clearQueue() { _queued = false; }

bool PhoenixSongManager::onPatternFinished(uint8_t &nextPattern) {
  if (!_playing) return false;

  if (_queued) {
    nextPattern = _queuedPattern & 3U;
    _queued = false;
    _repeatIndex = 0;
    return true;
  }

  SongEntry &entryRef = _entries[_position];
  sanitizeEntry(entryRef);

  // v0.7.39d: PATTERN loop holds the current song position forever.
  // It is evaluated only at the musical pattern boundary, so enabling or
  // disabling it never causes a mid-pattern jump.
  if (_loopMode == LOOP_PATTERN) {
    nextPattern = entryRef.pattern & 3U;
    return true;
  }

  ++_repeatIndex;

  if (_repeatIndex < entryRef.repeats) {
    nextPattern = entryRef.pattern & 3U;
    return true;
  }

  _repeatIndex = 0;
  uint8_t nextPos = (uint8_t)(_position + 1U);
  if (_loopMode == LOOP_SONG && _position >= _loopEnd) nextPos = _loopStart;

  if (nextPos >= kMaxEntries) {
    stop();
    return false;
  }

  _position = nextPos;
  SongEntry &next = _entries[_position];
  sanitizeEntry(next);
  if (next.end) {
    stop();
    return false;
  }

  nextPattern = next.pattern & 3U;
  return true;
}

bool PhoenixSongManager::saveConfig(const char *bankDir) const {
  if (!bankDir || !*bankDir) return false;
  char path[192], tmp[192], bak[192];
  snprintf(path, sizeof(path), "%s/SONG.CFG", bankDir);
  snprintf(tmp, sizeof(tmp), "%s/SONG.TMP", bankDir);
  snprintf(bak, sizeof(bak), "%s/SONG.BAK", bankDir);
  SD.remove(tmp);
  delay(1); // C036b: keep bank metadata writes cooperative on Core0.
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  f.println("VERSION=2");
  f.print("LOOP_MODE="); f.println((unsigned)_loopMode);
  f.print("LOOP_START="); f.println((unsigned)(_loopStart + 1U));
  f.print("LOOP_END="); f.println((unsigned)(_loopEnd + 1U));
  for (uint8_t i = 0; i < kMaxEntries; ++i) {
    f.print("POS"); if (i < 9) f.print('0'); f.print((unsigned)(i + 1U)); f.print('=');
    if (_entries[i].end) f.println("END");
    else { f.print('P'); f.print((unsigned)((_entries[i].pattern & 3U) + 1U)); f.print(','); f.println((unsigned)constrain((int)_entries[i].repeats, 1, 16)); }
    if ((i & 3U) == 3U) delay(1);
  }
  f.flush(); delay(1); f.close();
  SD.remove(bak); delay(1);
  if (SD.exists(path)) { if (!SD.rename(path, bak)) return false; delay(1); }
  if (!SD.rename(tmp, path)) { if (SD.exists(bak)) SD.rename(bak, path); return false; }
  delay(1);
  return true;
}

bool PhoenixSongManager::loadConfig(const char *bankDir) {
  if (!bankDir || !*bankDir) return false;
  char path[192], bak[192];
  snprintf(path, sizeof(path), "%s/SONG.CFG", bankDir);
  snprintf(bak, sizeof(bak), "%s/SONG.BAK", bankDir);
  File f = SD.open(path, FILE_READ);
  if (!f) f = SD.open(bak, FILE_READ);
  if (!f) { beginDefaultSong(); return true; }

  beginDefaultSong();
  bool valid = false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (!line.length() || line[0] == '#' || line[0] == ';') continue;
    if (line == "VERSION=1" || line == "VERSION=2") { valid = true; continue; }
    if (line.startsWith("LOOP_MODE=")) { _loopMode = (uint8_t)constrain(line.substring(10).toInt(), 0, 2); continue; }
    // Backward compatibility: v1 LOOP=1 means SONG range loop.
    if (line.startsWith("LOOP=")) { _loopMode = line.substring(5).toInt() != 0 ? LOOP_SONG : LOOP_OFF; continue; }
    if (line.startsWith("LOOP_START=")) { _loopStart = (uint8_t)constrain(line.substring(11).toInt() - 1, 0, 15); continue; }
    if (line.startsWith("LOOP_END=")) { _loopEnd = (uint8_t)constrain(line.substring(9).toInt() - 1, 0, 15); continue; }
    if (line.startsWith("POS")) {
      const int eq = line.indexOf('='); if (eq < 0) continue;
      const int n = line.substring(3, eq).toInt(); if (n < 1 || n > 16) continue;
      const uint8_t idx = (uint8_t)(n - 1);
      String data = line.substring(eq + 1); data.trim(); data.toUpperCase();
      if (data == "END") { setEntry(idx, 0, 1, true); continue; }
      const int comma = data.indexOf(','); if (comma < 0 || data[0] != 'P') continue;
      const int p = data.substring(1, comma).toInt();
      const int r = data.substring(comma + 1).toInt();
      setEntry(idx, (uint8_t)constrain(p - 1, 0, 3), (uint8_t)constrain(r, 1, 16), false);
    }
  }
  f.close();
  sanitizeLoop();
  stop();
  return valid;
}

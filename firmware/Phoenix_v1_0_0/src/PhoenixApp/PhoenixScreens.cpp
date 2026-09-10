#include "PhoenixScreens.h"
#include "PhoenixLoopEditorMath.h"
#include <SD.h>
#include <SPI.h>

static uint8_t phxDefaultMultiChannel(uint8_t baseChannel, uint8_t slot) {
  if (baseChannel < 1 || baseChannel > 16) baseChannel = 1;
  return (uint8_t)(((baseChannel - 1U + (slot & 3U)) % 16U) + 1U);
}

// v0.6.8 encoder helpers --------------------------------------------------
static int16_t encSign(int16_t v) {
  return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

static uint8_t encAbs16(int16_t v) {
  return (uint8_t)((v < 0) ? -v : v);
}

static int16_t semitoneStepFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  return sg * (encAbs16(v) >= 5 ? 2 : 1);
}

static int16_t fineStepFromEncoder(int16_t v) {
  // Directly use encoder acceleration for cent values: 1/2/5/10 cents.
  return v;
}

static int16_t rootStepFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  const uint8_t a = encAbs16(v);
  if (a >= 10) return sg * 12; // quick octave jumps
  if (a >= 5)  return sg * 5;
  if (a >= 2)  return sg * 2;
  return sg;
}

static int16_t delayStepMsFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  const uint8_t a = encAbs16(v);
  if (a >= 10) return sg * 100;
  if (a >= 5)  return sg * 50;
  if (a >= 2)  return sg * 20;
  return sg * 10;
}

static int16_t percentStepFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  const uint8_t a = encAbs16(v);
  if (a >= 10) return sg * 10;
  if (a >= 5)  return sg * 5;
  if (a >= 2)  return sg * 2;
  return sg;
}


static int16_t triggerLevelStepFromEncoder(int16_t v) {
  return encSign(v);
}

static inline uint32_t phxNonZero(uint32_t v) { return v == 0 ? 1UL : v; }

static void phxNoteName(uint8_t note, char *out, size_t n) {
  static const char * const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int octave = (int)(note / 12) - 2;  // MIDI 60 = C3 for this project
  snprintf(out, n, "%s%d", names[note % 12], octave);
}

static const char* phxAudioWaveName(uint8_t w) {
  switch (w) {
    case 0: return "SINE";
    case 1: return "TRIANGLE";
    case 2: return "SAW";
    case 3: return "SQUARE";
    case 4: return "WHITE NOISE";
    default: return "SINE";
  }
}

static int16_t audioFreqStepFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  const uint8_t a = encAbs16(v);
  if (a >= 10) return sg * 1000;
  if (a >= 5)  return sg * 500;
  if (a >= 2)  return sg * 100;
  return sg * 10;
}

static int16_t audioLevelStepFromEncoder(int16_t v) {
  const int16_t sg = encSign(v);
  if (!sg) return 0;
  const uint8_t a = encAbs16(v);
  if (a >= 10) return sg * 10;
  if (a >= 5)  return sg * 5;
  if (a >= 2)  return sg * 2;
  return sg;
}


static const char* const SELF_ITEMS[] = {
  "AUDIO OUTPUT TEST",
  "AUDIO INPUT TEST",
  "BUTTON TEST",
  "SYSTEM DIAGNOSTICS",
  "START SAMPLER"
};

static const char* const AUDIO_OUT_ITEMS[] = {
  "EXIT",
  "440HZ SINE",
  "440HZ SAW",
  "440HZ SQUARE",
  "WHITE NOISE",
  "AUDIO THROUGH"
};

static const char* const MAIN_ITEMS[] = {
  "SOUND SAMPLING",
  "QUATTRO SAMPLING",
  "PITCH CONVERTER",
  "ECHO",
  "DISK UTILITIES",
  "VINTAGE SAMPLER",
  "STEP SEQUENCER",
  "MIDI CONTROL"
};

static const char* const MIDI_CONTROL_ITEMS[] = {
  "MIDI INPUT",
  "MIDI CLOCK",
  "NOTE TRIGGER",
  "CC CONTROL",
  "PROGRAM CHG",
  "MIDI CHANNEL",
  "MIDI MONITOR",
  "RESET DEFAULTS"
};

static const char* const SOUND_ITEMS[] = {
  "RECORD",
  "REPLAY FORWARD",
  "REPLAY BACKWARD",
  "DRAW WAVEFORM",
  "SAMPLE EDITOR",
  "TRIGGER LEVEL",
  "LOOP",
  "EXIT"
};

static const char* const DISK_ITEMS[] = {
  "LOAD BANK",
  "SAVE BANK",
  "DELETE BANK",
  "WAV IMPORT",
  "USB MASS STORAGE"
};



static const char *PHX_ROOT_DIR = "/PHOENIX";
static const char *PHX_BANKS_DIR = "/PHOENIX/BANKS";
static const char *PHX_WAV_DIR = "/PHOENIX/WAV";
static const char *PHX_EXPORT_DIR = "/PHOENIX/EXPORT";
static const char *PHX_CONFIG_PATH = "/PHOENIX/CONFIG.TXT";

static bool phxIsWavName(const char *name) {
  if (!name) return false;
  const char *dot = strrchr(name, '.');
  return dot && !strcasecmp(dot, ".WAV");
}

static const char* phxBaseName(const char *path) {
  if (!path) return "";
  const char *a = strrchr(path, '/');
  return a ? a + 1 : path;
}

static bool phxIsHiddenOrSystemName(const char *name) {
  if (!name || !name[0]) return true;
  if (name[0] == '.') return true;
  if (!strcasecmp(name, "Thumbs.db")) return true;
  return false;
}

static bool phxBrowserShowName(const char *name, bool isDir) {
  if (phxIsHiddenOrSystemName(name)) return false;
  if (isDir) return true;
  return phxIsWavName(name);
}


static uint16_t phxReadLE16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t phxReadLE32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void phxFormatDurationTenths(uint16_t tenths, char *out, size_t n) {
  if (!out || n == 0) return;
  uint16_t sec = tenths / 10;
  uint16_t t = tenths % 10;
  snprintf(out, n, "%u.%us", (unsigned)sec, (unsigned)t);
}

static void phxJoinPath(const char *base, const char *name, char *out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = 0;
  if (!base || !base[0]) base = PHX_WAV_DIR;
  snprintf(out, outLen, "%s%s%s", base, (base[strlen(base)-1] == '/') ? "" : "/", name ? name : "");
}

static bool phxIsWavRoot(const char *path) {
  return path && !strcmp(path, PHX_WAV_DIR);
}

static bool phxParentPath(char *path, size_t n) {
  if (!path || !path[0] || phxIsWavRoot(path)) return false;
  char *slash = strrchr(path, '/');
  if (!slash) return false;
  if (slash <= path + strlen(PHX_WAV_DIR)) {
    strncpy(path, PHX_WAV_DIR, n);
    path[n-1] = 0;
    return true;
  }
  *slash = 0;
  if (strlen(path) < strlen(PHX_WAV_DIR)) {
    strncpy(path, PHX_WAV_DIR, n);
    path[n-1] = 0;
  }
  return true;
}

static void phxMakeBreadcrumb(const char *path, char *out, size_t n) {
  if (!out || n == 0) return;
  out[0] = 0;
  if (!path || !path[0] || !strcmp(path, PHX_WAV_DIR)) {
    strncpy(out, "WAV", n);
    out[n-1] = 0;
    return;
  }
  const char *p = path;
  if (!strncmp(p, PHX_WAV_DIR, strlen(PHX_WAV_DIR))) p += strlen(PHX_WAV_DIR);
  while (*p == '/') ++p;
  strncpy(out, "WAV", n);
  out[n-1] = 0;
  char temp[128];
  strncpy(temp, p, sizeof(temp));
  temp[sizeof(temp)-1] = 0;
  char *tok = strtok(temp, "/");
  while (tok) {
    if (strlen(out) + strlen(tok) + 4 >= n) {
      strncpy(out, "...", n);
      out[n-1] = 0;
      // keep last folder when too long
      strncat(out, " > ", n - strlen(out) - 1);
      strncat(out, tok, n - strlen(out) - 1);
    } else {
      strncat(out, " > ", n - strlen(out) - 1);
      strncat(out, tok, n - strlen(out) - 1);
    }
    tok = strtok(NULL, "/");
  }
}

static bool phxSdReadyForConfig() {
  // v0.7.36a: Keep the screen/browser SD state recoverable as well.
  // Previously a failed SD.begin() during boot was cached forever here,
  // even though PhoenixOS could later reinitialize the same card. This made
  // bank access work after hot insertion while WAV IMPORT still reported
  // "SD NOT READY" until reset.
  static bool ok = false;
  static uint32_t lastTryMs = 0;
  const uint32_t now = millis();

  if (SD.cardType() != CARD_NONE) { ok = true; return true; }
  ok = false;
  if (lastTryMs != 0 && (uint32_t)(now - lastTryMs) < 2000UL) return false;

  lastTryMs = now;
  ok = SD.begin(9, SPI);
  return ok;
}

static void phxEnsureDir(const char *path) {
  if (!SD.exists(path)) SD.mkdir(path);
}

static bool phxBoolFromText(const char *v, bool def) {
  if (!v) return def;
  if (!strcasecmp(v, "1") || !strcasecmp(v, "ON") || !strcasecmp(v, "TRUE") || !strcasecmp(v, "YES")) return true;
  if (!strcasecmp(v, "0") || !strcasecmp(v, "OFF") || !strcasecmp(v, "FALSE") || !strcasecmp(v, "NO")) return false;
  return def;
}

static void phxTrim(char *s) {
  if (!s) return;
  char *p = s;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
}

void PhoenixScreenManager::loadSystemSettings() {
  // v0.7.13c FIX8C: global device settings live in /PHOENIX/CONFIG.TXT.
  // If SD/config is unavailable, constructor defaults remain active.
  if (!phxSdReadyForConfig()) return;

  phxEnsureDir(PHX_ROOT_DIR);
  phxEnsureDir(PHX_BANKS_DIR);
  phxEnsureDir(PHX_WAV_DIR);
  phxEnsureDir(PHX_EXPORT_DIR);

  if (!SD.exists(PHX_CONFIG_PATH)) {
    saveSystemSettings();
    return;
  }

  File f = SD.open(PHX_CONFIG_PATH, FILE_READ);
  if (!f) return;

  char line[96];
  size_t pos = 0;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n' || pos >= sizeof(line) - 1) {
      line[pos] = 0;
      pos = 0;
      phxTrim(line);
      if (line[0] && line[0] != '#') {
        char *eq = strchr(line, '=');
        if (eq) {
          *eq = 0;
          char *key = line;
          char *val = eq + 1;
          phxTrim(key);
          phxTrim(val);
          if (!strcasecmp(key, "MIDI_CHANNEL")) {
            int v = atoi(val);
            if (v >= 1 && v <= 16) _midiChannel = (uint8_t)v;
          } else if (!strcasecmp(key, "MIDI_INPUT")) {
            _midiEnabled = phxBoolFromText(val, _midiEnabled);
          } else if (!strcasecmp(key, "MIDI_CLOCK")) {
            _midiClockEnabled = phxBoolFromText(val, _midiClockEnabled);
          } else if (!strcasecmp(key, "NOTE_TRIGGER")) {
            _midiNoteEnabled = phxBoolFromText(val, _midiNoteEnabled);
          } else if (!strcasecmp(key, "CC_CONTROL")) {
            _midiCcEnabled = phxBoolFromText(val, _midiCcEnabled);
          } else if (!strcasecmp(key, "PROGRAM_CHANGE")) {
            _midiPcEnabled = phxBoolFromText(val, _midiPcEnabled);
          } else if (!strcasecmp(key, "TRIGGER_MODE")) {
            _triggerAuto = !strcasecmp(val, "AUTO") || !strcasecmp(val, "1") || !strcasecmp(val, "ON");
          } else if (!strcasecmp(key, "TRIGGER_LEVEL")) {
            int v = atoi(val);
            if (v < 1) v = 1;
            if (v > 8) v = 8;
            _triggerLevel = (uint8_t)v;
          } else if (!strcasecmp(key, "SMART_MODE")) {
            if (!strcasecmp(val, "SMART") || !strcmp(val, "1")) _smartProcessingMode = 1U;
            else if (!strcasecmp(val, "FORCE") || !strcmp(val, "2")) _smartProcessingMode = 2U;
            else _smartProcessingMode = 0U;
          }
        }
      }
    } else if (c != '\r') {
      line[pos++] = c;
    }
  }
  if (pos) {
    line[pos] = 0;
    phxTrim(line);
    if (line[0] && line[0] != '#') {
      char *eq = strchr(line, '=');
      if (eq) {
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        phxTrim(key);
        phxTrim(val);
        if (!strcasecmp(key, "MIDI_CHANNEL")) {
          int v = atoi(val);
          if (v >= 1 && v <= 16) _midiChannel = (uint8_t)v;
        } else if (!strcasecmp(key, "MIDI_INPUT")) {
          _midiEnabled = phxBoolFromText(val, _midiEnabled);
        } else if (!strcasecmp(key, "MIDI_CLOCK")) {
          _midiClockEnabled = phxBoolFromText(val, _midiClockEnabled);
        } else if (!strcasecmp(key, "NOTE_TRIGGER")) {
          _midiNoteEnabled = phxBoolFromText(val, _midiNoteEnabled);
        } else if (!strcasecmp(key, "CC_CONTROL")) {
          _midiCcEnabled = phxBoolFromText(val, _midiCcEnabled);
        } else if (!strcasecmp(key, "PROGRAM_CHANGE")) {
          _midiPcEnabled = phxBoolFromText(val, _midiPcEnabled);
        } else if (!strcasecmp(key, "TRIGGER_MODE")) {
          _triggerAuto = !strcasecmp(val, "AUTO") || !strcasecmp(val, "1") || !strcasecmp(val, "ON");
        } else if (!strcasecmp(key, "TRIGGER_LEVEL")) {
          int v = atoi(val);
          if (v < 1) v = 1;
          if (v > 8) v = 8;
          _triggerLevel = (uint8_t)v;
        } else if (!strcasecmp(key, "SMART_MODE")) {
          if (!strcasecmp(val, "SMART") || !strcmp(val, "1")) _smartProcessingMode = 1U;
          else if (!strcasecmp(val, "FORCE") || !strcmp(val, "2")) _smartProcessingMode = 2U;
          else _smartProcessingMode = 0U;
        }
      }
    }
  }
  f.close();

  if (_midiChannel < 1 || _midiChannel > 16) _midiChannel = 1;
  if (_triggerLevel < 1) _triggerLevel = 1;
  if (_triggerLevel > 8) _triggerLevel = 8;
}

void PhoenixScreenManager::saveSystemSettings() {
  // v0.7.13c FIX8C: CONFIG.TXT is the single normal persistence backend.
  // No NVS writes. If SD is missing, defaults remain RAM-only for this boot.
  if (!phxSdReadyForConfig()) return;

  phxEnsureDir(PHX_ROOT_DIR);
  phxEnsureDir(PHX_BANKS_DIR);
  phxEnsureDir(PHX_WAV_DIR);
  phxEnsureDir(PHX_EXPORT_DIR);

  if (SD.exists(PHX_CONFIG_PATH)) SD.remove(PHX_CONFIG_PATH);
  File f = SD.open(PHX_CONFIG_PATH, FILE_WRITE);
  if (!f) return;

  f.println("# Project Phoenix Configuration");
  f.println("VERSION=2");
  f.println();
  f.print("MIDI_CHANNEL="); f.println(_midiChannel);
  f.print("MIDI_INPUT="); f.println(_midiEnabled ? "ON" : "OFF");
  f.print("MIDI_CLOCK="); f.println(_midiClockEnabled ? "ON" : "OFF");
  f.print("NOTE_TRIGGER="); f.println(_midiNoteEnabled ? "ON" : "OFF");
  f.print("CC_CONTROL="); f.println(_midiCcEnabled ? "ON" : "OFF");
  f.print("PROGRAM_CHANGE="); f.println(_midiPcEnabled ? "ON" : "OFF");
  f.println();
  f.print("TRIGGER_MODE="); f.println(_triggerAuto ? "AUTO" : "MANUAL");
  f.print("TRIGGER_LEVEL="); f.println(_triggerLevel);
  f.print("SMART_MODE=");
  f.println(_smartProcessingMode == 1U ? "SMART" : (_smartProcessingMode == 2U ? "FORCE" : "SAFE"));
  f.close();
}

void PhoenixScreenManager::saveMidiControlSettings() {
  saveSystemSettings();
}

void PhoenixScreenManager::saveMidiChannelSetting() {
  saveSystemSettings();
}

void PhoenixScreenManager::saveTriggerSettings() {
  saveSystemSettings();
}

PhoenixScreenManager::PhoenixScreenManager()
: _screen(SCR_BOOT), _diagnosticsReturnScreen(SCR_MAIN), _sel(0), _bootStart(0), _about(false), _overwriteConfirm(false), _overwriteSlot(0), _recordReturnScreen(SCR_SOUND), _recordCompleteSlot(0), _recordCompleteSaveState(-1), _serviceMode(false), _toneRequest(-1), _audioTestParam(0), _audioTestEditMode(false), _audioTestWaveform(0), _audioTestFreqHz(440), _audioTestLevelDb(-12), _audioTestOutput(false), _audioTestChanged(false),
  _recordRequest(false), _playRequest(false), _reversePlayRequest(false), _playFromCursorRequest(false), _playFromCursorFrame(0), _stopRequest(false), _selectSlotRequest(-1),
  _pitchParam(0), _pitchEditMode(false), _pitchChanged(false), _envParam(0), _envEditMode(false), _envChanged(false), _echoParam(0), _echoDelayMs(250), _echoFeedback(35), _echoMix(20), _echoChanged(false), _echoEditMode(false),
  _vintageParam(0), _vintageEditMode(false), _vintageChanged(false), _quattroMode(1), _quattroParam(0), _quattroEditMode(false), _quattroChanged(false), _voiceParam(0), _voiceEditMode(false), _voiceChanged(false), _mixerParam(0), _mixerEditMode(false), _mixerChanged(false), _filterParam(0), _filterEditMode(false), _filterChanged(false), _filterEnvParam(0), _filterEnvEditMode(false), _filterEnvChanged(false), _msSlot(0), _msGroup(0), _msLayer(0), _msParam(0), _msEditMode(false), _msChanged(false), _msPath(nullptr), _keygroupImportMode(false), _keygroupImportRequest(false), _keygroupDeleteRequest(false), _autoMapBrowserMode(false), _autoMapRequest(false), _autoMapSlot(0), _keygroupTargetSlot(0), _keygroupTargetGroup(0), _keygroupTargetLayer(0), _keygroupTargetVariant(0), _midiType(1), _midiChannel(1), _midiEnabled(true), _midiOmni(false), _midiClockEnabled(true), _midiNoteEnabled(true), _midiCcEnabled(true), _midiPcEnabled(true), _midiEditMode(false), _loopChanged(false), _loopRangeChanged(false), _sampleRangeChanged(false), _sampleMarkerEditPending(false), _sampleMarkerEditSlot(0), _sampleMarkerEditSampleStart(0), _sampleMarkerEditLoopStart(0), _sampleMarkerEditLoopEnd(0), _sampleMarkerEditSampleEnd(0), _zeroCrossSnapPending(false), _sampleProcessingAction(-1), _sampleProcessingSlot(0), _zeroCrossSnapSlot(0), _zeroCrossSnapMarker(0), _zeroCrossSnapFrame(0), _loopParam(0), _editorCursor(0), _editorMarker(0), _sampleEditorPlayRequest(false), _sampleEditorPlayStart(0), _sampleEditorPlayEnd(0), _sampleEditorPlayReverse(false), _triggerChanged(false), _triggerAuto(false), _triggerLevel(4), _smartProcessingMode(0), _triggerParam(0), _triggerEditMode(false), _triggerTestActive(false), _triggerTestAbove(false), _triggerTestPrevAbove(false), _triggerTestTrigUntilMs(0), _recUxStatus(0), _recUxUntilMs(0), _recUxPrevWaiting(false), _recUxPrevActive(false), _diskAction(-1), _usbStorageAction(-1), _usbStorageActive(false), _usbStorageAvailable(true), _usbStorageWritable(false), _usbStorageEjected(false), _usbStorageReadWriteSelected(false), _usbStorageExitConfirm(false), _diskBank(1), _diskBankUsed(false), _bankDirty(false), _diskConfirmAction(-1), _browserCount(0), _browserSel(0), _browserImportState(0), _browserImportSlot(0), _browserImportRequest(false), _browserPreviewRequest(false), _browserPreviewStopRequest(false),
  _diagnosticsResetRequest(false), _diagnosticsLogRequest(false), _diagnosticsStatusUntilMs(0), _diagnosticsLastAction(0), _diagElapsedMs(0), _diagVoices(0), _diagVoiceCapacity(12), _diagVoicePeak(0), _diagAudioAvgUs(0), _diagAudioPeakUs(0), _diagRiskCount(0), _diagOverrunCount(0), _diagUnderruns(0), _diagVoiceSteals(0), _diagHeapFreeKb(0), _diagHeapMinKb(0), _diagPsramFreeKb(0),
  _psramOk(false), _audioOk(false), _psramKb(0), _midiBytes(0), _underruns(0), _clipCount(0),
  _encSeen(false), _encSwSeen(false),
  _midiLearnActive(false), _midiLearnRequest(false), _midiLearnParam(PHX_PAR_SLOT_VOLUME), _midiLearnResultCc(255), _midiLearnResultAssigned(false), _midiLearnResultUntilMs(0), _midiLearnMenu(false), _midiLearnMenuSel(0), _midiResetConfirm(false), _recording(false), _selectedSlot(0), _playing(false), _playingReverse(false), _sampleReady(false), _sampleFrames(0), _recordFrames(0), _recordCapacityFrames(0), _playFrames(0), _samplePlayheadActive(false), _samplePlayheadFrame(0), _waveCursorFrame(0), _waveZoom(1), _editorZoom(1), _sampleRate(32000), _dcL(0), _dcR(0) {
  for (uint8_t i = 0; i < 4; ++i) { _quattroKeyLow[i] = i * 32; _quattroKeyHigh[i] = (i == 3) ? 127 : (i * 32 + 31); _quattroMidiChannel[i] = phxDefaultMultiChannel(_midiChannel, i); _pitchSemitone[i] = 0; _pitchFineCent[i] = 0; _pitchRootNote[i] = 60; _keyboardOctave[i] = 0; _pitchTrack[i] = true; _pitchBendRange[i] = 2; _envAttackMs[i] = 5; _envDecayMs[i] = 80; _envSustainPct[i] = 100; _envReleaseMs[i] = 80; _vintagePreset[i] = 0; _vintageRate[i] = 0; _vintageBits[i] = 0; _vintageFilter[i] = 0; _vintageJitter[i] = 0; _echoSend[i] = 0; _mixerLevel[i]=100; _mixerPan[i]=0; _voiceMode[i]=0; _voiceLimit[i]=12; _notePriority[i]=0; _glideMs[i]=0; _slotTrimEnabled[i]=false; _slotDcEnabled[i]=false; _slotNormalizeEnabled[i]=false; }
  memset(_btnSeen, 0, sizeof(_btnSeen));
  _analysisPeakPercent = 0;
  _analysisTrimApplied = false;
  _analysisDcDetected = false;
  _analysisSuggestedStart = 0;
  _analysisSuggestedEnd = 0;
  memset(_slotRecorded, 0, sizeof(_slotRecorded));
  memset(_slotRecording, 0, sizeof(_slotRecording));
  memset(_slotPlaying, 0, sizeof(_slotPlaying));
  memset(_slotFrames, 0, sizeof(_slotFrames));
  memset(_slotLoop, 0, sizeof(_slotLoop));
  memset(_slotLoopMode, 0, sizeof(_slotLoopMode));
  memset(_slotLoopXfadeMs, 0, sizeof(_slotLoopXfadeMs));
  memset(_slotLoopStart, 0, sizeof(_slotLoopStart));
  memset(_slotLoopEnd, 0, sizeof(_slotLoopEnd));
  memset(_slotSampleStart, 0, sizeof(_slotSampleStart));
  memset(_slotSampleEnd, 0, sizeof(_slotSampleEnd));
  strncpy(_usbStorageStatus, "USB STORAGE READY", sizeof(_usbStorageStatus));
  _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
  strncpy(_diskStatus, "BANK 01 CHECK", sizeof(_diskStatus));
  _diskStatus[sizeof(_diskStatus)-1] = 0;
  _browserStatus[0] = 0;
  strncpy(_browserPath, PHX_WAV_DIR, sizeof(_browserPath));
  _browserPath[sizeof(_browserPath)-1] = 0;
  _browserImportPath[0] = 0;
  _browserPreviewPath[0] = 0;
  _autoMapFolder[0] = 0;
  for (uint8_t i = 0; i < PHX_BROWSER_MAX; ++i) { _browserEntries[i].name[0] = 0; _browserEntries[i].isDir = false; _browserEntries[i].size = 0; }
  for(uint8_t i=0;i<4;++i){_filterCutoff[i]=100;_filterResonance[i]=0;_filterEnvAmount[i]=0;_filterVelocity[i]=0;_filterKeytrack[i]=0;_filterAttackMs[i]=0;_filterDecayMs[i]=250;_filterSustainPct[i]=0;_filterReleaseMs[i]=300;}
  for(uint8_t s=0;s<4;++s)for(uint8_t g=0;g<16;++g){_msEnabled[s][g]=false;_msLow[s][g]=0;_msHigh[s][g]=127;_msRoot[s][g]=60;_msTranspose[s][g]=0;_msFineCent[s][g]=0;_msKeytrackPct[s][g]=100;for(uint8_t l=0;l<3;++l){_msLayerEnabled[s][g][l]=false;_msVelLow[s][g][l]=(l==0)?1:(uint8_t)(l*43+1);_msVelHigh[s][g][l]=(l==2)?127:(uint8_t)((l+1)*43);_msLevel[s][g][l]=100;_msPan[s][g][l]=0;_msRrMode[s][g][l]=0;_msRrCount[s][g][l]=0;_msLoopMode[s][g][l]=0;_msLoopStartPct[s][g][l]=20;_msLoopEndPct[s][g][l]=80;_msLoopXfadeMs[s][g][l]=0;_msVelToLevel[s][g][l]=100;_msVelToFilter[s][g][l]=0;}}
  _seqPendingClick=0; _seqPendingClickMs=0; _seqQuickTrackUntilMs=0;
    _seqRun=false; _seqEditMode=false; _seqTrack=0; _seqStep=0; _seqParam=0; _seqPattern=0; _patternAction=0; _patternTarget=1; _seqBpm=120; _seqExternalClock=false; _seqExternalClockPresent=false; _seqExternalBpm=120; _seqPreviewRequest=false; _seqPreviewTrack=0; _seqPreviewNote=60; _seqPreviewVelocity=100;
  _songEditPos=0; _songEditField=0; _songAction=0; _songLoopMode=1; _songLoopStart=0; _songLoopEnd=0; _songDataChanged=false; _songPlayRequest=false; _songStopRequest=false; _songSaveRequest=false; _songPlaying=false; _songPlayPos=0; _songPlayRepeat=0; _songPlayRepeatTarget=1;
  for(uint8_t i=0;i<16;++i){_songPattern[i]=0;_songRepeats[i]=1;_songEnd[i]=true;} _songEnd[0]=false;
  for(uint8_t t=0;t<4;++t) _seqPlayhead[t]=0;
  for(uint8_t p=0;p<4;++p) for(uint8_t t=0;t<4;++t){
    _seqLength[p][t]=16;
    for(uint8_t st=0;st<16;++st){_seqOn[p][t][st]=false;_seqNote[p][t][st]=60+(t*2);_seqVelocity[p][t][st]=100;_seqGate[p][t][st]=50;}
  }
  _seqOn[0][0][0]=true; _seqOn[0][0][8]=true;
  _seqOn[0][1][4]=true; _seqOn[0][1][12]=true;
  for(uint8_t st=0;st<16;st+=2) _seqOn[0][2][st]=true;
}

void PhoenixScreenManager::restoreSessionView(uint8_t screen, uint8_t slot) {
  _selectedSlot = slot & 3U;
  // Restore only stable work pages; transient dialogs, boot and service pages fall back to main.
  if (screen >= (uint8_t)SCR_MAIN && screen <= (uint8_t)SCR_SONG_EDITOR &&
      screen != (uint8_t)SCR_SAMPLE_BROWSER && screen != (uint8_t)SCR_PATTERN_MANAGER &&
      screen != (uint8_t)SCR_DIAGNOSTICS) {
    go((ScreenID)screen);
  } else {
    go(SCR_MAIN);
  }
}

void PhoenixScreenManager::selectSequencerTrack(uint8_t track) {
  _seqTrack = track & 3U;
  const uint8_t len = _seqLength[_seqPattern][_seqTrack] ? _seqLength[_seqPattern][_seqTrack] : 1U;
  if (_seqStep >= len) _seqStep = (uint8_t)(len - 1U);
  _seqQuickTrackUntilMs = millis() + 350UL;
}

bool PhoenixScreenManager::consumeSequencerPreviewRequest(uint8_t &track, uint8_t &note, uint8_t &velocity) {
  if (!_seqPreviewRequest) return false;
  track = _seqPreviewTrack & 3;
  note = _seqPreviewNote;
  velocity = _seqPreviewVelocity;
  _seqPreviewRequest = false;
  return true;
}


void PhoenixScreenManager::clearSequencerPattern() {
  _seqRun=false; _seqPattern=0; _seqStep=0; _seqTrack=0; _seqParam=0; _seqBpm=120;
  _seqExternalClock=false; _seqExternalClockPresent=false; _seqExternalBpm=120; _seqPreviewRequest=false;
  for(uint8_t t=0;t<4;++t) _seqPlayhead[t]=0;
  for(uint8_t p=0;p<4;++p) for(uint8_t t=0;t<4;++t){
    _seqLength[p][t]=16;
    for(uint8_t st=0;st<16;++st){
      _seqOn[p][t][st]=false; _seqNote[p][t][st]=(uint8_t)(60+t*2); _seqVelocity[p][t][st]=100; _seqGate[p][t][st]=50;
    }
  }
}

bool PhoenixScreenManager::saveSequencerConfig(const char *bankDir) const {
  if(!bankDir||!*bankDir) return false;
  char path[192],tmp[192],bak[192];
  snprintf(path,sizeof(path),"%s/PATTERNS.CFG",bankDir); snprintf(tmp,sizeof(tmp),"%s/PATTERNS.TMP",bankDir); snprintf(bak,sizeof(bak),"%s/PATTERNS.BAK",bankDir);
  SD.remove(tmp);
  delay(1); // C036b: SD metadata operation must allow IDLE0 to run.
  File f=SD.open(tmp,FILE_WRITE); if(!f) return false;
  f.println("VERSION=1"); f.print("CURRENT_PATTERN=");f.println((unsigned)(_seqPattern+1)); f.print("CLOCK_MODE=");f.println(_seqExternalClock?"EXT":"INT"); f.print("BPM=");f.println((unsigned)_seqBpm);
  uint16_t linesSinceYield = 0;
  for(uint8_t p=0;p<4;++p){ f.println(); f.print("[P");f.print((unsigned)(p+1));f.println("]");
    for(uint8_t t=0;t<4;++t){ f.print("TRACK");f.print((unsigned)(t+1));f.print("_LENGTH=");f.println((unsigned)_seqLength[p][t]);
      for(uint8_t st=0;st<16;++st){ f.print("T");f.print((unsigned)(t+1));f.print("_STEP");if(st<9)f.print('0');f.print((unsigned)(st+1));f.print('=');
        f.print(_seqOn[p][t][st]?1:0);f.print(',');f.print((unsigned)_seqNote[p][t][st]);f.print(',');f.print((unsigned)_seqVelocity[p][t][st]);f.print(',');f.println((unsigned)_seqGate[p][t][st]);
        // C036b: many tiny FAT/SPI writes can monopolize PhoenixControl for seconds
        // on a slow/fragmented card. A real 1-tick sleep every eight step records
        // guarantees IDLE0 gets CPU time and keeps the task watchdog serviced.
        if (++linesSinceYield >= 8U) { linesSinceYield = 0; delay(1); }
      }
      delay(1);
    }
  }
  f.flush();
  delay(1);
  f.close();
  SD.remove(bak); delay(1);
  if(SD.exists(path)){ if(!SD.rename(path,bak)) return false; delay(1); }
  if(!SD.rename(tmp,path)){if(SD.exists(bak))SD.rename(bak,path);return false;}
  delay(1);
  return true;
}

bool PhoenixScreenManager::loadSequencerFile(const char *path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  clearSequencerPattern();
  uint8_t track = 0xFF;
  bool sawVersion = false;
  bool sawStep = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line[0] == ';' || line[0] == '#') continue;
    if (line == "VERSION=1" || line == "VERSION=2") { sawVersion = true; continue; }
    if (line.startsWith("CLOCK_MODE=")) {
      String v = line.substring(11); v.trim(); v.toUpperCase();
      _seqExternalClock = (v == "EXT");
      continue;
    }
    if (line.startsWith("BPM=")) {
      int v = line.substring(4).toInt();
      _seqBpm = (uint16_t)constrain(v, 40, 240);
      continue;
    }
    if (line.startsWith("[TRACK") && line.endsWith("]")) {
      int n = line.substring(6, line.length() - 1).toInt();
      track = (n >= 1 && n <= 4) ? (uint8_t)(n - 1) : 0xFF;
      continue;
    }
    if (track < 4 && line.startsWith("LENGTH=")) {
      _seqLength[_seqPattern][track] = (uint8_t)constrain(line.substring(7).toInt(), 1, 16);
      continue;
    }
    if (track < 4 && line.startsWith("STEP")) {
      int eq = line.indexOf('=');
      if (eq < 0) continue;
      int n = line.substring(4, eq).toInt();
      if (n < 1 || n > 16) continue;
      String data = line.substring(eq + 1);
      int c1 = data.indexOf(',');
      int c2 = (c1 >= 0) ? data.indexOf(',', c1 + 1) : -1;
      int c3 = (c2 >= 0) ? data.indexOf(',', c2 + 1) : -1;
      if (c1 < 0 || c2 < 0 || c3 < 0) continue;
      const uint8_t st = (uint8_t)(n - 1);
      _seqOn[_seqPattern][track][st] = data.substring(0, c1).toInt() != 0;
      _seqNote[_seqPattern][track][st] = (uint8_t)constrain(data.substring(c1 + 1, c2).toInt(), 0, 127);
      _seqVelocity[_seqPattern][track][st] = (uint8_t)constrain(data.substring(c2 + 1, c3).toInt(), 1, 127);
      _seqGate[_seqPattern][track][st] = (uint8_t)constrain(data.substring(c3 + 1).toInt(), 10, 100);
      sawStep = true;
    }
  }
  f.close();
  _seqRun = false;
  for (uint8_t t = 0; t < 4; ++t) _seqPlayhead[t] = 0;
  return sawVersion && sawStep;
}

bool PhoenixScreenManager::loadSequencerConfig(const char *bankDir) {
  if(!bankDir||!*bankDir) return false; clearSequencerPattern();
  char path[192]; snprintf(path,sizeof(path),"%s/PATTERNS.CFG",bankDir); File f=SD.open(path,FILE_READ);
  if(!f){
    snprintf(path,sizeof(path),"%s/SEQUENCER.CFG",bankDir);
    if (!SD.exists(path)) {
      // Older audio-only and multisample-only banks are valid projects.
      // Missing pattern data means an empty P1..P4 set, not LOAD FAILED.
      _seqRun=false; _seqPattern=0; _seqStep=0; _seqBpm=120; _seqExternalClock=false;
      for(uint8_t t=0;t<4;++t)_seqPlayhead[t]=0;
      return true;
    }
    return loadSequencerFile(path);
  }
  int pat=-1;
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();if(!line.length()||line[0]=='#')continue;
    if(line.startsWith("[P")&&line.endsWith("]")){pat=constrain(line.substring(2,line.length()-1).toInt()-1,0,3);continue;}
    if(line.startsWith("CURRENT_PATTERN=")){_seqPattern=(uint8_t)constrain(line.substring(16).toInt()-1,0,3);continue;}
    if(line.startsWith("CLOCK_MODE=")){_seqExternalClock=line.substring(11)=="EXT";continue;}
    if(line.startsWith("BPM=")){_seqBpm=(uint16_t)constrain(line.substring(4).toInt(),40,240);continue;}
    if(pat<0)continue;
    if(line.startsWith("TRACK")&&line.indexOf("_LENGTH=")>0){int u=line.indexOf('_');int t=constrain(line.substring(5,u).toInt()-1,0,3);_seqLength[pat][t]=(uint8_t)constrain(line.substring(line.indexOf('=')+1).toInt(),1,16);continue;}
    if(line[0]=='T'){int sep=line.indexOf("_STEP"),eq=line.indexOf('=');if(sep<0||eq<0)continue;int t=constrain(line.substring(1,sep).toInt()-1,0,3),st=constrain(line.substring(sep+5,eq).toInt()-1,0,15);String d=line.substring(eq+1);int c1=d.indexOf(','),c2=d.indexOf(',',c1+1),c3=d.indexOf(',',c2+1);if(c1<0||c2<0||c3<0)continue;_seqOn[pat][t][st]=d.substring(0,c1).toInt()!=0;_seqNote[pat][t][st]=(uint8_t)constrain(d.substring(c1+1,c2).toInt(),0,127);_seqVelocity[pat][t][st]=(uint8_t)constrain(d.substring(c2+1,c3).toInt(),1,127);_seqGate[pat][t][st]=(uint8_t)constrain(d.substring(c3+1).toInt(),10,100);}
  }
  f.close();_seqRun=false;_seqStep=0;for(uint8_t t=0;t<4;++t)_seqPlayhead[t]=0;return true;
}


void PhoenixScreenManager::setSongEntryStatus(uint8_t index,uint8_t pattern,uint8_t repeats,bool end){if(index>=16)return;_songPattern[index]=pattern&3U;_songRepeats[index]=(uint8_t)constrain((int)repeats,1,16);_songEnd[index]=end;}
void PhoenixScreenManager::setSongLoopStatus(uint8_t mode,uint8_t start,uint8_t end){_songLoopMode=(uint8_t)constrain((int)mode,0,2);_songLoopStart=start&15U;_songLoopEnd=end&15U;if(_songLoopEnd<_songLoopStart)_songLoopEnd=_songLoopStart;}
void PhoenixScreenManager::setSongPlaybackStatus(bool playing,uint8_t position,uint8_t repeatIndex,uint8_t repeatTarget){_songPlaying=playing;_songPlayPos=position&15U;_songPlayRepeat=repeatIndex;_songPlayRepeatTarget=repeatTarget<1?1:repeatTarget;}
bool PhoenixScreenManager::consumeSongDataChanged(){bool v=_songDataChanged;_songDataChanged=false;return v;}
bool PhoenixScreenManager::consumeSongPlayRequest(){bool v=_songPlayRequest;_songPlayRequest=false;return v;}
bool PhoenixScreenManager::consumeSongStopRequest(){bool v=_songStopRequest;_songStopRequest=false;return v;}
bool PhoenixScreenManager::consumeSongSaveRequest(){bool v=_songSaveRequest;_songSaveRequest=false;return v;}

bool PhoenixScreenManager::consumeQuattroChanged() { bool v = _quattroChanged; _quattroChanged = false; return v; }
bool PhoenixScreenManager::consumeVoiceChanged() { bool v = _voiceChanged; _voiceChanged = false; return v; }
bool PhoenixScreenManager::consumeMixerChanged() { bool v = _mixerChanged; _mixerChanged = false; return v; }
bool PhoenixScreenManager::consumeFilterChanged(){bool v=_filterChanged;_filterChanged=false;return v;}
bool PhoenixScreenManager::consumeFilterEnvelopeChanged(){bool v=_filterEnvChanged;_filterEnvChanged=false;return v;}
bool PhoenixScreenManager::consumeMultisampleChanged(){ bool v=_msChanged; _msChanged=false; return v; }
bool PhoenixScreenManager::consumeKeygroupImportRequest(char *path,size_t pathLen,uint8_t &slot,uint8_t &group,uint8_t &layer){if(!_keygroupImportRequest)return false;if(path&&pathLen){strncpy(path,_keygroupImportPath,pathLen);path[pathLen-1]=0;}slot=_keygroupTargetSlot;group=_keygroupTargetGroup;layer=_keygroupTargetLayer;_keygroupImportRequest=false;return true;}
bool PhoenixScreenManager::consumeKeygroupDeleteRequest(uint8_t &slot,uint8_t &group){if(!_keygroupDeleteRequest)return false;slot=_keygroupTargetSlot;group=_keygroupTargetGroup;_keygroupDeleteRequest=false;return true;}
bool PhoenixScreenManager::consumeMultisampleAutoMapRequest(char *folder,size_t folderLen,uint8_t &slot){if(!_multisampleAutoMapRequest)return false;if(folder&&folderLen){strncpy(folder,_multisampleAutoMapFolder,folderLen);folder[folderLen-1]=0;}slot=_multisampleAutoMapSlot&3;_multisampleAutoMapRequest=false;return true;}
bool PhoenixScreenManager::consumeAutoMapRequest(char *folder,size_t folderLen,uint8_t &slot){if(!_autoMapRequest)return false;if(folder&&folderLen){strncpy(folder,_autoMapFolder,folderLen);folder[folderLen-1]=0;}slot=_autoMapSlot&3;_autoMapRequest=false;return true;}
void PhoenixScreenManager::setMultisampleKeygroupStatus(uint8_t slot,uint8_t group,bool enabled,uint8_t low,uint8_t high,uint8_t root,uint8_t chokeGroup,bool oneShot,uint16_t chokeFadeMs,uint8_t playMode,uint8_t exclusiveGroup,bool retriggerLegato,uint8_t startPct,uint8_t endPct,bool reversePlayback,int8_t transpose,int16_t fineCent,uint8_t keytrackPct){slot&=3;group&=15;_msEnabled[slot][group]=enabled;_msLow[slot][group]=low;_msHigh[slot][group]=high<low?low:high;_msRoot[slot][group]=root;_msChoke[slot][group]=chokeGroup>8?8:chokeGroup;_msOneShot[slot][group]=oneShot;_msChokeFadeMs[slot][group]=chokeFadeMs>250?250:chokeFadeMs;_msPlayMode[slot][group]=playMode>2?2:playMode;_msExclusiveGroup[slot][group]=exclusiveGroup>8?8:exclusiveGroup;_msRetriggerLegato[slot][group]=retriggerLegato;_msStartPct[slot][group]=startPct>99?99:startPct;_msEndPct[slot][group]=endPct<1?1:(endPct>100?100:endPct);_msReverse[slot][group]=reversePlayback;_msTranspose[slot][group]=(int8_t)constrain((int)transpose,-24,24);_msFineCent[slot][group]=(int16_t)constrain((int)fineCent,-100,100);_msKeytrackPct[slot][group]=keytrackPct>100?100:keytrackPct;}
void PhoenixScreenManager::setMultisampleLayerStatus(uint8_t slot,uint8_t group,uint8_t layer,bool enabled,uint8_t velocityLow,uint8_t velocityHigh,uint8_t level,int8_t pan,const char *path,uint8_t rrMode,uint8_t rrCount,uint8_t loopMode,uint8_t loopStartPct,uint8_t loopEndPct,uint8_t loopXfadeMs,uint8_t velToLevelPct,uint8_t velToFilterPct){slot&=3;group&=15;layer%=3;_msLayerEnabled[slot][group][layer]=enabled;_msVelLow[slot][group][layer]=velocityLow;_msVelHigh[slot][group][layer]=velocityHigh;_msLevel[slot][group][layer]=level;_msPan[slot][group][layer]=pan;_msRrMode[slot][group][layer]=rrMode;_msRrCount[slot][group][layer]=rrCount;_msLoopMode[slot][group][layer]=loopMode>2?2:loopMode;_msLoopStartPct[slot][group][layer]=loopStartPct>98?98:loopStartPct;_msLoopEndPct[slot][group][layer]=loopEndPct<1?1:(loopEndPct>100?100:loopEndPct);_msLoopXfadeMs[slot][group][layer]=loopXfadeMs>50?50:loopXfadeMs;_msVelToLevel[slot][group][layer]=velToLevelPct>100?100:velToLevelPct;_msVelToFilter[slot][group][layer]=velToFilterPct>100?100:velToFilterPct;if(_msPath&&path){strncpy(_msPath[slot][group][layer][0],path,sizeof(_msPath[slot][group][layer][0]));_msPath[slot][group][layer][0][sizeof(_msPath[slot][group][layer][0])-1]=0;}}
void PhoenixScreenManager::setMultisampleVariantPath(uint8_t slot,uint8_t group,uint8_t layer,uint8_t variant,const char*path){slot&=3;group&=15;layer%=3;variant&=3;if(!_msPath)return;if(path){strncpy(_msPath[slot][group][layer][variant],path,sizeof(_msPath[slot][group][layer][variant]));_msPath[slot][group][layer][variant][sizeof(_msPath[slot][group][layer][variant])-1]=0;}else _msPath[slot][group][layer][variant][0]=0;}
void PhoenixScreenManager::setFilterSlotStatus(uint8_t slot,uint8_t c,uint8_t r,int8_t e,uint8_t v,uint8_t k,uint16_t a,uint16_t d,uint8_t su,uint16_t re){slot&=3;_filterCutoff[slot]=c;_filterResonance[slot]=r;_filterEnvAmount[slot]=e;_filterVelocity[slot]=v;_filterKeytrack[slot]=k;_filterAttackMs[slot]=a;_filterDecayMs[slot]=d;_filterSustainPct[slot]=su;_filterReleaseMs[slot]=re;}
void PhoenixScreenManager::setVoiceSlotStatus(uint8_t slot, uint8_t mode, uint8_t limit, uint8_t priority, uint16_t glideMs) {
  slot &= 3; _voiceMode[slot]=mode>2?2:mode; _voiceLimit[slot]=constrain((int)limit,1,12); _notePriority[slot]=priority>2?2:priority; _glideMs[slot]=constrain((int)glideMs,0,2000);
}
void PhoenixScreenManager::setMixerSlotStatus(uint8_t slot, uint8_t level, int8_t pan, uint8_t echoSend) {
  slot &= 3; _mixerLevel[slot]=(uint8_t)constrain((int)level,0,100); _mixerPan[slot]=(int8_t)constrain((int)pan,-100,100); _echoSend[slot]=(uint8_t)constrain((int)echoSend,0,100);
}

void PhoenixScreenManager::setQuattroStatus(uint8_t mode, const uint8_t low[4], const uint8_t high[4], const uint8_t channel[4]) {
  _quattroMode = mode ? 1 : 0;
  for (uint8_t i=0;i<4;++i) {
    _quattroKeyLow[i] = low ? (uint8_t)constrain((int)low[i],0,127) : (uint8_t)(i*32);
    _quattroKeyHigh[i] = high ? (uint8_t)constrain((int)high[i],0,127) : (uint8_t)((i==3)?127:(i*32+31));
    if (_quattroKeyHigh[i] < _quattroKeyLow[i]) _quattroKeyHigh[i] = _quattroKeyLow[i];
    _quattroMidiChannel[i] = channel ? constrain((int)channel[i],1,16) : phxDefaultMultiChannel(_midiChannel, i);
  }
}

bool PhoenixScreenManager::allocateMultisamplePathCache() {
  if (_msPath) return true;

  const size_t bytes = (size_t)4U * 16U * 3U * 4U * 160U;
  void *memory = nullptr;
  bool inPsram = false;

  if (psramFound()) {
    memory = ps_malloc(bytes);
    inPsram = (memory != nullptr);
  }
  if (!memory) memory = malloc(bytes);
  if (!memory) {
    Serial.printf("MEM ERROR: multisample path cache allocation failed (%u bytes)\n",
                  (unsigned)bytes);
    return false;
  }

  _msPath = reinterpret_cast<char (*)[16][3][4][160]>(memory);
  memset(_msPath, 0, bytes);
  Serial.printf("MEM: multisample path cache %u bytes in %s\n",
                (unsigned)bytes, inPsram ? "PSRAM" : "internal heap fallback");
  return true;
}


void PhoenixScreenManager::begin(bool serviceMode) {
  allocateMultisamplePathCache();
  _screen = SCR_BOOT;
  _sel = 0;
  _about = false;
  _overwriteConfirm = false;
  _overwriteSlot = 0;
  _recordReturnScreen = SCR_SOUND;
  _recordCompleteSlot = 0;
  _recordCompleteSaveState = -1;
  _serviceMode = serviceMode;
  loadSystemSettings();
  // v0.7.18a: Multi-Mode defaults are derived once from the loaded base MIDI channel.
  // The base channel is only a template here; Multi Mode itself still ignores it.
  for (uint8_t i = 0; i < 4; ++i) {
    _quattroMidiChannel[i] = phxDefaultMultiChannel(_midiChannel, i);
  }
  _toneRequest = -1;
  _recordRequest = false;
  _playRequest = false;
  _reversePlayRequest = false;
  _playFromCursorRequest = false;
  _reversePlayFromCursorRequest = false;
  _playFromCursorFrame = 0;
  _waveReverseMode = false;
  _stopRequest = false;
  _selectSlotRequest = -1;
  _pitchChanged = false;
  _pitchParam = 0;
  _pitchEditMode = false;
  _echoEditMode = false;
  _diskAction = 3;
  _diskConfirmAction = -1;
  _browserImportState = 0;
  _browserImportRequest = false;
  _browserPreviewRequest = false;
  _browserPreviewStopRequest = false;
  _loopChanged = false;
  _loopRangeChanged = false;
  _sampleRangeChanged = false;
  _editorCursor = 0;
  _editorMarker = 0;
  _sampleEditorPlayRequest = false;
  _sampleEditorPlayStart = 0;
  _sampleEditorPlayEnd = 0;
  _sampleEditorPlayReverse = false;
  _triggerChanged = false;
  _triggerEditMode = false;
  _triggerTestActive = false;
  _triggerTestAbove = false;
  _triggerTestPrevAbove = false;
  _triggerTestTrigUntilMs = 0;
  _seqRun = false;
  for (uint8_t t = 0; t < 4; ++t) _seqPlayhead[t] = 0;
  _recUxStatus = 0;
  _recUxUntilMs = 0;
  _recUxPrevWaiting = false;
  _recUxPrevActive = false;
  _bootStart = millis();
}

void PhoenixScreenManager::setHardwareStatus(bool psramOk, uint32_t psramKb, bool audioOk, uint32_t midiBytes, uint32_t underruns, uint32_t clipCount) {
  _psramOk = psramOk;
  _psramKb = psramKb;
  _audioOk = audioOk;
  _midiBytes = midiBytes;
  _underruns = underruns;
  _clipCount = clipCount;
}


void PhoenixScreenManager::setDiagnosticsStatus(uint32_t elapsedMs, uint8_t voices, uint8_t voiceCapacity, uint8_t voicePeak,
                                                uint32_t audioAvgUs, uint32_t audioPeakUs, uint32_t riskCount,
                                                uint32_t overrunCount, uint32_t underruns, uint32_t voiceSteals,
                                                uint32_t heapFreeKb, uint32_t heapMinKb, uint32_t psramFreeKb) {
  _diagElapsedMs = elapsedMs;
  _diagVoices = voices;
  _diagVoiceCapacity = voiceCapacity;
  _diagVoicePeak = voicePeak;
  _diagAudioAvgUs = audioAvgUs;
  _diagAudioPeakUs = audioPeakUs;
  _diagRiskCount = riskCount;
  _diagOverrunCount = overrunCount;
  _diagUnderruns = underruns;
  _diagVoiceSteals = voiceSteals;
  _diagHeapFreeKb = heapFreeKb;
  _diagHeapMinKb = heapMinKb;
  _diagPsramFreeKb = psramFreeKb;
}

bool PhoenixScreenManager::consumeDiagnosticsResetRequest() {
  const bool r = _diagnosticsResetRequest;
  _diagnosticsResetRequest = false;
  return r;
}

bool PhoenixScreenManager::consumeDiagnosticsLogRequest() {
  const bool r = _diagnosticsLogRequest;
  _diagnosticsLogRequest = false;
  return r;
}

bool PhoenixScreenManager::diagnosticsVisible() const {
  return _screen == SCR_DIAGNOSTICS;
}

void PhoenixScreenManager::setTransportStatus(bool recording, bool playing, bool sampleReady, uint32_t frames, uint32_t recordFrames, uint32_t recordCapacityFrames, uint32_t playFrames, uint32_t sampleRate, int16_t dcL, int16_t dcR, uint8_t selectedSlot) {
  _recording = recording;
  _playing = playing;
  _sampleReady = sampleReady;
  if (!playing) _playingReverse = false;
  _sampleFrames = frames;
  _recordFrames = recordFrames;
  _recordCapacityFrames = recordCapacityFrames;
  _playFrames = playFrames;
  _sampleRate = sampleRate;
  _dcL = dcL;
  _dcR = dcR;
  _selectedSlot = selectedSlot < 4 ? selectedSlot : 0;
}

void PhoenixScreenManager::setSamplePlayheadStatus(bool active, uint32_t frame) {
  _samplePlayheadActive = active;
  _samplePlayheadFrame = frame;
}

void PhoenixScreenManager::setRecorderUxStatus(bool waitingForTrigger, bool activelyRecording) {
  const uint32_t now = millis();

  if (waitingForTrigger) {
    _recUxStatus = 1;       // WAIT TRIG
    _recUxUntilMs = 0;
  } else if (activelyRecording) {
    if (_recUxPrevWaiting && !_recUxPrevActive) {
      _recUxStatus = 3;     // TRIGGER!
      _recUxUntilMs = now + 450;
    } else if (_recUxStatus != 3 || now >= _recUxUntilMs) {
      _recUxStatus = 2;     // RECORDING
      _recUxUntilMs = 0;
    }
  } else {
    if (_recUxPrevWaiting || _recUxPrevActive) {
      _recUxStatus = (_recordFrames > 0) ? 4 : 5; // DONE / CANCELLED
      _recUxUntilMs = now + 1800;
    } else if (_recUxUntilMs && now >= _recUxUntilMs) {
      _recUxStatus = 0;
      _recUxUntilMs = 0;
    }
  }

  _recUxPrevWaiting = waitingForTrigger;
  _recUxPrevActive = activelyRecording;
}

void PhoenixScreenManager::setTriggerTestInput(bool thresholdReached) {
  if (!_triggerTestActive) {
    _triggerTestAbove = false;
    _triggerTestPrevAbove = false;
    _triggerTestTrigUntilMs = 0;
    return;
  }

  const uint32_t now = millis();
  // Latch short transients long enough to be visible on the 45 ms GUI refresh.
  if (thresholdReached && !_triggerTestPrevAbove) _triggerTestTrigUntilMs = now + 700UL;
  _triggerTestAbove = thresholdReached;
  _triggerTestPrevAbove = thresholdReached;
}

void PhoenixScreenManager::setSampleAnalysisSummary(uint8_t peakPercent, bool dcDetected,
                                                     uint32_t suggestedStart, uint32_t suggestedEnd,
                                                     bool trimApplied) {
  _analysisPeakPercent = peakPercent;
  _analysisDcDetected = dcDetected;
  _analysisSuggestedStart = suggestedStart;
  _analysisSuggestedEnd = suggestedEnd;
  _analysisTrimApplied = trimApplied;
  _recUxStatus = 6; // SMART PROCESSING RESULT
  _recUxUntilMs = millis() + 2500UL;
}


void PhoenixScreenManager::setSlotStatus(const bool recorded[4], const bool recording[4], const bool playing[4], const uint32_t frames[4]) {
  for (uint8_t i = 0; i < 4; ++i) {
    _slotRecorded[i] = recorded ? recorded[i] : false;
    _slotRecording[i] = recording ? recording[i] : false;
    _slotPlaying[i] = playing ? playing[i] : false;
    _slotFrames[i] = frames ? frames[i] : 0;
  }
}


void PhoenixScreenManager::setLoopStatus(const bool loopEnabled[4]) {
  // Compatibility bridge for older callers: enabled means FORWARD unless an
  // already selected ALTERNATE mode should be preserved.
  for (uint8_t i = 0; i < 4; ++i) {
    const bool enabled = loopEnabled ? loopEnabled[i] : false;
    _slotLoop[i] = enabled;
    if (!enabled) _slotLoopMode[i] = 0;
    else if (_slotLoopMode[i] == 0 || _slotLoopMode[i] > 2) _slotLoopMode[i] = 1;
  }
}

void PhoenixScreenManager::setLoopModeStatus(const uint8_t loopMode[4]) {
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t mode = loopMode ? (uint8_t)constrain((int)loopMode[i], 0, 2) : 0;
    _slotLoopMode[i] = mode;
    _slotLoop[i] = (mode != 0);
  }
}

void PhoenixScreenManager::setLoopCrossfadeStatus(const uint8_t xfadeMs[4]) {
  for (uint8_t i=0;i<4;++i) _slotLoopXfadeMs[i] = xfadeMs ? xfadeMs[i] : 0U;
}

void PhoenixScreenManager::setLoopRangeStatus(const uint32_t loopStart[4], const uint32_t loopEnd[4]) {
  for (uint8_t i = 0; i < 4; ++i) {
    _slotLoopStart[i] = loopStart ? loopStart[i] : 0;
    _slotLoopEnd[i] = loopEnd ? loopEnd[i] : 0;
  }
}

void PhoenixScreenManager::setSampleRangeStatus(const uint32_t sampleStart[4], const uint32_t sampleEnd[4]) {
  for (uint8_t i = 0; i < 4; ++i) {
    _slotSampleStart[i] = sampleStart ? sampleStart[i] : 0;
    _slotSampleEnd[i] = sampleEnd ? sampleEnd[i] : 0;
  }
}

void PhoenixScreenManager::setEchoStatus(uint16_t delayMs, uint8_t feedback, uint8_t mix, const uint8_t send[4]) {
  if (delayMs < 50) delayMs = 50;
  if (delayMs > 1000) delayMs = 1000;
  if (feedback > 90) feedback = 90;
  if (mix > 100) mix = 100;
  _echoDelayMs = delayMs;
  _echoFeedback = feedback;
  _echoMix = mix;
  for (uint8_t i = 0; i < 4; ++i) _echoSend[i] = send ? constrain((int)send[i], 0, 100) : 0;
}

void PhoenixScreenManager::setTriggerStatus(bool automatic, uint8_t level) {
  if (level < 1) level = 1;
  if (level > 8) level = 8;
  _triggerAuto = automatic;
  _triggerLevel = level;
}

void PhoenixScreenManager::setPitchSlotStatus(uint8_t slot, int8_t semitone, int16_t fineCent, uint8_t rootNote, int8_t octaveOffset, bool pitchTrack, uint8_t bendRange) {
  if (slot >= 4) return;
  if (semitone < -24) semitone = -24;
  if (semitone > 24) semitone = 24;
  if (fineCent < -100) fineCent = -100;
  if (fineCent > 100) fineCent = 100;
  if (octaveOffset < -2) octaveOffset = -2;
  if (octaveOffset > 2) octaveOffset = 2;
  if (rootNote > 127) rootNote = 127;
  _pitchSemitone[slot] = semitone;
  _pitchFineCent[slot] = fineCent;
  _pitchRootNote[slot] = rootNote;
  _keyboardOctave[slot] = octaveOffset;
  _pitchTrack[slot] = pitchTrack;
  _pitchBendRange[slot] = bendRange;
}

void PhoenixScreenManager::setEnvelopeSlotStatus(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs) {
  slot &= 3;
  _envAttackMs[slot] = attackMs;
  _envDecayMs[slot] = decayMs;
  _envSustainPct[slot] = sustainPct;
  _envReleaseMs[slot] = releaseMs;
}

void PhoenixScreenManager::setVintageSlotStatus(uint8_t slot, uint8_t preset, uint8_t rate, uint8_t bits, uint8_t filter, uint8_t jitter) {
  slot &= 3; _vintagePreset[slot]=preset; _vintageRate[slot]=rate; _vintageBits[slot]=bits; _vintageFilter[slot]=filter; _vintageJitter[slot]=jitter;
}


void PhoenixScreenManager::setPlaybackReverseMode(bool reverse) {
  _waveReverseMode = reverse;
  _sampleEditorPlayReverse = reverse;
}

int8_t PhoenixScreenManager::consumeToneRequest() {
  int8_t r = _toneRequest;
  _toneRequest = -1;
  return r;
}

bool PhoenixScreenManager::consumeAudioTestChanged() {
  bool r = _audioTestChanged;
  _audioTestChanged = false;
  return r;
}


void PhoenixScreenManager::browserGoRoot() {
  strncpy(_browserPath, PHX_WAV_DIR, sizeof(_browserPath));
  _browserPath[sizeof(_browserPath)-1] = 0;
  scanSampleBrowser();
}

bool PhoenixScreenManager::browserGoUp() {
  if (phxIsWavRoot(_browserPath)) return false;
  bool ok = phxParentPath(_browserPath, sizeof(_browserPath));
  scanSampleBrowser();
  return ok;
}

bool PhoenixScreenManager::browserOpenSelected() {
  if (_browserCount == 0 || _browserSel >= _browserCount) return false;
  if (!_browserEntries[_browserSel].isDir) {
    strncpy(_browserStatus, "FILE SELECTED", sizeof(_browserStatus));
    _browserStatus[sizeof(_browserStatus)-1] = 0;
    return false;
  }

  char nextPath[sizeof(_browserPath)];
  phxJoinPath(_browserPath, _browserEntries[_browserSel].name, nextPath, sizeof(nextPath));
  strncpy(_browserPath, nextPath, sizeof(_browserPath));
  _browserPath[sizeof(_browserPath)-1] = 0;
  _browserPreviewStopRequest = true;
  scanSampleBrowser();
  return true;
}


bool PhoenixScreenManager::consumeWavImportRequest(char *path, size_t pathLen, uint8_t &slot) {
  if (!_browserImportRequest) return false;
  if (path && pathLen > 0) {
    strncpy(path, _browserImportPath, pathLen);
    path[pathLen - 1] = 0;
  }
  slot = _browserImportSlot & 3;
  _browserImportRequest = false;
  _browserPreviewRequest = false;
  _browserPreviewStopRequest = false;
  return true;
}

bool PhoenixScreenManager::consumeWavPreviewRequest(char *path, size_t pathLen) {
  if (!_browserPreviewRequest) return false;
  if (path && pathLen > 0) {
    strncpy(path, _browserPreviewPath, pathLen);
    path[pathLen - 1] = 0;
  }
  _browserPreviewRequest = false;
  return true;
}

bool PhoenixScreenManager::consumeWavPreviewStopRequest() {
  bool r = _browserPreviewStopRequest;
  _browserPreviewStopRequest = false;
  return r;
}

void PhoenixScreenManager::setBrowserStatus(const char *text) {
  if (!text) text = "";
  strncpy(_browserStatus, text, sizeof(_browserStatus));
  _browserStatus[sizeof(_browserStatus)-1] = 0;
}


bool PhoenixScreenManager::readWavInfo(const char *fullPath, BrowserEntry &entry) {
  entry.wavInfoValid = false;
  entry.sampleRate = 0;
  entry.bitsPerSample = 0;
  entry.channels = 0;
  entry.dataOffset = 0;
  entry.dataLength = 0;
  entry.sampleCount = 0;
  entry.durationTenths = 0;

  if (!fullPath || !fullPath[0]) return false;
  File f = SD.open(fullPath, FILE_READ);
  if (!f) return false;

  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) { f.close(); return false; }
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { f.close(); return false; }

  bool gotFmt = false;
  bool gotData = false;
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bits = 0;
  uint32_t dataOffset = 0;
  uint32_t dataLength = 0;

  while (f.available()) {
    uint8_t ch[8];
    if (f.read(ch, 8) != 8) break;
    uint32_t chunkSize = phxReadLE32(ch + 4);
    uint32_t dataPos = f.position();

    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fmt[40];
      uint32_t toRead = chunkSize;
      if (toRead > sizeof(fmt)) toRead = sizeof(fmt);
      if (f.read(fmt, toRead) != (int)toRead) break;
      if (chunkSize > toRead) f.seek(dataPos + chunkSize + (chunkSize & 1));
      if (chunkSize >= 16) {
        audioFormat = phxReadLE16(fmt + 0);
        channels = phxReadLE16(fmt + 2);
        sampleRate = phxReadLE32(fmt + 4);
        bits = phxReadLE16(fmt + 14);
        gotFmt = true;
      }
    } else if (memcmp(ch, "data", 4) == 0) {
      dataOffset = dataPos;
      dataLength = chunkSize;
      gotData = true;
      // We have enough metadata; do not read the audio data in Phase 1.
      break;
    } else {
      f.seek(dataPos + chunkSize + (chunkSize & 1));
    }
  }
  f.close();

  if (!gotFmt || !gotData) return false;
  if (audioFormat != 1) return false; // PCM only in v0.7.13g.
  if (channels < 1 || channels > 2) return false;
  if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
  if (sampleRate < 8000 || sampleRate > 48000) return false;

  uint32_t bytesPerFrame = ((uint32_t)bits / 8U) * (uint32_t)channels;
  uint32_t frames = (bytesPerFrame > 0) ? (dataLength / bytesPerFrame) : 0;
  uint32_t tenths = (sampleRate > 0) ? ((frames * 10UL) / sampleRate) : 0;
  if (tenths > 65535UL) tenths = 65535UL;

  entry.wavInfoValid = true;
  entry.sampleRate = sampleRate;
  entry.bitsPerSample = bits;
  entry.channels = channels;
  entry.dataOffset = dataOffset;
  entry.dataLength = dataLength;
  entry.sampleCount = frames;
  entry.durationTenths = (uint16_t)tenths;
  return true;
}

void PhoenixScreenManager::scanSampleBrowser() {
  _browserCount = 0;
  _browserSel = 0;
  strncpy(_browserStatus, "SCANNING...", sizeof(_browserStatus));
  _browserStatus[sizeof(_browserStatus)-1] = 0;

  for (uint8_t i = 0; i < PHX_BROWSER_MAX; ++i) {
    _browserEntries[i].name[0] = 0;
    _browserEntries[i].isDir = false;
    _browserEntries[i].size = 0;
    _browserEntries[i].wavInfoValid = false;
    _browserEntries[i].sampleRate = 0;
    _browserEntries[i].bitsPerSample = 0;
    _browserEntries[i].channels = 0;
    _browserEntries[i].dataOffset = 0;
    _browserEntries[i].dataLength = 0;
    _browserEntries[i].sampleCount = 0;
    _browserEntries[i].durationTenths = 0;
  }

  if (!_browserPath[0]) {
    strncpy(_browserPath, PHX_WAV_DIR, sizeof(_browserPath));
    _browserPath[sizeof(_browserPath)-1] = 0;
  }

  if (!phxSdReadyForConfig()) {
    strncpy(_browserStatus, "SD NOT READY", sizeof(_browserStatus));
    _browserStatus[sizeof(_browserStatus)-1] = 0;
    return;
  }
  phxEnsureDir(PHX_ROOT_DIR);
  phxEnsureDir(PHX_WAV_DIR);

  File dir = SD.open(_browserPath);
  if (!dir || !dir.isDirectory()) {
    strncpy(_browserStatus, "DIR ERROR", sizeof(_browserStatus));
    _browserStatus[sizeof(_browserStatus)-1] = 0;
    if (dir) dir.close();
    return;
  }

  while (_browserCount < PHX_BROWSER_MAX) {
    File entry = dir.openNextFile();
    if (!entry) break;
    const char *bn = phxBaseName(entry.name());
    bool isDir = entry.isDirectory();
    if (phxBrowserShowName(bn, isDir)) {
      strncpy(_browserEntries[_browserCount].name, bn, sizeof(_browserEntries[_browserCount].name));
      _browserEntries[_browserCount].name[sizeof(_browserEntries[_browserCount].name)-1] = 0;
      _browserEntries[_browserCount].isDir = isDir;
      _browserEntries[_browserCount].size = isDir ? 0 : (uint32_t)entry.size();
      if (!isDir && phxIsWavName(bn)) {
        char fullPath[sizeof(_browserPath) + 72];
        phxJoinPath(_browserPath, bn, fullPath, sizeof(fullPath));
        readWavInfo(fullPath, _browserEntries[_browserCount]);
      }
      ++_browserCount;
    }
    entry.close();
  }
  dir.close();

  // v0.7.13f: sort RAM cache only. Folders first, then WAV files, alphabetically.
  for (uint8_t i = 0; i < _browserCount; ++i) {
    for (uint8_t j = i + 1; j < _browserCount; ++j) {
      bool swapIt = false;
      if (_browserEntries[i].isDir != _browserEntries[j].isDir) {
        swapIt = !_browserEntries[i].isDir && _browserEntries[j].isDir;
      } else if (strcasecmp(_browserEntries[i].name, _browserEntries[j].name) > 0) {
        swapIt = true;
      }
      if (swapIt) {
        BrowserEntry tmp = _browserEntries[i];
        _browserEntries[i] = _browserEntries[j];
        _browserEntries[j] = tmp;
      }
    }
  }

  if (_browserCount == 0) strncpy(_browserStatus, "EMPTY FOLDER", sizeof(_browserStatus));
  else snprintf(_browserStatus, sizeof(_browserStatus), "%u ITEM(S)", (unsigned)_browserCount);
  _browserStatus[sizeof(_browserStatus)-1] = 0;
}

void PhoenixScreenManager::moveBrowserSel(int8_t d) {
  if (_browserCount == 0) return;
  int ns = (int)_browserSel + d;
  if (ns < 0) ns = _browserCount - 1;
  if (ns >= (int)_browserCount) ns = 0;
  _browserSel = (uint8_t)ns;
  _browserPreviewStopRequest = true;
  _browserStatus[0] = 0;
}

bool PhoenixScreenManager::consumeRecordRequest() { bool r = _recordRequest; _recordRequest = false; return r; }
bool PhoenixScreenManager::consumePlayRequest()   { bool r = _playRequest;   _playRequest = false;   return r; }
bool PhoenixScreenManager::consumePlayFromCursorRequest(uint32_t &frame) { bool r = _playFromCursorRequest; frame = _playFromCursorFrame; _playFromCursorRequest = false; return r; }
bool PhoenixScreenManager::consumeReversePlayFromCursorRequest(uint32_t &frame) { bool r = _reversePlayFromCursorRequest; frame = _playFromCursorFrame; _reversePlayFromCursorRequest = false; return r; }
bool PhoenixScreenManager::consumeReversePlayRequest() { bool r = _reversePlayRequest; _reversePlayRequest = false; return r; }
bool PhoenixScreenManager::consumeStopRequest()   { bool r = _stopRequest;   _stopRequest = false;   return r; }
int8_t PhoenixScreenManager::consumeSelectSlotRequest() { int8_t r = _selectSlotRequest; _selectSlotRequest = -1; return r; }
bool PhoenixScreenManager::consumePitchChanged() { bool r = _pitchChanged; _pitchChanged = false; return r; }
bool PhoenixScreenManager::consumeEnvelopeChanged() { bool r = _envChanged; _envChanged = false; return r; }
bool PhoenixScreenManager::consumeVintageChanged() { bool r = _vintageChanged; _vintageChanged = false; return r; }
bool PhoenixScreenManager::consumeEchoChanged() { bool r = _echoChanged; _echoChanged = false; return r; }
bool PhoenixScreenManager::consumeLoopChanged() { bool r = _loopChanged; _loopChanged = false; return r; }
bool PhoenixScreenManager::consumeLoopRangeChanged() { bool r = _loopRangeChanged; _loopRangeChanged = false; return r; }
bool PhoenixScreenManager::consumeSampleRangeChanged() { bool r = _sampleRangeChanged; _sampleRangeChanged = false; return r; }
int8_t PhoenixScreenManager::consumeSampleProcessingAction(uint8_t &slot) {
  const int8_t a = _sampleProcessingAction;
  if (a < 0) return -1;
  slot = _sampleProcessingSlot & 3U;
  _sampleProcessingAction = -1;
  return a;
}

void PhoenixScreenManager::setSampleProcessingStatus(const bool trim[4], const bool dc[4], const bool norm[4]) {
  for (uint8_t i = 0; i < 4U; ++i) {
    _slotTrimEnabled[i] = trim ? trim[i] : false;
    _slotDcEnabled[i] = dc ? dc[i] : false;
    _slotNormalizeEnabled[i] = norm ? norm[i] : false;
  }
}

bool PhoenixScreenManager::consumeSampleMarkerEdit(uint8_t &slot, uint32_t &sampleStart,
                                                    uint32_t &loopStart, uint32_t &loopEnd,
                                                    uint32_t &sampleEnd) {
  if (!_sampleMarkerEditPending) return false;
  slot = _sampleMarkerEditSlot & 3U;
  sampleStart = _sampleMarkerEditSampleStart;
  loopStart = _sampleMarkerEditLoopStart;
  loopEnd = _sampleMarkerEditLoopEnd;
  sampleEnd = _sampleMarkerEditSampleEnd;
  _sampleMarkerEditPending = false;
  return true;
}
bool PhoenixScreenManager::consumeZeroCrossSnapRequest(uint8_t &slot, uint8_t &marker, uint32_t &frame) {
  if (!_zeroCrossSnapPending) return false;
  slot = _zeroCrossSnapSlot & 3U;
  marker = _zeroCrossSnapMarker & 3U;
  frame = _zeroCrossSnapFrame;
  _zeroCrossSnapPending = false;
  return true;
}
void PhoenixScreenManager::applyZeroCrossSnap(uint8_t slot, uint8_t marker, uint32_t frame) {
  slot &= 3U; marker &= 3U;
  const uint32_t frames = _slotFrames[slot];
  if (frames == 0U) return;
  uint32_t ss = _slotSampleStart[slot], ls = _slotLoopStart[slot];
  uint32_t le = _slotLoopEnd[slot], se = _slotSampleEnd[slot];
  const uint32_t cur = (marker == 0U) ? ss : (marker == 1U) ? ls : (marker == 2U) ? (le ? le - 1U : 0U) : (se ? se - 1U : 0U);
  int64_t d64 = (int64_t)frame - (int64_t)cur;
  if (d64 > 2147483647LL) d64 = 2147483647LL;
  if (d64 < -2147483647LL) d64 = -2147483647LL;
  PhoenixSampleMarkerEditResult edit = phxMoveSampleMarker(frames, ss, ls, le, se, marker, cur, (int32_t)d64);
  _editorMarker = marker;
  _editorCursor = edit.cursor;
  _slotSampleStart[slot] = edit.sampleStart;
  _slotLoopStart[slot] = edit.loopStart;
  _slotLoopEnd[slot] = edit.loopEnd;
  _slotSampleEnd[slot] = edit.sampleEnd;
  _sampleMarkerEditSlot = slot;
  _sampleMarkerEditSampleStart = edit.sampleStart;
  _sampleMarkerEditLoopStart = edit.loopStart;
  _sampleMarkerEditLoopEnd = edit.loopEnd;
  _sampleMarkerEditSampleEnd = edit.sampleEnd;
  _sampleMarkerEditPending = true;
  if (edit.sampleChanged) _sampleRangeChanged = true;
  if (edit.loopChanged) _loopRangeChanged = true;
}
bool PhoenixScreenManager::consumeSampleEditorPlayRequest(uint32_t &start, uint32_t &end, bool &reverse) {
  bool r = _sampleEditorPlayRequest;
  start = _sampleEditorPlayStart;
  end = _sampleEditorPlayEnd;
  reverse = _sampleEditorPlayReverse;
  _sampleEditorPlayRequest = false;
  return r;
}
void PhoenixScreenManager::showRecordingComplete(uint8_t slot) {
  _bankDirty = true;
  _recordCompleteSlot = slot & 3U;
  _selectedSlot = _recordCompleteSlot;
  _selectSlotRequest = (int8_t)_selectedSlot;
  _recordCompleteSaveState = -1;
  go(SCR_RECORD_COMPLETE);
}

void PhoenixScreenManager::setRecordingCompleteSaveResult(bool ok) {
  if (_screen == SCR_RECORD_COMPLETE) _recordCompleteSaveState = ok ? 1 : 0;
}

bool PhoenixScreenManager::consumeTriggerChanged() { bool r = _triggerChanged; _triggerChanged = false; return r; }
int8_t PhoenixScreenManager::consumeDiskAction() { int8_t r = _diskAction; _diskAction = -1; return r; }
int8_t PhoenixScreenManager::consumeUsbStorageAction() { int8_t r = _usbStorageAction; _usbStorageAction = -1; return r; }
void PhoenixScreenManager::setUsbStorageStatus(bool active, bool available, bool writable, bool ejected, const char *status) {
  _usbStorageActive = active;
  _usbStorageAvailable = available;
  _usbStorageWritable = writable;
  _usbStorageEjected = ejected;
  if (!active || ejected) _usbStorageExitConfirm = false;
  // Do not overwrite the explicit fallback confirmation prompt while it is open.
  if (status && !_usbStorageExitConfirm) {
    strncpy(_usbStorageStatus, status, sizeof(_usbStorageStatus));
    _usbStorageStatus[sizeof(_usbStorageStatus)-1]=0;
  }
}
void PhoenixScreenManager::finishUsbStorageExit(bool remountOk, bool mediaChanged) {
  _usbStorageActive = false;
  _usbStorageWritable = false;
  _usbStorageEjected = false;
  _usbStorageExitConfirm = false;
  strncpy(_usbStorageStatus,
          remountOk ? (mediaChanged ? "SD UPDATED - RELOAD" : "SD READY") : "SD REMOUNT FAILED",
          sizeof(_usbStorageStatus));
  _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
  if (remountOk) go(SCR_DISK, 4);
}
void PhoenixScreenManager::setDiskBankUsed(bool used) {
  _diskBankUsed = used;
}

void PhoenixScreenManager::setDiskStatus(const char *text) {
  if (!text) text = "";
  strncpy(_diskStatus, text, sizeof(_diskStatus));
  _diskStatus[sizeof(_diskStatus)-1] = 0;
}

int PhoenixScreenManager::itemCountFor(ScreenID s) const {
  switch (s) {
    case SCR_SELFTEST: return 5;
    case SCR_AUDIO_OUT: return 4;
    case SCR_MAIN: return 8;
    case SCR_SOUND: return 8;
    case SCR_PITCH: return 5;
    case SCR_ENVELOPE: return 4;
    case SCR_ECHO: return 3;
    case SCR_LOOP: return 1;
    case SCR_SAMPLE_EDITOR: return 4;
    case SCR_TRIGGER: return 3;
    case SCR_DISK: return 5;
    case SCR_SAMPLE_BROWSER: return (_browserCount > 0) ? _browserCount : 1;
    case SCR_VINTAGE: return 5;
    case SCR_MIDI_TYPE: return 8;
    case SCR_MIDI_CH: return 1;
    case SCR_MIDI_MONITOR: return 1;
    case SCR_QUATTRO: return 8;
    case SCR_KEYBOARD: return 1;
    case SCR_VOICE: return 4;
    case SCR_MIXER: return 3;
    case SCR_FILTER: return 5;
    case SCR_FILTER_ENV: return 4;
    case SCR_MULTISAMPLE: return 13;
    case SCR_SEQUENCER: return 7;
    case SCR_PATTERN_MANAGER: return 3;
    default: return 1;
  }
}

void PhoenixScreenManager::moveSel(int8_t d) {
  int n = itemCountFor(_screen);
  int ns = (int)_sel + d;
  if (ns < 0) ns = n - 1;
  if (ns >= n) ns = 0;
  _sel = (uint8_t)ns;
}

void PhoenixScreenManager::go(ScreenID s, uint8_t sel) {
  if (_screen == SCR_TRIGGER && s != SCR_TRIGGER) {
    _triggerTestActive = false;
    _triggerTestAbove = false;
    _triggerTestPrevAbove = false;
    _triggerTestTrigUntilMs = 0;
  }
  _screen = s;
  _sel = sel;
}

void PhoenixScreenManager::requestRecordingForSelectedSlot() {
  const uint8_t slot = _selectedSlot & 3U;
  _recordReturnScreen = (_screen == SCR_QUATTRO) ? SCR_QUATTRO : SCR_SOUND;
  _recordCompleteSaveState = -1;
  if (_slotRecorded[slot]) {
    _overwriteSlot = slot;
    _overwriteConfirm = true;
  } else {
    _recordRequest = true;
  }
}

void PhoenixScreenManager::enterSelection() {
  if (_screen == SCR_SELFTEST) {
    if (_sel == 0) { go(SCR_AUDIO_OUT); return; }
    if (_sel == 1) { go(SCR_AUDIO_IN); return; }
    if (_sel == 2) { go(SCR_BUTTONS); return; }
    if (_sel == 3) { _diagnosticsReturnScreen = SCR_SELFTEST; go(SCR_DIAGNOSTICS); return; }
    if (_sel == 4) { go(SCR_MAIN); return; }
  }

  if (_screen == SCR_AUDIO_OUT) {
    // v0.7.13b Audio Test Generator is handled in update().
    return;
  }

  if (_screen == SCR_AUDIO_IN || _screen == SCR_BUTTONS) {
    go(SCR_SELFTEST);
    return;
  }

  if (_screen == SCR_MAIN) {
    if (_sel == 0) { go(SCR_SOUND); return; }
    if (_sel == 1) { go(SCR_QUATTRO); return; }
    if (_sel == 2) { go(SCR_PITCH); return; }
    if (_sel == 3) { go(SCR_ECHO); _echoEditMode = false; return; }
    if (_sel == 4) { go(SCR_DISK); _diskConfirmAction = -1; _diskAction = 3; return; }
    if (_sel == 5) { go(SCR_VINTAGE); return; }
    if (_sel == 6) { go(SCR_SEQUENCER); return; }
    if (_sel == 7) { go(SCR_MIDI_TYPE); _midiEditMode = false; return; }
    return;
  }

  if (_screen == SCR_VINTAGE) { _vintageEditMode = !_vintageEditMode; return; }

  if (_screen == SCR_MIDI_TYPE) {
    if (_sel == 0) _midiEnabled = !_midiEnabled;
    else if (_sel == 1) _midiClockEnabled = !_midiClockEnabled;
    else if (_sel == 2) _midiNoteEnabled = !_midiNoteEnabled;
    else if (_sel == 3) _midiCcEnabled = !_midiCcEnabled;
    else if (_sel == 4) _midiPcEnabled = !_midiPcEnabled;
    else if (_sel == 5) {
      _midiEditMode = !_midiEditMode;
      if (!_midiEditMode) saveMidiChannelSetting();
      return;
    } else if (_sel == 6) {
      go(SCR_MIDI_MONITOR); return;
    } else if (_sel == 7) {
      _midiResetConfirm = true; return;
    }
    saveMidiControlSettings();
    return;
  }

  if (_screen == SCR_MIDI_CH) {
    _midiEditMode = !_midiEditMode;
    if (!_midiEditMode) saveSystemSettings();
    return;
  }

  if (_screen == SCR_SOUND) {
    if (_sel == 0) { // RECORD
      if (_recording || _playing) _stopRequest = true;
      else requestRecordingForSelectedSlot();
      return;
    }
    if (_sel == 1) { // REPLAY FORWARD
      _waveReverseMode = false;
      if (_playing || _recording) _stopRequest = true;
      else _playRequest = true;
      return;
    }
    if (_sel == 2) { // REPLAY BACKWARD
      _waveReverseMode = true;
      if (_playing || _recording) _stopRequest = true;
      else _reversePlayRequest = true;
      return;
    }
    if (_sel == 3) { go(SCR_WAVE); return; }
    if (_sel == 4) {
      const uint8_t sl = _selectedSlot & 3;
      _editorCursor = _slotSampleStart[sl];
      if (_slotFrames[sl] > 0 && _editorCursor >= _slotFrames[sl]) _editorCursor = 0;
      _editorMarker = 0;
      go(SCR_SAMPLE_EDITOR);
      return;
    }
    if (_sel == 5) { go(SCR_TRIGGER); _triggerEditMode = false; _triggerParam = 0; return; }
    if (_sel == 6) { go(SCR_LOOP); return; }
    if (_sel == 7) { go(SCR_MAIN); return; }
  }

  if (_screen == SCR_PITCH) {
    // Parameter editing is handled in update(const PhoenixInputState&).
    return;
  }


  if (_screen == SCR_ECHO) {
    // Echo parameter browsing/editing is handled in update(const PhoenixInputState&).
    return;
  }


  if (_screen == SCR_DISK) {
    if (_sel == 0) {
      _diskConfirmAction = -1;
      if (!_diskBankUsed) { setDiskStatus("BANK EMPTY"); return; }
      _diskAction = 1;
      return;
    }
    if (_sel == 1) {
      if (_diskBankUsed && _diskConfirmAction != 0) {
        _diskConfirmAction = 0;
        setDiskStatus("OVERWRITE? F7 YES");
        return;
      }
      _diskConfirmAction = -1;
      _diskAction = 0;
      return;
    }
    if (_sel == 2) {
      if (!_diskBankUsed) { setDiskStatus("BANK EMPTY"); return; }
      if (_diskConfirmAction != 2) {
        _diskConfirmAction = 2;
        setDiskStatus("DELETE? F7 YES");
        return;
      }
      _diskConfirmAction = -1;
      _diskAction = 2;
      return;
    }
    if (_sel == 3) { _diskConfirmAction = -1; browserGoRoot(); go(SCR_SAMPLE_BROWSER); return; }
    if (_sel == 4) { _diskConfirmAction = -1; go(SCR_USB_STORAGE); return; }
  }


  if (_screen == SCR_WAVE) {
    go(SCR_SOUND, 2);
    return;
  }



  if (_screen == SCR_QUATTRO) {
    if (_sel < 4) {
      // Slot rows are safe play/select actions.
      // If the slot is READY, F7/Enter plays it. Empty slots start recording.
      // Overwrite is deliberately moved to the separate action row below so
      // an existing sample cannot be erased by accident.
      _selectedSlot = _sel;
      _selectSlotRequest = (int8_t)_sel;
      if (_recording || _playing) _stopRequest = true;
      else if (_slotRecorded[_selectedSlot]) _playRequest = true;
      else requestRecordingForSelectedSlot();
      return;
    }
    if (_sel == 4) {
      // Action row: RECORD for empty slots, OVERWRITE for occupied slots, STOP during transport.
      if (_recording || _playing) {
        _stopRequest = true;
      } else {
        requestRecordingForSelectedSlot();
      }
      return;
    }
    if (_sel == 5) { go(SCR_KEYBOARD); return; }
    if (_sel == 6) { _msSlot=_selectedSlot&3; _msEditMode=false; go(SCR_MULTISAMPLE); return; }
    if (_sel == 7) { go(SCR_MAIN, 1); return; }
  }
}

bool PhoenixScreenManager::currentMidiLearnParameter(PhoenixParameterId &id) const {
  if (_screen == SCR_MIXER) {
    if (_mixerParam == 1) id=PHX_PAR_SLOT_VOLUME;
    else if (_mixerParam == 2) id=PHX_PAR_SLOT_PAN;
    else if (_mixerParam == 3) id=PHX_PAR_ECHO_SEND;
    else return false;
    return true;
  }
  if (_screen == SCR_FILTER) {
    if (_filterParam == 1) id=PHX_PAR_FILTER_CUTOFF;
    else if (_filterParam == 2) id=PHX_PAR_FILTER_RESONANCE;
    else return false;
    return true;
  }
  if (_screen == SCR_ENVELOPE) {
    static const PhoenixParameterId ids[4]={PHX_PAR_AMP_ATTACK,PHX_PAR_AMP_DECAY,PHX_PAR_AMP_SUSTAIN,PHX_PAR_AMP_RELEASE};
    if(_envParam<4){id=ids[_envParam];return true;} return false;
  }
  if (_screen == SCR_ECHO) {
    if (_echoParam==0) id=PHX_PAR_ECHO_TIME;
    else if (_echoParam==1) id=PHX_PAR_ECHO_FEEDBACK;
    else if (_echoParam==2) id=PHX_PAR_ECHO_MIX;
    else if (_echoParam<=6) id=PHX_PAR_ECHO_SEND;
    else return false;
    return true;
  }
  if (_screen == SCR_PITCH) {
    if (_pitchParam==0) id=PHX_PAR_TRANSPOSE;
    else if (_pitchParam==1) id=PHX_PAR_FINE_TUNE;
    else if (_pitchParam==2) id=PHX_PAR_ROOT_KEY;
    else return false;
    return true;
  }
  if (_screen == SCR_SAMPLE_EDITOR) {
    static const PhoenixParameterId ids[4] = { PHX_PAR_SAMPLE_START, PHX_PAR_LOOP_START, PHX_PAR_LOOP_END, PHX_PAR_SAMPLE_END };
    id = ids[_editorMarker & 3U];
    return true;
  }
  if (_screen == SCR_VOICE && _voiceParam==4) { id=PHX_PAR_GLIDE; return true; }
  return false;
}

bool PhoenixScreenManager::consumeMidiLearnRequest(PhoenixParameterId &id) {
  if(!_midiLearnRequest) return false; _midiLearnRequest=false; id=_midiLearnParam; return true;
}
void PhoenixScreenManager::setMidiLearnResult(uint8_t cc, bool assigned) {
  _midiLearnResultCc=cc; _midiLearnResultAssigned=assigned; _midiLearnResultUntilMs=millis()+900UL;
}
void PhoenixScreenManager::cancelMidiLearn() { _midiLearnActive=false; _midiLearnRequest=false; _midiLearnResultUntilMs=0; _midiLearnMenu=false; }

void PhoenixScreenManager::drawMidiLearnPopup(PhoenixGUI &gui) {
  const int x=18,y=10,w=92,h=44; gui.fill(x,y,w,h,false); gui.drawWindow(x,y,w,h,"MIDI LEARN");
  const PhoenixMidiParameterDef *def=phxMidiParameterById(_midiLearnParam);
  gui.text(x+6,y+11,def?def->name:"PARAMETER",false);
  if(_midiLearnResultUntilMs && (int32_t)(millis()-_midiLearnResultUntilMs)<0){
    char line[20]; snprintf(line,sizeof(line),"CC%03u %s",(unsigned)_midiLearnResultCc,_midiLearnResultAssigned?"ASSIGNED":"BLOCKED"); gui.text(x+6,y+25,line,false);
    return;
  }
  if(_midiLearnMenu){
    static const char* opts[4]={"REPLACE","CLEAR","DEFAULT","CANCEL"};
    for(uint8_t i=0;i<4;++i){ const int yy=y+19+(i&1)*8; const int xx=x+6+(i/2)*43; gui.text(xx,yy,(i==_midiLearnMenuSel)?">":" ",false); gui.text(xx+5,yy,opts[i],false); }
  } else {
    gui.text(x+6,y+25,"WAITING FOR CC...",false);
    gui.text(x+6,y+34,"F8 CANCEL",false);
  }
}

void PhoenixScreenManager::update(const PhoenixInputState &in) {
  if (_screen == SCR_BOOT && millis() - _bootStart > 1100) go(_serviceMode ? SCR_SELFTEST : SCR_MAIN);
  if (_screen == SCR_BOOT) return;

  if (_screen == SCR_USB_STORAGE) {
    // Core 2.0.16/Windows does not always report the LoEj bit when Explorer
    // ejects a removable drive. If automatic detection did not arrive, offer
    // an explicit second-step confirmation rather than leaving Phoenix locked.
    if (_usbStorageActive && _usbStorageExitConfirm) {
      if (in.f[7]) {
        _usbStorageExitConfirm = false;
        _usbStorageAction = 3; // user confirms that Windows has ejected the drive
        return;
      }
      if (in.f[8]) {
        _usbStorageExitConfirm = false;
        strncpy(_usbStorageStatus, "EJECT ON PC FIRST", sizeof(_usbStorageStatus));
        _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
        return;
      }
      return;
    }
    if (!_usbStorageActive && in.f[5]) {
      _usbStorageReadWriteSelected = !_usbStorageReadWriteSelected;
      strncpy(_usbStorageStatus,
              _usbStorageReadWriteSelected ? "EJECT REQUIRED" : "USB STORAGE READY",
              sizeof(_usbStorageStatus));
      _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
      return;
    }
    if (in.f[7] && !_usbStorageActive) {
      if (_usbStorageReadWriteSelected && _bankDirty) {
        strncpy(_usbStorageStatus, "SAVE BANK FIRST", sizeof(_usbStorageStatus));
        _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
        return;
      }
      _usbStorageAction = _usbStorageReadWriteSelected ? 2 : 0;
      return;
    }
    if (in.f[8]) {
      if (_usbStorageActive) {
        // Read-only sessions and automatically detected ejects can exit directly.
        if (!_usbStorageWritable || _usbStorageEjected) {
          _usbStorageAction = 1;
        } else {
          // Fallback for Windows hosts that stop/remove the drive without a
          // callback Phoenix can distinguish. The user must confirm with F7.
          _usbStorageExitConfirm = true;
          strncpy(_usbStorageStatus, "PC EJECTED? F7 YES", sizeof(_usbStorageStatus));
          _usbStorageStatus[sizeof(_usbStorageStatus)-1] = 0;
        }
      } else go(SCR_DISK, 4);
      return;
    }
    return;
  }

  if (_screen == SCR_DIAGNOSTICS) {
    if (in.f[8]) { go(_diagnosticsReturnScreen); return; }
    if (in.f[1] || in.encPress) {
      _diagnosticsResetRequest = true;
      _diagnosticsLastAction = 1;
      _diagnosticsStatusUntilMs = millis() + 900UL;
      return;
    }
    if (in.f[7]) {
      _diagnosticsLogRequest = true;
      _diagnosticsLastAction = 2;
      _diagnosticsStatusUntilMs = millis() + 900UL;
      return;
    }
    return;
  }

  if (_screen == SCR_MAIN && in.f[2]) {
    _diagnosticsReturnScreen = SCR_MAIN;
    go(SCR_DIAGNOSTICS);
    return;
  }

  for (uint8_t i = 1; i <= 8; ++i) if (in.f[i]) _btnSeen[i - 1] = true;
  if (in.encoderStep != 0) _encSeen = true;
  if (in.encPress || in.encLong) _encSwSeen = true;

  // Modal overwrite confirmation has priority over global help/about handling.
  // F1/ F8 = NO, F7 / Encoder press = YES.
  if (_overwriteConfirm) {
    if (in.f[1] || in.f[8]) {
      _overwriteConfirm = false;
      return;
    }
    if (in.f[7] || in.encPress) {
      _selectedSlot = _overwriteSlot;
      _selectSlotRequest = (int8_t)_selectedSlot;
      _recordRequest = true;
      _overwriteConfirm = false;
      return;
    }
    return;
  }

  // C024: direct post-record workflow. The new recording remains selected.
  if (_screen == SCR_RECORD_COMPLETE) {
    if (in.f[5]) { go(SCR_SAMPLE_EDITOR); return; }
    if (in.f[6]) {
      if (_playing) _stopRequest = true;
      else _playRequest = true;
      return;
    }
    if (in.f[7]) { _diskAction = 0; _recordCompleteSaveState = -1; return; }
    if (in.f[8]) { go(_recordReturnScreen); return; }
    return;
  }

  // C022: in both sampling screens F7 is always transport STOP/CANCEL,
  // independent of which menu row the encoder currently highlights.
  if ((_screen == SCR_SOUND || _screen == SCR_QUATTRO) && (_recording || _playing) && in.f[7]) {
    _stopRequest = true;
    return;
  }

  if (_midiLearnActive) {
    if (in.f[8]) { cancelMidiLearn(); return; }
    if (_midiLearnResultUntilMs && (int32_t)(millis()-_midiLearnResultUntilMs)>=0) { cancelMidiLearn(); return; }
    if (_midiLearnMenu) {
      int16_t d=in.encoderStep; if(in.f[1])d=-1; if(in.f[3])d=1;
      if(d<0) _midiLearnMenuSel=(uint8_t)((_midiLearnMenuSel+3U)%4U);
      if(d>0) _midiLearnMenuSel=(uint8_t)((_midiLearnMenuSel+1U)%4U);
      if(in.f[7] || in.encPress){
        if(_midiLearnMenuSel==0){ _midiLearnMenu=false; _midiLearnRequest=true; }
        else if(_midiLearnMenuSel==1){ const bool ok=phxMidiMapClear(_midiLearnParam); setMidiLearnResult(255,ok); _midiLearnMenu=false; }
        else if(_midiLearnMenuSel==2){ const bool ok=phxMidiMapResetParameter(_midiLearnParam); const PhoenixMidiParameterDef* dfn=phxMidiParameterById(_midiLearnParam); setMidiLearnResult(dfn?dfn->defaultCc:255,ok); _midiLearnMenu=false; }
        else cancelMidiLearn();
      }
      return;
    }
    return;
  }
  if (in.encLong) {
    PhoenixParameterId id;
    if (currentMidiLearnParameter(id)) {
      const PhoenixMidiParameterDef *def=phxMidiParameterById(id);
      if(def && def->learnable){
        _midiLearnParam=id; _midiLearnActive=true; _midiLearnResultUntilMs=0; _midiLearnMenuSel=0;
        const uint8_t current=phxMidiMapCc(id);
        _midiLearnMenu=(current<=127U);
        _midiLearnRequest=!_midiLearnMenu;
        return;
      }
    }
    _about = true; return;
  }
  if (_about) {
    // RTAL GUI standard: F8 always leaves information/about screens.
    if (in.f[8]) _about = false;
    return;
  }


  if (_screen == SCR_AUDIO_IN || _screen == SCR_BUTTONS) {
    if (in.f[8]) enterSelection();
    return;
  }

  if (_screen == SCR_AUDIO_OUT) {
    if (in.f[8]) {
      _audioTestOutput = false;
      _audioTestChanged = true;
      go(SCR_SELFTEST, 0);
      return;
    }

    if (in.f[7]) {
      _audioTestOutput = !_audioTestOutput;
      _audioTestChanged = true;
      return;
    }

    if (in.encPress) {
      _audioTestEditMode = !_audioTestEditMode;
      return;
    }

    int16_t delta = 0;
    if (in.encoderStep != 0) delta = in.encoderStep;
    if (in.f[1]) delta = -1;
    if (in.f[3]) delta = 1;

    if (delta != 0) {
      if (!_audioTestEditMode) {
        int p = (int)_audioTestParam + encSign(delta);
        if (p < 0) p = 3;
        if (p > 3) p = 0;
        _audioTestParam = (uint8_t)p;
        return;
      }

      if (_audioTestParam == 0) {
        int w = (int)_audioTestWaveform + encSign(delta);
        if (w < 0) w = 4;
        if (w > 4) w = 0;
        if (w != (int)_audioTestWaveform) { _audioTestWaveform = (uint8_t)w; _audioTestChanged = true; }
      } else if (_audioTestParam == 1) {
        int v = (int)_audioTestFreqHz + audioFreqStepFromEncoder(delta);
        if (v < 20) v = 20;
        if (v > 20000) v = 20000;
        if (v != (int)_audioTestFreqHz) { _audioTestFreqHz = (uint16_t)v; _audioTestChanged = true; }
      } else if (_audioTestParam == 2) {
        int v = (int)_audioTestLevelDb + audioLevelStepFromEncoder(delta);
        if (v < -60) v = -60;
        if (v > -3) v = -3;
        if (v != (int)_audioTestLevelDb) { _audioTestLevelDb = (int8_t)v; _audioTestChanged = true; }
      } else {
        bool next = _audioTestOutput;
        if (delta != 0) next = !next;
        if (next != _audioTestOutput) { _audioTestOutput = next; _audioTestChanged = true; }
      }
      return;
    }
    return;
  }



  if (_screen == SCR_SONG_EDITOR) {
    if (in.f[8]) { _songSaveRequest = true; go(SCR_PATTERN_MANAGER); return; }
    if (in.f[1]) { _songEditPos = (uint8_t)((_songEditPos + 15U) & 15U); return; }
    if (in.f[2]) { _songEditPos = (uint8_t)((_songEditPos + 1U) & 15U); return; }
    if (in.f[3]) { _songEditField = (uint8_t)((_songEditField + 1U) % 5U); return; }
    if (in.f[4]) { _songLoopMode = (uint8_t)((_songLoopMode + 1U) % 3U); _songDataChanged = true; return; }
    if (in.f[7]) { if (_songPlaying) _songStopRequest=true; else _songPlayRequest=true; return; }

    int16_t delta=in.encoderStep; if(in.f[5])delta=-1; if(in.f[6])delta=1;
    const int8_t d=delta>0?1:(delta<0?-1:0);
    if(d){
      const uint8_t pos=_songEditPos&15U;
      if(_songEditField==0){
        int v=_songEnd[pos]?4:(int)_songPattern[pos]; v=constrain(v+d,0,4);
        if(v==4){_songEnd[pos]=true;}else{_songEnd[pos]=false;_songPattern[pos]=(uint8_t)v;}
      }else if(_songEditField==1&&!_songEnd[pos]){
        _songRepeats[pos]=(uint8_t)constrain((int)_songRepeats[pos]+d,1,16);
      }else if(_songEditField==2){_songAction=(uint8_t)((_songAction+(d>0?1:1))&1U);
      }else if(_songEditField==3){_songLoopStart=(uint8_t)constrain((int)_songLoopStart+d,0,15);if(_songLoopEnd<_songLoopStart)_songLoopEnd=_songLoopStart;
      }else if(_songEditField==4){_songLoopEnd=(uint8_t)constrain((int)_songLoopEnd+d,(int)_songLoopStart,15);}
      _songDataChanged=true;return;
    }
    if(in.encPress){
      if(_songEditField==2){
        const uint8_t pos=_songEditPos&15U;
        if(_songAction==0){for(int i=15;i>(int)pos;--i){_songPattern[i]=_songPattern[i-1];_songRepeats[i]=_songRepeats[i-1];_songEnd[i]=_songEnd[i-1];}_songPattern[pos]=0;_songRepeats[pos]=1;_songEnd[pos]=false;}
        else{for(uint8_t i=pos;i<15;++i){_songPattern[i]=_songPattern[i+1];_songRepeats[i]=_songRepeats[i+1];_songEnd[i]=_songEnd[i+1];}_songPattern[15]=0;_songRepeats[15]=1;_songEnd[15]=true;if(_songEnd[0]){_songEnd[0]=false;_songEnd[1]=true;}}
        _songDataChanged=true;
      }else _songEditField=(uint8_t)((_songEditField+1U)%5U);
      return;
    }
    return;
  }

  if (_screen == SCR_SEQUENCER) {
    if (in.f[8]) { go(SCR_MAIN, 6); return; }
    if (in.f[7]) {
      if (_seqExternalClock) {
        // EXT mode is started only by MIDI FA. F7 is a local STOP/PANIC request.
        _seqRun = false;
        for (uint8_t t = 0; t < 4; ++t) _seqPlayhead[t] = 0;
      } else {
        _seqRun = !_seqRun;
        if (!_seqRun) for (uint8_t t = 0; t < 4; ++t) _seqPlayhead[t] = 0;
      }
      return;
    }
    // v0.7.40: F1..F4 double-click selects tracks 1..4 directly.
    // First clicks are deferred briefly so F3/F4 do not execute their normal
    // actions before a possible second click is known.
    const uint32_t clickNow = millis();
    if (_seqPendingClick && (uint32_t)(clickNow - _seqPendingClickMs) > 285UL) {
      const uint8_t b = _seqPendingClick; _seqPendingClick = 0;
      if (b == 1) _seqTrack = (_seqTrack + 3) & 3;
      else if (b == 2) _seqTrack = (_seqTrack + 1) & 3;
      else if (b == 3) _seqParam = (uint8_t)((_seqParam + 1) % 6);
      else if (b == 4) { _seqRun=false; for(uint8_t t=0;t<4;++t)_seqPlayhead[t]=0; _patternAction=0; _patternTarget=_seqPattern; go(SCR_PATTERN_MANAGER,0); return; }
    }
    for (uint8_t b=1; b<=4; ++b) if (in.f[b]) {
      if (_seqPendingClick == b && (uint32_t)(clickNow - _seqPendingClickMs) <= 285UL) {
        _seqPendingClick = 0; selectSequencerTrack((uint8_t)(b-1)); return;
      }
      if (_seqPendingClick) {
        const uint8_t oldb=_seqPendingClick; _seqPendingClick=0;
        if(oldb==1)_seqTrack=(_seqTrack+3)&3; else if(oldb==2)_seqTrack=(_seqTrack+1)&3; else if(oldb==3)_seqParam=(uint8_t)((_seqParam+1)%6);
      }
      _seqPendingClick=b; _seqPendingClickMs=clickNow; return;
    }

    uint8_t tr = _seqTrack & 3;
    uint8_t st = _seqStep & 15;

    if (in.encPress) {
      if (_seqParam == 0) {
        _seqOn[_seqPattern][tr][st] = !_seqOn[_seqPattern][tr][st];
        if (_seqOn[_seqPattern][tr][st]) {
          _seqPreviewTrack = tr;
          _seqPreviewNote = _seqNote[_seqPattern][tr][st];
          _seqPreviewVelocity = _seqVelocity[_seqPattern][tr][st];
          _seqPreviewRequest = true;
        }
      } else if (_seqParam == 5) {
        _seqExternalClock=!_seqExternalClock; _seqRun=false; for(uint8_t t=0;t<4;++t)_seqPlayhead[t]=0;
      } else {
        _seqParam = (uint8_t)((_seqParam + 1) % 6);
      }
      return;
    }

    int16_t delta = in.encoderStep;
    if (in.f[5]) delta = -1;
    if (in.f[6]) delta = 1;
    if (delta != 0) {
      const int16_t d = encSign(delta);
      const uint8_t accel = encAbs16(delta);
      if (_seqParam == 0) {
        const uint8_t len = _seqLength[_seqPattern][tr];
        _seqStep = (uint8_t)((_seqStep + (d > 0 ? 1 : (len - 1))) % len);
      } else if (_seqParam == 1) {
        const int nstep = (accel >= 5) ? 12 : 1;
        _seqNote[_seqPattern][tr][st] = (uint8_t)constrain((int)_seqNote[_seqPattern][tr][st] + d * nstep, 0, 127);
      } else if (_seqParam == 2) {
        const int vstep = (accel >= 5) ? 8 : ((accel >= 2) ? 4 : 1);
        _seqVelocity[_seqPattern][tr][st] = (uint8_t)constrain((int)_seqVelocity[_seqPattern][tr][st] + d * vstep, 1, 127);
      } else if (_seqParam == 3) {
        const int gstep = (accel >= 5) ? 10 : ((accel >= 2) ? 5 : 1);
        _seqGate[_seqPattern][tr][st] = (uint8_t)constrain((int)_seqGate[_seqPattern][tr][st] + d * gstep, 10, 100);
      } else if (_seqParam == 4) {
        _seqLength[_seqPattern][tr] = (uint8_t)constrain((int)_seqLength[_seqPattern][tr] + d, 1, 16);
        if (_seqStep >= _seqLength[_seqPattern][tr]) _seqStep = (uint8_t)(_seqLength[_seqPattern][tr] - 1);
        if (_seqPlayhead[tr] >= _seqLength[_seqPattern][tr]) _seqPlayhead[tr] = 0;
      } else if (!_seqExternalClock) {
        const int bstep = (accel >= 5) ? 10 : ((accel >= 2) ? 5 : 1);
        _seqBpm = (uint16_t)constrain((int)_seqBpm + d * bstep, 40, 240);
      }
      return;
    }
    return;
  }

  if (_screen == SCR_PATTERN_MANAGER) {
    if(in.f[8]){go(SCR_SEQUENCER);return;}
    if(in.f[4]){go(SCR_SONG_EDITOR);return;}
    if(in.f[3]){_patternAction=(uint8_t)((_patternAction+1)%3);return;}
    int16_t d=in.encoderStep;if(in.f[1]||in.f[5])d=-1;if(in.f[2]||in.f[6])d=1;if(d)_patternTarget=(uint8_t)((_patternTarget+(d>0?1:3))&3);
    if(in.f[7]||in.encPress){
      const uint8_t src=_seqPattern&3,dst=_patternTarget&3;
      if(_patternAction==0){_seqPattern=dst;_seqStep=0;for(uint8_t t=0;t<4;++t)_seqPlayhead[t]=0;go(SCR_SEQUENCER);}
      else if(_patternAction==1&&src!=dst){for(uint8_t t=0;t<4;++t){_seqLength[dst][t]=_seqLength[src][t];for(uint8_t st=0;st<16;++st){_seqOn[dst][t][st]=_seqOn[src][t][st];_seqNote[dst][t][st]=_seqNote[src][t][st];_seqVelocity[dst][t][st]=_seqVelocity[src][t][st];_seqGate[dst][t][st]=_seqGate[src][t][st];}}}
      else if(_patternAction==2){for(uint8_t t=0;t<4;++t){_seqLength[dst][t]=16;for(uint8_t st=0;st<16;++st){_seqOn[dst][t][st]=false;_seqNote[dst][t][st]=(uint8_t)(60+t*2);_seqVelocity[dst][t][st]=100;_seqGate[dst][t][st]=50;}}}
      return;
    }
    return;
  }

  if (_screen == SCR_QUATTRO) {
    if (in.f[8]) {
      // v0.7.13c FIX7: F8 behaves like the EXIT row and returns to the
      // main menu with the cursor still on QUATTRO SAMPLING.
      go(SCR_MAIN, 1);
      return;
    }
  }


  if (_screen == SCR_LOOP) {
    if (in.f[8]) { go(SCR_SAMPLE_EDITOR); return; }
    if (in.f[5]) { _loopParam = (uint8_t)((_loopParam + 1U) & 1U); return; }

    int16_t d = in.encoderStep;
    if (in.f[1]) d = -1;
    if (in.f[3] || in.encPress) d = 1;
    if (d != 0) {
      const uint8_t sl = _selectedSlot & 3U;
      if (_loopParam == 0U) {
        uint8_t mode = (_slotLoopMode[sl] <= 2U) ? _slotLoopMode[sl] : 0U;
        mode = (uint8_t)((mode + (d > 0 ? 1U : 2U)) % 3U);
        _slotLoopMode[sl] = mode;
        _slotLoop[sl] = (mode != 0U);
      } else {
        static const uint8_t values[6] = {0,2,4,8,16,32};
        uint8_t idx=0; for(uint8_t i=0;i<6;++i) if(values[i]==_slotLoopXfadeMs[sl]) { idx=i; break; }
        idx = (uint8_t)((idx + (d > 0 ? 1U : 5U)) % 6U);
        _slotLoopXfadeMs[sl] = values[idx];
      }
      _loopChanged = true;
      return;
    }
    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true; else _playRequest = true;
      return;
    }
    return;
  }

  if (_screen == SCR_TRIGGER) {
    if (in.f[8]) {
      saveSystemSettings();
      _triggerEditMode = false;
      _triggerTestActive = false;
      _triggerTestAbove = false;
      _triggerTestPrevAbove = false;
      _triggerTestTrigUntilMs = 0;
      go(SCR_SOUND, 5);
      return;
    }

    if (in.encPress) {
      _triggerEditMode = !_triggerEditMode;
      return;
    }

    if (in.f[7]) {
      if (_playing || _recording) {
        _stopRequest = true;
      } else {
        // C021: F7 is a read-only threshold test. No record request is issued,
        // so the active slot, PCM data, markers and processing state are safe.
        _triggerTestActive = !_triggerTestActive;
        _triggerTestAbove = false;
        _triggerTestPrevAbove = false;
        _triggerTestTrigUntilMs = 0;
      }
      return;
    }

    int16_t delta = 0;
    if (in.encoderStep != 0) delta = in.encoderStep;
    if (in.f[1]) delta = -1;
    if (in.f[3]) delta = 1;

    if (delta != 0) {
      if (!_triggerEditMode) {
        int p = (int)_triggerParam + encSign(delta);
        if (p < 0) p = 1;
        if (p > 1) p = 0;
        _triggerParam = (uint8_t)p;
        return;
      }

      if (_triggerParam == 0) {
        bool next = _triggerAuto;
        if (delta != 0) next = !next;
        if (next != _triggerAuto) { _triggerAuto = next; _triggerChanged = true; saveTriggerSettings(); }
      } else if (_triggerParam == 1) {
        int v = (int)_triggerLevel + triggerLevelStepFromEncoder(delta);
        if (v < 1) v = 1;
        if (v > 8) v = 8;
        if (v != (int)_triggerLevel) { _triggerLevel = (uint8_t)v; _triggerChanged = true; saveTriggerSettings(); }
      }
      return;
    }
    return;
  }

  if (_screen == SCR_DISK) {
    if (in.f[8]) { _diskConfirmAction = -1; go(SCR_MAIN, 4); return; }
    if (in.f[5] || in.f[6]) {
      int bank = (int)_diskBank + (in.f[6] ? 1 : -1);
      if (bank < 1) bank = 99;
      if (bank > 99) bank = 1;
      _diskBank = (uint8_t)bank;
      _diskBankUsed = false;
      _diskConfirmAction = -1;
      _diskAction = 3;
      char msg[24];
      snprintf(msg, sizeof(msg), "BANK %02u CHECK", (unsigned)_diskBank);
      setDiskStatus(msg);
      return;
    }
    // F7 or Encoder push executes the selected disk utility via enterSelection().
  }

  if (_screen == SCR_SAMPLE_BROWSER) {
    if (_browserImportState == 1) {
      if (in.f[8]) { _browserImportState = 0; strncpy(_browserStatus, "IMPORT CANCEL", sizeof(_browserStatus)); _browserStatus[sizeof(_browserStatus)-1] = 0; return; }
      int16_t d = 0;
      if (in.encoderStep != 0) d = encSign(in.encoderStep);
      if (in.f[1]) d = -1;
      if (in.f[3]) d = 1;
      if (d != 0) { int v = (int)_browserImportSlot + d; if (v < 0) v = 3; if (v > 3) v = 0; _browserImportSlot = (uint8_t)v; return; }
      if (in.f[7] || in.encPress) {
        if (_slotRecorded[_browserImportSlot & 3]) { _browserImportState = 2; return; }
        _browserImportRequest = true;
        _browserImportState = 0;
        snprintf(_browserStatus, sizeof(_browserStatus), "IMPORT S%u", (unsigned)(_browserImportSlot + 1));
        return;
      }
      return;
    }
    if (_browserImportState == 2) {
      if (in.f[1] || in.f[8]) { _browserImportState = 1; return; }
      if (in.f[7] || in.encPress) {
        _browserImportRequest = true;
        _browserImportState = 0;
        snprintf(_browserStatus, sizeof(_browserStatus), "IMPORT S%u", (unsigned)(_browserImportSlot + 1));
        return;
      }
      return;
    }

    if (in.f[8]) {
      _browserPreviewStopRequest = true;
      if (!browserGoUp()) { if(_keygroupImportMode||_autoMapBrowserMode){_keygroupImportMode=false;_autoMapBrowserMode=false;go(SCR_MULTISAMPLE);} else go(SCR_DISK,3); }
      return;
    }
    if (in.f[1]) { _browserPreviewStopRequest = true; browserGoRoot(); return; }
    if (in.f[3] || in.encoderStep > 0) { moveBrowserSel(1); return; }
    if (in.encoderStep < 0) { moveBrowserSel(-1); return; }
    if (in.f[5] && _autoMapBrowserMode) {
      strncpy(_autoMapFolder,_browserPath,sizeof(_autoMapFolder)); _autoMapFolder[sizeof(_autoMapFolder)-1]=0;
      _autoMapSlot=_msSlot&3; _autoMapRequest=true; strncpy(_browserStatus,"AUTO MAPPING...",sizeof(_browserStatus)); _browserStatus[sizeof(_browserStatus)-1]=0; return;
    }
    if (in.f[6]) {
      if (_multisampleAutoMapMode) {
        strncpy(_multisampleAutoMapFolder,_browserPath,sizeof(_multisampleAutoMapFolder));
        _multisampleAutoMapFolder[sizeof(_multisampleAutoMapFolder)-1]=0;
        _multisampleAutoMapSlot=_msSlot&3;
        _multisampleAutoMapRequest=true;
        _multisampleAutoMapMode=false;
        strncpy(_browserStatus,"AUTO MAP...",sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1]=0;
        go(SCR_MULTISAMPLE);
        return;
      }
      if (_keygroupImportMode) {
        if (_browserCount == 0 || _browserSel >= _browserCount || _browserEntries[_browserSel].isDir || !_browserEntries[_browserSel].wavInfoValid) { strncpy(_browserStatus,"SELECT WAV",sizeof(_browserStatus)); _browserStatus[sizeof(_browserStatus)-1]=0; }
        else { phxJoinPath(_browserPath,_browserEntries[_browserSel].name,_keygroupImportPath,sizeof(_keygroupImportPath)); _keygroupTargetSlot=_msSlot&3; _keygroupTargetGroup=_msGroup&15; _keygroupTargetLayer=_msLayer%3; _keygroupTargetVariant=_msRrVariant&3; _keygroupImportRequest=true; _keygroupImportMode=false; go(SCR_MULTISAMPLE); }
        return;
      }
      if (_browserCount == 0 || _browserSel >= _browserCount || _browserEntries[_browserSel].isDir) {
        strncpy(_browserStatus, "SELECT WAV", sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1] = 0;
      } else if (!_browserEntries[_browserSel].wavInfoValid) {
        strncpy(_browserStatus, "UNSUPPORTED", sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1] = 0;
      } else {
        phxJoinPath(_browserPath, _browserEntries[_browserSel].name, _browserImportPath, sizeof(_browserImportPath));
        _browserImportSlot = _selectedSlot & 3;
        _browserImportState = 1;
      }
      return;
    }
    if (in.f[7] || in.encPress) {
      if (_browserCount == 0) {
        strncpy(_browserStatus, "NO FILE", sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1] = 0;
      } else if (_browserEntries[_browserSel].isDir) {
        browserOpenSelected();
      } else if (!_browserEntries[_browserSel].wavInfoValid) {
        strncpy(_browserStatus, "UNSUPPORTED", sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1] = 0;
      } else {
        phxJoinPath(_browserPath, _browserEntries[_browserSel].name, _browserPreviewPath, sizeof(_browserPreviewPath));
        _browserPreviewRequest = true;
        strncpy(_browserStatus, "PREVIEW...", sizeof(_browserStatus));
        _browserStatus[sizeof(_browserStatus)-1] = 0;
      }
      return;
    }
    return;
  }

  if (_screen == SCR_VINTAGE) {
    if (in.f[8]) { _vintageEditMode = false; go(SCR_MAIN, 5); return; }
    if (in.f[7]) { if (_playing || _recording) _stopRequest = true; else _playRequest = true; return; }
    if (in.encPress) { _vintageEditMode = !_vintageEditMode; return; }
    int16_t d = in.encoderStep;
    if (in.f[1]) d = -1;
    if (in.f[3]) d = 1;
    if (d != 0) {
      const int step = (d > 0) ? 1 : -1;
      if (!_vintageEditMode) {
        int p = (int)_vintageParam + step; if (p < 0) p = 4; if (p > 4) p = 0; _vintageParam = (uint8_t)p;
      } else {
        const uint8_t sl = _selectedSlot & 3;
        if (_vintageParam == 0) {
          int v=(int)_vintagePreset[sl]+step; if(v<0)v=7; if(v>7)v=0; _vintagePreset[sl]=(uint8_t)v;
          static const uint8_t pr[8][4]={{0,0,0,0},{4,4,1,8},{3,3,1,4},{5,4,2,12},{6,5,2,18},{7,5,3,28},{8,6,3,45},{2,2,1,3}};
          if (_vintagePreset[sl] < 7) { _vintageRate[sl]=pr[_vintagePreset[sl]][0]; _vintageBits[sl]=pr[_vintagePreset[sl]][1]; _vintageFilter[sl]=pr[_vintagePreset[sl]][2]; _vintageJitter[sl]=pr[_vintagePreset[sl]][3]; }
        } else if (_vintageParam == 1) { int v=(int)_vintageRate[sl]+step; if(v<0)v=8; if(v>8)v=0; _vintageRate[sl]=(uint8_t)v; _vintagePreset[sl]=7; }
        else if (_vintageParam == 2) { int v=(int)_vintageBits[sl]+step; if(v<0)v=6; if(v>6)v=0; _vintageBits[sl]=(uint8_t)v; _vintagePreset[sl]=7; }
        else if (_vintageParam == 3) { int v=(int)_vintageFilter[sl]+step; if(v<0)v=3; if(v>3)v=0; _vintageFilter[sl]=(uint8_t)v; _vintagePreset[sl]=7; }
        else { int v=(int)_vintageJitter[sl]+step*2; if(v<0)v=0; if(v>100)v=100; _vintageJitter[sl]=(uint8_t)v; _vintagePreset[sl]=7; }
        _vintageChanged = true;
      }
      return;
    }
    return;
  }

  if (_screen == SCR_MIDI_MONITOR) {
    if (in.f[8]) { go(SCR_MIDI_TYPE,6); return; }
    // v0.7.41d: F7 toggles MIDI Clock events in the monitor. The setting is
    // stored in NVS and defaults to HIDE for continuously clocked setups.
    if (in.f[7] || in.encPress) { phxMidiMonitorToggleClockVisible(); return; }
    return;
  }

  if (_screen == SCR_MIDI_TYPE) {
    if (_midiResetConfirm) {
      if (in.f[8]) { _midiResetConfirm=false; return; }
      if (in.f[7] || in.encPress) { phxMidiMapResetDefaults(); _midiResetConfirm=false; return; }
      return;
    }
    if (in.f[8]) {
      saveMidiControlSettings();
      saveMidiChannelSetting();
      _midiEditMode = false;
      go(SCR_MAIN, 7);
      return;
    }

    // MIDI CHANNEL is edited in place; all other rows remain simple toggles.
    if (_midiEditMode && _sel == 5) {
      if (in.f[7] || in.encPress) {
        _midiEditMode = false;
        saveMidiChannelSetting();
        return;
      }
      int16_t d = in.encoderStep;
      if (in.f[1]) d = -1;
      if (in.f[3]) d = 1;
      if (d != 0) {
        int step = (d > 0) ? d : -d;
        if (step < 1) step = 1;
        int ch = (int)_midiChannel + ((d > 0) ? step : -step);
        while (ch < 1) ch += 16;
        while (ch > 16) ch -= 16;
        _midiChannel = (uint8_t)ch;
        saveMidiChannelSetting();
      }
      return;
    }

    if (in.f[1] || in.encoderStep < 0) { moveSel(-1); return; }
    if (in.f[3] || in.encoderStep > 0) { moveSel(1); return; }
    if (in.f[7] || in.encPress) { enterSelection(); return; }
    return;
  }

  if (_screen == SCR_MIDI_CH) {
    if (in.f[8]) { saveSystemSettings(); _midiEditMode = false; go(SCR_MAIN, 7); return; }
    if (in.f[7] || in.encPress) { _midiEditMode = !_midiEditMode; return; }
    int16_t d = in.encoderStep;
    if (in.f[1]) d = -1;
    if (in.f[3]) d = 1;
    if (d != 0) {
      // v0.7.13c FIX8A: Keep MIDI CHANNEL consistent with other editors.
      // Encoder/F1/F3 change the value only after PUSH/F7 has entered EDIT mode.
      if (!_midiEditMode) return;
      int step = (d > 0) ? d : -d;
      if (step < 1) step = 1;
      int ch = (int)_midiChannel + ((d > 0) ? step : -step);
      while (ch < 1) ch += 16;
      while (ch > 16) ch -= 16;
      _midiChannel = (uint8_t)ch;
      saveMidiChannelSetting();
      return;
    }
  }

  if (_screen == SCR_PITCH) {
    if (in.f[6]) { _pitchEditMode = false; _envEditMode = false; go(SCR_ENVELOPE); return; }
    if (in.f[8]) { _pitchEditMode = false; go(SCR_MAIN, 2); return; }

    // v0.6.8 Instrument Editor Phase 1:
    // Browse mode: encoder selects SEMITONE / FINE / ROOT NOTE.
    // Edit mode: encoder changes selected parameter. Encoder push toggles mode.
    // F1/F3 follow the current mode as shortcuts. F2 resets current slot.
    if (in.encPress) {
      _pitchEditMode = !_pitchEditMode;
      return;
    }

    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true;
      else _playRequest = true;
      return;
    }

    if (in.f[2]) {
      const uint8_t sl = _selectedSlot & 3;
      _pitchSemitone[sl] = 0;
      _pitchFineCent[sl] = 0;
      _pitchRootNote[sl] = 60;
      _keyboardOctave[sl] = 0;
      _pitchTrack[sl] = true;
      _pitchBendRange[sl] = 2;
      _pitchChanged = true;
      return;
    }

    int16_t delta = 0;
    if (in.encoderStep != 0) delta = in.encoderStep;
    if (in.f[1]) delta = -1;
    if (in.f[3]) delta = 1;
    if (delta != 0) {
      const uint8_t sl = _selectedSlot & 3;
      if (!_pitchEditMode) {
        const int16_t dsel = encSign(delta);
        int p = (int)_pitchParam + dsel;
        if (p < 0) p = 5;
        if (p > 5) p = 0;
        _pitchParam = (uint8_t)p;
        return;
      }

      if (_pitchParam == 0) {
        const int16_t step = (in.encoderStep != 0) ? semitoneStepFromEncoder(delta) : delta;
        int v = (int)_pitchSemitone[sl] + step;
        if (v < -24) v = -24;
        if (v > 24) v = 24;
        if (v != _pitchSemitone[sl]) { _pitchSemitone[sl] = (int8_t)v; _pitchChanged = true; }
      } else if (_pitchParam == 1) {
        const int16_t step = (in.encoderStep != 0) ? fineStepFromEncoder(delta) : delta;
        int v = (int)_pitchFineCent[sl] + step;
        if (v < -100) v = -100;
        if (v > 100) v = 100;
        if (v != _pitchFineCent[sl]) { _pitchFineCent[sl] = (int16_t)v; _pitchChanged = true; }
      } else if (_pitchParam == 2) {
        const int16_t step = (in.encoderStep != 0) ? rootStepFromEncoder(delta) : delta;
        int v = (int)_pitchRootNote[sl] + step;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        if (v != _pitchRootNote[sl]) { _pitchRootNote[sl] = (uint8_t)v; _pitchChanged = true; }
      } else if (_pitchParam == 3) {
        const int16_t step = encSign(delta);
        int v = (int)_keyboardOctave[sl] + step;
        if (v < -2) v = -2;
        if (v > 2) v = 2;
        if (v != _keyboardOctave[sl]) { _keyboardOctave[sl] = (int8_t)v; _pitchChanged = true; }
      } else if (_pitchParam == 4) {
        bool next = _pitchTrack[sl];
        if (delta != 0) next = !next;
        if (next != _pitchTrack[sl]) { _pitchTrack[sl] = next; _pitchChanged = true; }
      } else {
        static const uint8_t ranges[7] = {1,2,3,5,7,12,24};
        int idx=0; for (int i=0;i<7;++i) if (ranges[i]==_pitchBendRange[sl]) idx=i;
        idx += encSign(delta); if (idx<0) idx=6; if (idx>6) idx=0;
        _pitchBendRange[sl]=ranges[idx]; _pitchChanged=true;
      }
      return;
    }
    return;
  }

  if (_screen == SCR_ENVELOPE) {
    if (in.f[8]) { _envEditMode = false; go(SCR_PITCH); return; }
    if (in.encPress) { _envEditMode = !_envEditMode; return; }
    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true;
      else _playRequest = true;
      return;
    }
    if (in.f[2]) {
      const uint8_t sl = _selectedSlot & 3;
      _envAttackMs[sl] = 5; _envDecayMs[sl] = 80; _envSustainPct[sl] = 100; _envReleaseMs[sl] = 80;
      _envChanged = true;
      return;
    }
    int16_t delta = 0;
    if (in.encoderStep != 0) delta = in.encoderStep;
    if (in.f[1]) delta = -1;
    if (in.f[3]) delta = 1;
    if (delta != 0) {
      const uint8_t sl = _selectedSlot & 3;
      if (!_envEditMode) {
        int p = (int)_envParam + encSign(delta);
        if (p < 0) p = 3; if (p > 3) p = 0;
        _envParam = (uint8_t)p;
        return;
      }
      int step = (in.encoderStep != 0) ? abs((int)delta) : 1;
      int sign = (delta > 0) ? 1 : -1;
      if (_envParam == 0) { int v = (int)_envAttackMs[sl] + sign * step * 5; v = constrain(v, 0, 2000); if (v != _envAttackMs[sl]) { _envAttackMs[sl] = v; _envChanged = true; } }
      else if (_envParam == 1) { int v = (int)_envDecayMs[sl] + sign * step * 10; v = constrain(v, 0, 5000); if (v != _envDecayMs[sl]) { _envDecayMs[sl] = v; _envChanged = true; } }
      else if (_envParam == 2) { int v = (int)_envSustainPct[sl] + sign * step; v = constrain(v, 0, 100); if (v != _envSustainPct[sl]) { _envSustainPct[sl] = v; _envChanged = true; } }
      else { int v = (int)_envReleaseMs[sl] + sign * step * 10; v = constrain(v, 0, 5000); if (v != _envReleaseMs[sl]) { _envReleaseMs[sl] = v; _envChanged = true; } }
      return;
    }
    return;
  }

  if (_screen == SCR_ECHO) {
    if (in.f[8]) { _echoEditMode = false; go(SCR_MAIN, 3); return; }
    if (in.f[2]) { _echoDelayMs = 250; _echoFeedback = 35; _echoMix = 20; for (uint8_t i=0;i<4;++i) _echoSend[i]=0; _echoChanged = true; return; }

    // v0.6.6: Encoder-based parameter workflow.
    // Browse mode: encoder selects DELAY / FEEDBACK / MIX / SEND S1..S4.
    // Edit mode: encoder changes the selected value. Encoder push toggles mode.
    // F1/F3 follow the current mode as shortcuts.
    if (in.encPress) {
      _echoEditMode = !_echoEditMode;
      return;
    }

    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true;
      else _playRequest = true;
      return;
    }

    int16_t delta = 0;
    if (in.encoderStep != 0) delta = in.encoderStep;
    if (in.f[1]) delta = -1;
    if (in.f[3]) delta = 1;
    if (delta != 0) {
      if (!_echoEditMode) {
        const int16_t dsel = encSign(delta);
        int p = (int)_echoParam + dsel;
        // v0.7.18c: three global parameters plus four per-slot sends.
        if (p < 0) p = 6;
        if (p > 6) p = 0;
        _echoParam = (uint8_t)p;
        return;
      }

      if (_echoParam == 0) {
        const int16_t step = (in.encoderStep != 0) ? delayStepMsFromEncoder(delta) : (delta * 10);
        int v = (int)_echoDelayMs + step;
        if (v < 50) v = 50;
        if (v > 1000) v = 1000;
        if (v != (int)_echoDelayMs) { _echoDelayMs = (uint16_t)v; _echoChanged = true; }
      } else if (_echoParam == 1) {
        const int16_t step = (in.encoderStep != 0) ? percentStepFromEncoder(delta) : delta;
        int v = (int)_echoFeedback + step;
        if (v < 0) v = 0;
        if (v > 90) v = 90;
        if (v != (int)_echoFeedback) { _echoFeedback = (uint8_t)v; _echoChanged = true; }
      } else if (_echoParam == 2) {
        const int16_t step = (in.encoderStep != 0) ? percentStepFromEncoder(delta) : delta;
        int v = (int)_echoMix + step;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != (int)_echoMix) { _echoMix = (uint8_t)v; _echoChanged = true; }
      } else {
        const uint8_t sl = _echoParam - 3;
        const int16_t step = (in.encoderStep != 0) ? percentStepFromEncoder(delta) : delta;
        int v = (int)_echoSend[sl] + step;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != (int)_echoSend[sl]) { _echoSend[sl] = (uint8_t)v; _echoChanged = true; }
      }
      return;
    }

    return;
  }

  if (_screen == SCR_SAMPLE_EDITOR) {
    const uint8_t sl = _selectedSlot & 3;
    const uint32_t frames = _slotFrames[sl];
    if (in.f[8]) { go(SCR_SOUND, 4); return; }

    if (frames == 0) {
      if (in.f[5]) _editorMarker = (uint8_t)((_editorMarker + 1U) & 3U);
      return;
    }

    uint32_t sampleStart = _slotSampleStart[sl];
    uint32_t sampleEnd = _slotSampleEnd[sl];
    uint32_t loopStart = _slotLoopStart[sl];
    uint32_t loopEnd = _slotLoopEnd[sl];
    if (sampleEnd == 0U || sampleEnd > frames) sampleEnd = frames;
    if (sampleStart >= sampleEnd) sampleStart = 0U;
    if (loopStart < sampleStart) loopStart = sampleStart;
    if (loopEnd == 0U || loopEnd > sampleEnd) loopEnd = sampleEnd;
    if (loopEnd <= loopStart + 1U) {
      loopStart = sampleStart;
      loopEnd = sampleEnd;
    }

    auto markerCursor = [&](uint8_t marker) -> uint32_t {
      if (marker == 0U) return sampleStart;
      if (marker == 1U) return loopStart;
      if (marker == 2U) return loopEnd ? loopEnd - 1U : 0U;
      return sampleEnd ? sampleEnd - 1U : 0U;
    };

    if (_editorCursor >= frames) _editorCursor = frames - 1U;

    if (in.f[1]) { _sampleProcessingSlot = sl; _sampleProcessingAction = 0; return; }
    if (in.f[3]) { _sampleProcessingSlot = sl; _sampleProcessingAction = 1; return; }
    if (in.f[4]) { _sampleProcessingSlot = sl; _sampleProcessingAction = 2; return; }
    if (in.f[2]) {
      _editorZoom = (_editorZoom == 1) ? 2 : (_editorZoom == 2) ? 4 : (_editorZoom == 4) ? 8 : 1;
      return;
    }
    if (in.f[6]) { _loopParam = 0; go(SCR_LOOP); return; }

    if (in.f[5]) {
      _editorMarker = (uint8_t)((_editorMarker + 1U) & 3U);
      _editorCursor = markerCursor(_editorMarker);
      return;
    }

    // C016: Encoder press snaps the selected marker to the nearest real
    // zero crossing. Turning the encoder remains immediate live editing.
    if (in.encPress) {
      _zeroCrossSnapSlot = sl;
      _zeroCrossSnapMarker = _editorMarker & 3U;
      _zeroCrossSnapFrame = markerCursor(_editorMarker);
      _zeroCrossSnapPending = true;
      return;
    }

    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true;
      else _playRequest = true; // audition complete Sample Start -> Sustain Loop -> Sample End
      return;
    }

    int32_t move = 0;
    const uint32_t onePixelStep = (frames / 88UL) ? (frames / 88UL) : 1UL;
    const uint32_t buttonStep = onePixelStep * 4UL;
    if (in.encoderStep != 0) {
      const int16_t d = in.encoderStep;
      const int16_t a = d < 0 ? -d : d;
      uint32_t mul = 1U;
      if (a >= 10) mul = 16U;
      else if (a >= 5) mul = 8U;
      else if (a >= 2) mul = 3U;
      const uint32_t step = onePixelStep * mul;
      move = (d > 0) ? (int32_t)step : -(int32_t)step;
    }

    if (move != 0) {
      const PhoenixSampleMarkerEditResult edit = phxMoveSampleMarker(
          frames, sampleStart, loopStart, loopEnd, sampleEnd,
          _editorMarker, _editorCursor, move);
      _editorCursor = edit.cursor;
      _slotSampleStart[sl] = edit.sampleStart;
      _slotLoopStart[sl] = edit.loopStart;
      _slotLoopEnd[sl] = edit.loopEnd;
      _slotSampleEnd[sl] = edit.sampleEnd;
      // C011: publish one coherent four-marker edit with the slot captured at
      // the moment of the encoder movement. The OS must not reconstruct the
      // request later from a mutable selected-slot value.
      _sampleMarkerEditSlot = sl;
      _sampleMarkerEditSampleStart = edit.sampleStart;
      _sampleMarkerEditLoopStart = edit.loopStart;
      _sampleMarkerEditLoopEnd = edit.loopEnd;
      _sampleMarkerEditSampleEnd = edit.sampleEnd;
      _sampleMarkerEditPending = true;
      if (edit.sampleChanged) _sampleRangeChanged = true;
      if (edit.loopChanged) _loopRangeChanged = true;
      return;
    }
    return;
  }

  if (_screen == SCR_WAVE) {
    if (in.f[8]) { go(SCR_SOUND, 3); return; }
    if (in.f[1]) {
      _waveZoom = (_waveZoom == 1) ? 2 : (_waveZoom == 2) ? 4 : (_waveZoom == 4) ? 8 : 1;
      return;
    }

    // v0.6.0 Waveform Editor:
    // F7 toggles Play/Stop directly in Draw Waveform.
    // Encoder moves the edit cursor while stopped; encoder press plays from cursor.
    if (in.encoderStep != 0 && !_playing && _sampleFrames > 0) {
      const uint32_t baseStep = (_sampleFrames / 96UL) ? (_sampleFrames / 96UL) : 1UL;
      const uint32_t step = baseStep * (uint32_t)encAbs16(in.encoderStep);
      if (in.encoderStep > 0) {
        uint32_t nf = _waveCursorFrame + step;
        _waveCursorFrame = (nf >= _sampleFrames) ? (_sampleFrames - 1) : nf;
      } else {
        _waveCursorFrame = (_waveCursorFrame > step) ? (_waveCursorFrame - step) : 0;
      }
    }

    if (in.f[7]) {
      if (_playing || _recording) _stopRequest = true;
      else if (_sampleReady && _sampleFrames > 0) {
        // v0.7.13c FIX2:
        // In reverse mode a finished playback leaves the visible cursor at/near 0.
        // Restarting reverse from frame 0 immediately ends again, so F7 appeared to do nothing.
        // When the waveform is in REV mode and the cursor is at the left edge, restart from
        // the right edge (sample end). If the user has moved the cursor elsewhere, use it.
        if (_waveCursorFrame >= _sampleFrames) _waveCursorFrame = _sampleFrames - 1;
        _playFromCursorFrame = (_waveReverseMode && _waveCursorFrame == 0) ? (_sampleFrames - 1) : _waveCursorFrame;
        if (_waveReverseMode) _reversePlayFromCursorRequest = true;
        else _playFromCursorRequest = true;
      }
      return;
    }

    if (in.encPress) {
      if (_playing || _recording) _stopRequest = true;
      else if (_sampleReady && _sampleFrames > 0) {
        // v0.7.13c FIX2:
        // In reverse mode a finished playback leaves the visible cursor at/near 0.
        // Restarting reverse from frame 0 immediately ends again, so F7 appeared to do nothing.
        // When the waveform is in REV mode and the cursor is at the left edge, restart from
        // the right edge (sample end). If the user has moved the cursor elsewhere, use it.
        if (_waveCursorFrame >= _sampleFrames) _waveCursorFrame = _sampleFrames - 1;
        _playFromCursorFrame = (_waveReverseMode && _waveCursorFrame == 0) ? (_sampleFrames - 1) : _waveCursorFrame;
        if (_waveReverseMode) _reversePlayFromCursorRequest = true;
        else _playFromCursorRequest = true;
      }
      return;
    }
    return;
  }


  if (_screen == SCR_MULTISAMPLE) {
    uint8_t sl=_msSlot&3,gr=_msGroup&15,ly=_msLayer%3;
    if(in.f[8]){_msEditMode=false;go(SCR_QUATTRO,6);return;}
    if(in.f[1]&&!_msEditMode){_autoMapBrowserMode=true;_keygroupImportMode=false;browserGoRoot();go(SCR_SAMPLE_BROWSER);return;}
    if(in.f[7]&&!_msEditMode){_keygroupImportMode=true;_multisampleAutoMapMode=false;browserGoRoot();go(SCR_SAMPLE_BROWSER);return;}
    if(in.f[2]&&!_msEditMode){_keygroupTargetSlot=sl;_keygroupTargetGroup=gr;_keygroupDeleteRequest=true;return;}
    if(in.encPress){_msEditMode=!_msEditMode;return;}
    int16_t d=in.encoderStep;if(in.f[5])d=-1;if(in.f[6])d=1;int step=encSign(d);
    if(!_msEditMode){if(step<0)_msParam=(_msParam==0)?30:_msParam-1;else if(step>0)_msParam=(_msParam>=30)?0:_msParam+1;}
    else if(step){
      if(_msParam==0){_msSlot=(uint8_t)((_msSlot+(step>0?1:3))&3);_selectedSlot=_msSlot;_selectSlotRequest=(int8_t)_selectedSlot;}
      else if(_msParam==1)_msGroup=(uint8_t)((_msGroup+(step>0?1:15))&15);
      else if(_msParam==2)_msLayer=(uint8_t)((_msLayer+(step>0?1:2))%3);
      else if(_msParam==3)_msRrVariant=(uint8_t)((_msRrVariant+(step>0?1:3))&3);
      else {sl=_msSlot&3;gr=_msGroup&15;ly=_msLayer%3;if(_msParam==4)_msLayerEnabled[sl][gr][ly]=!_msLayerEnabled[sl][gr][ly];
        else if(_msParam==5)_msVelLow[sl][gr][ly]=constrain((int)_msVelLow[sl][gr][ly]+step,1,(int)_msVelHigh[sl][gr][ly]);
        else if(_msParam==6)_msVelHigh[sl][gr][ly]=constrain((int)_msVelHigh[sl][gr][ly]+step,(int)_msVelLow[sl][gr][ly],127);
        else if(_msParam==7)_msLow[sl][gr]=constrain((int)_msLow[sl][gr]+step,0,(int)_msHigh[sl][gr]);
        else if(_msParam==8)_msHigh[sl][gr]=constrain((int)_msHigh[sl][gr]+step,(int)_msLow[sl][gr],127);
        else if(_msParam==9)_msRoot[sl][gr]=constrain((int)_msRoot[sl][gr]+step,0,127);
        else if(_msParam==10)_msLevel[sl][gr][ly]=constrain((int)_msLevel[sl][gr][ly]+step,0,100);
        else if(_msParam==11)_msPan[sl][gr][ly]=constrain((int)_msPan[sl][gr][ly]+step*2,-100,100);
        else if(_msParam==12){static const uint8_t modes[5]={0,2,3,4,5};uint8_t idx=0;for(uint8_t q=0;q<5;++q)if(modes[q]==_msRrMode[sl][gr][ly])idx=q;idx=(uint8_t)((idx+(step>0?1:4))%5);_msRrMode[sl][gr][ly]=modes[idx];}
        else if(_msParam==13)_msChoke[sl][gr]=(uint8_t)constrain((int)_msChoke[sl][gr]+step,0,8);
        else if(_msParam==14)_msOneShot[sl][gr]=!_msOneShot[sl][gr];
        else if(_msParam==15)_msChokeFadeMs[sl][gr]=(uint16_t)constrain((int)_msChokeFadeMs[sl][gr]+step*5,0,250);
        else if(_msParam==16)_msPlayMode[sl][gr]=(uint8_t)((_msPlayMode[sl][gr]+(step>0?1:2))%3);
        else if(_msParam==17)_msExclusiveGroup[sl][gr]=(uint8_t)constrain((int)_msExclusiveGroup[sl][gr]+step,0,8);
        else if(_msParam==18)_msRetriggerLegato[sl][gr]=!_msRetriggerLegato[sl][gr];
        else if(_msParam==19)_msStartPct[sl][gr]=(uint8_t)constrain((int)_msStartPct[sl][gr]+step,0,(int)_msEndPct[sl][gr]-1);
        else if(_msParam==20)_msEndPct[sl][gr]=(uint8_t)constrain((int)_msEndPct[sl][gr]+step,(int)_msStartPct[sl][gr]+1,100);
        else if(_msParam==21)_msReverse[sl][gr]=!_msReverse[sl][gr];
        else if(_msParam==22)_msTranspose[sl][gr]=(int8_t)constrain((int)_msTranspose[sl][gr]+step,-24,24);
        else if(_msParam==23)_msFineCent[sl][gr]=(int16_t)constrain((int)_msFineCent[sl][gr]+step*5,-100,100);
        else if(_msParam==24)_msKeytrackPct[sl][gr]=(uint8_t)constrain((int)_msKeytrackPct[sl][gr]+step*5,0,100);
        else if(_msParam==25)_msLoopMode[sl][gr][ly]=(uint8_t)((_msLoopMode[sl][gr][ly]+(step>0?1:2))%3);
        else if(_msParam==26)_msLoopStartPct[sl][gr][ly]=(uint8_t)constrain((int)_msLoopStartPct[sl][gr][ly]+step,0,(int)_msLoopEndPct[sl][gr][ly]-2);
        else if(_msParam==27)_msLoopEndPct[sl][gr][ly]=(uint8_t)constrain((int)_msLoopEndPct[sl][gr][ly]+step,(int)_msLoopStartPct[sl][gr][ly]+2,100);
        else if(_msParam==28)_msLoopXfadeMs[sl][gr][ly]=(uint8_t)constrain((int)_msLoopXfadeMs[sl][gr][ly]+step*2,0,50);
        else if(_msParam==29)_msVelToLevel[sl][gr][ly]=(uint8_t)constrain((int)_msVelToLevel[sl][gr][ly]+step*5,0,100);
        else if(_msParam==30)_msVelToFilter[sl][gr][ly]=(uint8_t)constrain((int)_msVelToFilter[sl][gr][ly]+step*5,0,100);
        _msEnabled[sl][gr]=true;_msChanged=true;}
    }return;
  }

  if (_screen == SCR_KEYBOARD) {
    // F1..F4 remain direct sample triggers. Encoder operates the menu.
    for (uint8_t i = 1; i <= 4; ++i) {
      if (in.f[i]) {
        _selectedSlot = i - 1;
        _selectSlotRequest = (int8_t)_selectedSlot;
        if (_slotRecorded[_selectedSlot]) _playRequest = true;
      }
    }
    const uint8_t maxParam = (_quattroMode == 0) ? 4 : 3; // MODE,SLOT,LOW,HIGH,ROOT or MODE,SLOT,CHANNEL,ROOT mapped below
    if (in.encPress) { _quattroEditMode = !_quattroEditMode; return; }
    int step = in.encoderStep;
    if (!_quattroEditMode) {
      if (step < 0) _quattroParam = (_quattroParam == 0) ? maxParam : (_quattroParam - 1);
      else if (step > 0) _quattroParam = (_quattroParam >= maxParam) ? 0 : (_quattroParam + 1);
    } else if (step != 0) {
      int dir = step < 0 ? -1 : 1;
      uint8_t sl = _selectedSlot & 3;
      if (_quattroParam == 0) { _quattroMode ^= 1; _quattroParam = 0; }
      else if (_quattroParam == 1) { _selectedSlot = (uint8_t)((_selectedSlot + (dir > 0 ? 1 : 3)) & 3); _selectSlotRequest = (int8_t)_selectedSlot; }
      else if (_quattroMode == 0 && _quattroParam == 2) { int v=(int)_quattroKeyLow[sl]+dir; _quattroKeyLow[sl]=constrain(v,0,(int)_quattroKeyHigh[sl]); }
      else if (_quattroMode == 0 && _quattroParam == 3) { int v=(int)_quattroKeyHigh[sl]+dir; _quattroKeyHigh[sl]=constrain(v,(int)_quattroKeyLow[sl],127); }
      else if (_quattroMode == 0 && _quattroParam == 4) { int v=(int)_pitchRootNote[sl]+dir; _pitchRootNote[sl]=constrain(v,0,127); _pitchChanged=true; }
      else if (_quattroMode == 1 && _quattroParam == 2) { int v=(int)_quattroMidiChannel[sl]+dir; _quattroMidiChannel[sl]=constrain(v,1,16); }
      else if (_quattroMode == 1 && _quattroParam == 3) { int v=(int)_pitchRootNote[sl]+dir; _pitchRootNote[sl]=constrain(v,0,127); _pitchChanged=true; }
      _quattroChanged = true;
    }
    if (_quattroEditMode && (in.f[5] || in.f[6])) {
      PhoenixInputState t = in; t.encoderStep = in.f[5] ? -1 : 1;
      // handled on next physical encoder action; F5/F6 support below via compact duplicate
      int dir=in.f[5]?-1:1; uint8_t sl=_selectedSlot&3;
      if (_quattroParam==0) _quattroMode^=1;
      else if (_quattroParam==1){_selectedSlot=(uint8_t)((_selectedSlot+(dir>0?1:3))&3);_selectSlotRequest=(int8_t)_selectedSlot;}
      else if (_quattroMode==0&&_quattroParam==2)_quattroKeyLow[sl]=constrain((int)_quattroKeyLow[sl]+dir,0,(int)_quattroKeyHigh[sl]);
      else if (_quattroMode==0&&_quattroParam==3)_quattroKeyHigh[sl]=constrain((int)_quattroKeyHigh[sl]+dir,(int)_quattroKeyLow[sl],127);
      else if (_quattroMode==0&&_quattroParam==4){_pitchRootNote[sl]=constrain((int)_pitchRootNote[sl]+dir,0,127);_pitchChanged=true;}
      else if (_quattroMode==1&&_quattroParam==2)_quattroMidiChannel[sl]=constrain((int)_quattroMidiChannel[sl]+dir,1,16);
      else if (_quattroMode==1&&_quattroParam==3){_pitchRootNote[sl]=constrain((int)_pitchRootNote[sl]+dir,0,127);_pitchChanged=true;}
      _quattroChanged=true;
    }
    if (in.f[5] && !_quattroEditMode) { go(SCR_FILTER); return; }
    if (in.f[6] && !_quattroEditMode) { go(SCR_MIXER); return; }
    if (in.f[7]) { _quattroEditMode=false; go(SCR_VOICE); return; }
    if (in.f[8]) { _quattroEditMode=false; go(SCR_QUATTRO, 5); }
    return;
  }

  if (_screen == SCR_VOICE) {
    uint8_t sl=_selectedSlot&3;
    if (in.f[8]) { _voiceEditMode=false; go(SCR_KEYBOARD); return; }
    if (in.encPress) { _voiceEditMode=!_voiceEditMode; return; }
    int step=in.encoderStep; if (in.f[5]) step=-1; if (in.f[6]) step=1;
    if (!_voiceEditMode) {
      if (step<0) _voiceParam=(_voiceParam==0)?4:_voiceParam-1;
      else if(step>0) _voiceParam=(_voiceParam>=4)?0:_voiceParam+1;
    } else if(step) {
      int d=step<0?-1:1;
      if(_voiceParam==0){_selectedSlot=(uint8_t)((_selectedSlot+(d>0?1:3))&3);_selectSlotRequest=(int8_t)_selectedSlot;}
      else if(_voiceParam==1){_voiceMode[sl]=(uint8_t)((_voiceMode[sl]+(d>0?1:2))%3); if(_voiceMode[sl]) _voiceLimit[sl]=1;}
      else if(_voiceParam==2 && _voiceMode[sl]==0)_voiceLimit[sl]=constrain((int)_voiceLimit[sl]+d,1,12);
      else if(_voiceParam==3)_notePriority[sl]=(uint8_t)((_notePriority[sl]+(d>0?1:2))%3);
      else if(_voiceParam==4)_glideMs[sl]=constrain((int)_glideMs[sl]+d*10,0,2000);
      _voiceChanged=true;
    }
    return;
  }

  if (_screen == SCR_FILTER) {
    uint8_t sl=_selectedSlot&3; if(in.f[8]){_filterEditMode=false;go(SCR_KEYBOARD);return;} if(in.f[7]&&!_filterEditMode){go(SCR_FILTER_ENV);return;} if(in.encPress){_filterEditMode=!_filterEditMode;return;} int step=in.encoderStep;if(in.f[5])step=-1;if(in.f[6])step=1;
    if(!_filterEditMode){if(step<0)_filterParam=(_filterParam==0)?5:_filterParam-1;else if(step>0)_filterParam=(_filterParam>=5)?0:_filterParam+1;}
    else if(step){int d=step<0?-1:1;if(_filterParam==0){_selectedSlot=(uint8_t)((_selectedSlot+(d>0?1:3))&3);_selectSlotRequest=(int8_t)_selectedSlot;}else if(_filterParam==1)_filterCutoff[sl]=constrain((int)_filterCutoff[sl]+d,0,100);else if(_filterParam==2)_filterResonance[sl]=constrain((int)_filterResonance[sl]+d,0,100);else if(_filterParam==3)_filterEnvAmount[sl]=constrain((int)_filterEnvAmount[sl]+d,-100,100);else if(_filterParam==4)_filterVelocity[sl]=constrain((int)_filterVelocity[sl]+d,0,100);else _filterKeytrack[sl]=constrain((int)_filterKeytrack[sl]+d,0,100);_filterChanged=true;} return;
  }
  if (_screen == SCR_FILTER_ENV) {
    uint8_t sl=_selectedSlot&3;if(in.f[8]){_filterEnvEditMode=false;go(SCR_FILTER);return;}if(in.encPress){_filterEnvEditMode=!_filterEnvEditMode;return;}int step=in.encoderStep;if(in.f[5])step=-1;if(in.f[6])step=1;if(!_filterEnvEditMode){if(step<0)_filterEnvParam=(_filterEnvParam==0)?4:_filterEnvParam-1;else if(step>0)_filterEnvParam=(_filterEnvParam>=4)?0:_filterEnvParam+1;}else if(step){int d=step<0?-1:1;if(_filterEnvParam==0){_selectedSlot=(uint8_t)((_selectedSlot+(d>0?1:3))&3);_selectSlotRequest=(int8_t)_selectedSlot;}else if(_filterEnvParam==1)_filterAttackMs[sl]=constrain((int)_filterAttackMs[sl]+d*10,0,2000);else if(_filterEnvParam==2)_filterDecayMs[sl]=constrain((int)_filterDecayMs[sl]+d*10,0,5000);else if(_filterEnvParam==3)_filterSustainPct[sl]=constrain((int)_filterSustainPct[sl]+d,0,100);else _filterReleaseMs[sl]=constrain((int)_filterReleaseMs[sl]+d*10,0,5000);_filterEnvChanged=true;}return;
  }

  if (_screen == SCR_MIXER) {
    uint8_t sl=_selectedSlot&3;
    if (in.f[8]) { _mixerEditMode=false; go(SCR_KEYBOARD); return; }
    if (in.encPress) { _mixerEditMode=!_mixerEditMode; return; }
    int step=in.encoderStep; if (in.f[5]) step=-1; if (in.f[6]) step=1;
    if (!_mixerEditMode) {
      if (step<0) _mixerParam=(_mixerParam==0)?3:_mixerParam-1;
      else if(step>0) _mixerParam=(_mixerParam>=3)?0:_mixerParam+1;
    } else if(step) {
      int d=step<0?-1:1;
      if(_mixerParam==0){_selectedSlot=(uint8_t)((_selectedSlot+(d>0?1:3))&3);_selectSlotRequest=(int8_t)_selectedSlot;}
      else if(_mixerParam==1)_mixerLevel[sl]=(uint8_t)constrain((int)_mixerLevel[sl]+d,0,100);
      else if(_mixerParam==2)_mixerPan[sl]=(int8_t)constrain((int)_mixerPan[sl]+d*2,-100,100);
      else if(_mixerParam==3)_echoSend[sl]=(uint8_t)constrain((int)_echoSend[sl]+d,0,100);
      _mixerChanged=true;
    }
    return;
  }

  if (in.f[1] || in.encoderStep < 0) moveSel(-1);
  if (in.f[3] || in.encoderStep > 0) moveSel(1);
  if (in.f[7] || in.encPress) enterSelection();
  if (in.f[8] && _screen != SCR_MAIN) go(SCR_MAIN);
}

void PhoenixScreenManager::drawSoundSampling(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "SOUND SAMPLING");

  const int listX = x + PhoenixLayout::MENU_X_PAD;
  const int listY = y + PhoenixLayout::MENU_FIRST_Y;
  const int rowH  = PhoenixLayout::MENU_ROW_H;
  for (uint8_t i = 0; i < 8; ++i) {
    int yy = listY + i * rowH;
    bool sel = (i == _sel);
    gui.text(listX, yy, sel ? ">" : " ", false);
    gui.text(listX + 5, yy, SOUND_ITEMS[i], false);
  }

  // v0.7.12: SOUND SAMPLING now has 8 original-style entries (including SAMPLE EDITOR).
  // The former status line at y=51 would overlap the last entry, so transport
  // status is shown by the LEVEL footer and the bottom help line instead.

  if (_recUxStatus == 1) {
    char msg[18];
    snprintf(msg, sizeof(msg), "S%u ARMED L%u", (unsigned)((_selectedSlot & 3U) + 1U), (unsigned)_triggerLevel);
    gui.drawClassicHelp(msg, "7CAN", "8BACK");
  } else if (_recUxStatus == 3) {
    gui.drawClassicHelp("TRIGGER!", "7STOP", "8BACK");
  } else if (_recUxStatus == 2) {
    const uint32_t sr = phxNonZero(_sampleRate);
    const uint32_t sec = _recordFrames / sr;
    const uint32_t tenth = ((_recordFrames % sr) * 10UL) / sr;
    const uint32_t leftFrames = (_recordCapacityFrames > _recordFrames) ? (_recordCapacityFrames - _recordFrames) : 0U;
    const uint32_t leftSec = leftFrames / sr;
    const uint32_t leftTenth = ((leftFrames % sr) * 10UL) / sr;
    char left[14], mid[18];
    snprintf(left, sizeof(left), "S%u %02lu.%luS", (unsigned)((_selectedSlot & 3U) + 1U), (unsigned long)(sec % 100UL), (unsigned long)tenth);
    snprintf(mid, sizeof(mid), "L%02lu.%lu 7STOP", (unsigned long)(leftSec % 100UL), (unsigned long)leftTenth);
    gui.drawClassicHelp(left, mid, "8BACK");
  } else if (_recUxStatus == 4) {
    gui.drawClassicHelp("REC DONE", "7SEL", "8BACK");
  } else if (_recUxStatus == 6) {
    gui.drawClassicHelp(_analysisTrimApplied ? "TRIM APPLIED" : "SAMPLE OPEN", "7SEL", "8BACK");
  } else if (_recUxStatus == 5) {
    gui.drawClassicHelp("REC CANCEL", "7SEL", "8BACK");
  } else if (_recording || _playing) gui.drawClassicHelp("", "7STOP", "8BACK");
  else gui.drawClassicHelp("1UP", "3DOWN 7SEL", "8BACK");
}


void PhoenixScreenManager::drawQuattroKeyboard(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"QUATTRO KEYBOARD");
  char v[24],lo[8],hi[8],root[8]; uint8_t sl=_selectedSlot&3;
  phxNoteName(_quattroKeyLow[sl],lo,sizeof(lo)); phxNoteName(_quattroKeyHigh[sl],hi,sizeof(hi)); phxNoteName(_pitchRootNote[sl],root,sizeof(root));
  const char* labelsK[5]={"MODE","SLOT","LOW","HIGH","ROOT"};
  const char* labelsM[4]={"MODE","SLOT","MIDI CH","ROOT"};
  uint8_t rows=(_quattroMode==0)?5:4;
  for(uint8_t i=0;i<rows;++i){
    int yy=y+13+i*8; const char* lab=(_quattroMode==0)?labelsK[i]:labelsM[i];
    if(i==0) snprintf(v,sizeof(v),"%s",_quattroMode?"MULTI":"KEYZONE");
    else if(i==1) snprintf(v,sizeof(v),"S%u",(unsigned)(sl+1));
    else if(_quattroMode==0&&i==2) snprintf(v,sizeof(v),"%s",lo);
    else if(_quattroMode==0&&i==3) snprintf(v,sizeof(v),"%s",hi);
    else if(_quattroMode==0&&i==4) snprintf(v,sizeof(v),"%s",root);
    else if(_quattroMode==1&&i==2) snprintf(v,sizeof(v),"%u FULL",(unsigned)_quattroMidiChannel[sl]);
    else snprintf(v,sizeof(v),"%s",root);

    // Phoenix UI standard: selection is indicated only by the leading arrow.
    // Inverse text is intentionally avoided because it is difficult to read
    // on the target OLED. Edit mode keeps the same readable rendering.
    const bool sel = (i == _quattroParam);
    gui.text(x+4, yy, sel ? ">" : " ", false);
    gui.text(x+9, yy, lab, false);
    gui.text(x+61, yy, v, false);
  }
  gui.drawClassicHelp("1-4PLAY","5FLT 6MIX","7VOC 8BACK");
}

void PhoenixScreenManager::drawVoiceAllocation(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"VOICE ALLOCATION");
  uint8_t sl=_selectedSlot&3; const char* modeName[3]={"POLY","MONO","LEGATO"}; const char* priName[3]={"LAST","LOW","HIGH"};
  const char* labels[5]={"SLOT","MODE","MAX VOICES","PRIORITY","GLIDE"}; char v[18];
  for(uint8_t i=0;i<5;++i){ int yy=y+14+i*9; gui.text(x+5,yy,(i==_voiceParam)?">":" ",false); gui.text(x+11,yy,labels[i],false);
    if(i==0) snprintf(v,sizeof(v),"S%u",sl+1); else if(i==1) snprintf(v,sizeof(v),"%s",modeName[_voiceMode[sl]]);
    else if(i==2) snprintf(v,sizeof(v),"%u",_voiceMode[sl]?1:_voiceLimit[sl]); else if(i==3) snprintf(v,sizeof(v),"%s",priName[_notePriority[sl]]); else snprintf(v,sizeof(v),"%u MS",_glideMs[sl]);
    gui.text(x+76,yy,v,false);
  }
  gui.drawClassicHelp("5- 6+","PUSH EDIT","8BACK");
}


void PhoenixScreenManager::drawQuattroMixer(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"QUATTRO MIXER");
  const uint8_t sl=_selectedSlot&3;
  const char* labels[4]={"SLOT","LEVEL","PAN","ECHO SEND"};
  char v[18];
  for(uint8_t i=0;i<4;++i){
    int yy=y+15+i*10;
    gui.text(x+5,yy,(i==_mixerParam)?">":" ",false);
    gui.text(x+11,yy,labels[i],false);
    if(i==0) snprintf(v,sizeof(v),"S%u",(unsigned)(sl+1));
    else if(i==1) snprintf(v,sizeof(v),"%u%%",(unsigned)_mixerLevel[sl]);
    else if(i==2){ if(_mixerPan[sl]==0) snprintf(v,sizeof(v),"CENTER"); else if(_mixerPan[sl]<0) snprintf(v,sizeof(v),"L%u",(unsigned)(-_mixerPan[sl])); else snprintf(v,sizeof(v),"R%u",(unsigned)_mixerPan[sl]); }
    else snprintf(v,sizeof(v),"%u%%",(unsigned)_echoSend[sl]);
    gui.text(x+70,yy,v,false);
  }
  gui.drawClassicHelp("5- 6+","PUSH EDIT","8BACK");
}

void PhoenixScreenManager::drawQuattroFilter(PhoenixGUI &gui){gui.drawClassicLevelMeter(0);const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;gui.drawWindow(x,y,w,h,"QUATTRO FILTER");uint8_t sl=_selectedSlot&3;const char* l[6]={"SLOT","CUTOFF","RESONANCE","ENV AMT","VEL AMT","KEYTRACK"};char v[16];for(uint8_t i=0;i<6;++i){int yy=y+10+i*8;gui.text(x+4,yy,(i==_filterParam)?">":" ",false);gui.text(x+10,yy,l[i],false);if(i==0)snprintf(v,sizeof(v),"S%u",sl+1);else if(i==1)snprintf(v,sizeof(v),"%u%%",_filterCutoff[sl]);else if(i==2)snprintf(v,sizeof(v),"%u%%",_filterResonance[sl]);else if(i==3)snprintf(v,sizeof(v),"%+d%%",_filterEnvAmount[sl]);else if(i==4)snprintf(v,sizeof(v),"%u%%",_filterVelocity[sl]);else snprintf(v,sizeof(v),"%u%%",_filterKeytrack[sl]);gui.text((i==3)?x+70:x+74,yy,v,false);}gui.drawClassicHelp("5- 6+","7ADSR","8BACK");}
void PhoenixScreenManager::drawFilterEnvelope(PhoenixGUI &gui){gui.drawClassicLevelMeter(0);const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;gui.drawWindow(x,y,w,h,"FILTER ADSR");uint8_t sl=_selectedSlot&3;const char*l[5]={"SLOT","ATTACK","DECAY","SUSTAIN","RELEASE"};char v[18];for(uint8_t i=0;i<5;++i){int yy=y+13+i*9;gui.text(x+5,yy,(i==_filterEnvParam)?">":" ",false);gui.text(x+11,yy,l[i],false);if(i==0)snprintf(v,sizeof(v),"S%u",sl+1);else if(i==1)snprintf(v,sizeof(v),"%u MS",_filterAttackMs[sl]);else if(i==2)snprintf(v,sizeof(v),"%u MS",_filterDecayMs[sl]);else if(i==3)snprintf(v,sizeof(v),"%u%%",_filterSustainPct[sl]);else snprintf(v,sizeof(v),"%u MS",_filterReleaseMs[sl]);gui.text(x+72,yy,v,false);}gui.drawClassicHelp("5- 6+","PUSH EDIT","8BACK");}

void PhoenixScreenManager::drawMultisampleEditor(PhoenixGUI &gui) {
  const int x=2,y=12,w=124,h=46;gui.drawWindow(x,y,w,h,"MULTISAMPLE EDIT");uint8_t sl=_msSlot&3,gr=_msGroup&15,ly=_msLayer%3,rv=_msRrVariant&3;const char *variantPath=_msPath?_msPath[sl][gr][ly][rv]:"";char top[28];snprintf(top,sizeof(top),"S%u G%02u L%u R%u %s",sl+1,gr+1,ly+1,rv+1,phxBaseName(variantPath));gui.text(x+4,y+10,top,false);
  static const char*labels[31]={"SLOT","GROUP","LAYER","RR VAR","STATUS","VEL LOW","VEL HIGH","LOW","HIGH","ROOT","LEVEL","PAN","RR MODE","CHOKE","ONE SHOT","CHOKE FADE","PLAY MODE","EXCL GROUP","RETRIGGER","START","END","REVERSE","TRANSPOSE","FINE","KEYTRACK","LOOP MODE","LOOP START","LOOP END","LOOP XFADE","VEL LEVEL","VEL FILTER"};
  uint8_t first=(_msParam>3)?(_msParam-3):0;if(first>27)first=27;
  for(uint8_t row=0;row<4;++row){uint8_t i=first+row;int yy=y+18+row*7;char v[20],n[8];gui.text(x+4,yy,(i==_msParam)?">":" ",false);gui.text(x+10,yy,labels[i],false);
    if(i==0)snprintf(v,sizeof(v),"S%u",sl+1);else if(i==1)snprintf(v,sizeof(v),"%02u/16",gr+1);else if(i==2)snprintf(v,sizeof(v),"%u/3",ly+1);else if(i==3)snprintf(v,sizeof(v),"%u/4",rv+1);else if(i==4)snprintf(v,sizeof(v),"%s",_msLayerEnabled[sl][gr][ly]?"ON":"OFF");else if(i==5)snprintf(v,sizeof(v),"%u",_msVelLow[sl][gr][ly]);else if(i==6)snprintf(v,sizeof(v),"%u",_msVelHigh[sl][gr][ly]);else if(i==7){phxNoteName(_msLow[sl][gr],n,sizeof(n));snprintf(v,sizeof(v),"%s",n);}else if(i==8){phxNoteName(_msHigh[sl][gr],n,sizeof(n));snprintf(v,sizeof(v),"%s",n);}else if(i==9){phxNoteName(_msRoot[sl][gr],n,sizeof(n));snprintf(v,sizeof(v),"%s",n);}else if(i==10)snprintf(v,sizeof(v),"%u%%",_msLevel[sl][gr][ly]);else if(i==11){if(_msPan[sl][gr][ly]==0)snprintf(v,sizeof(v),"CENTER");else if(_msPan[sl][gr][ly]<0)snprintf(v,sizeof(v),"L%u",(unsigned)-_msPan[sl][gr][ly]);else snprintf(v,sizeof(v),"R%u",(unsigned)_msPan[sl][gr][ly]);}else if(i==12){const char*m="OFF";if(_msRrMode[sl][gr][ly]==2)m="2-WAY";else if(_msRrMode[sl][gr][ly]==3)m="3-WAY";else if(_msRrMode[sl][gr][ly]==4)m="4-WAY";else if(_msRrMode[sl][gr][ly]==5)m="RANDOM";snprintf(v,sizeof(v),"%s",m);}else if(i==13){if(_msChoke[sl][gr])snprintf(v,sizeof(v),"GROUP %u",_msChoke[sl][gr]);else snprintf(v,sizeof(v),"OFF");}else if(i==14)snprintf(v,sizeof(v),"%s",_msOneShot[sl][gr]?"ON":"OFF");else if(i==15)snprintf(v,sizeof(v),"%u ms",_msChokeFadeMs[sl][gr]);else if(i==16){const char*m=_msPlayMode[sl][gr]==1?"MONO":(_msPlayMode[sl][gr]==2?"EXCLUSIVE":"POLY");snprintf(v,sizeof(v),"%s",m);}else if(i==17){if(_msExclusiveGroup[sl][gr])snprintf(v,sizeof(v),"GROUP %u",_msExclusiveGroup[sl][gr]);else snprintf(v,sizeof(v),"OFF");}else if(i==18)snprintf(v,sizeof(v),"%s",_msRetriggerLegato[sl][gr]?"LEGATO":"RESTART");else if(i==19)snprintf(v,sizeof(v),"%u%%",_msStartPct[sl][gr]);else if(i==20)snprintf(v,sizeof(v),"%u%%",_msEndPct[sl][gr]);else if(i==21)snprintf(v,sizeof(v),"%s",_msReverse[sl][gr]?"ON":"OFF");else if(i==22)snprintf(v,sizeof(v),"%+d st",_msTranspose[sl][gr]);else if(i==23)snprintf(v,sizeof(v),"%+d ct",_msFineCent[sl][gr]);else if(i==24)snprintf(v,sizeof(v),"%u%%",_msKeytrackPct[sl][gr]);else if(i==25){const char*m=_msLoopMode[sl][gr][ly]==1?"FORWARD":(_msLoopMode[sl][gr][ly]==2?"PING-PONG":"OFF");snprintf(v,sizeof(v),"%s",m);}else if(i==26)snprintf(v,sizeof(v),"%u%%",_msLoopStartPct[sl][gr][ly]);else if(i==27)snprintf(v,sizeof(v),"%u%%",_msLoopEndPct[sl][gr][ly]);else if(i==28)snprintf(v,sizeof(v),"%u ms",_msLoopXfadeMs[sl][gr][ly]);else if(i==29)snprintf(v,sizeof(v),"%u%%",_msVelToLevel[sl][gr][ly]);else snprintf(v,sizeof(v),"%u%%",_msVelToFilter[sl][gr][ly]);gui.text(x+76,yy,v,false);}
  gui.drawClassicHelp("1AUTO","2DEL 7WAV","8BACK");
}

void PhoenixScreenManager::drawQuattro(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "QUATTRO SAMPLING");

  const int listX = x + PhoenixLayout::MENU_X_PAD;
  const int listY = y + PhoenixLayout::MENU_FIRST_Y;
  const int rowH  = PhoenixLayout::MENU_ROW_H;

  char line[24];
  for (uint8_t i = 0; i < 4; ++i) {
    const char *st = "EMPTY";
    if (_slotRecording[i]) st = "REC";
    else if (_slotPlaying[i]) st = "PLAY";
    else if (_slotRecorded[i]) st = "READY";

    const uint32_t sr = phxNonZero(_sampleRate);
    const uint32_t sec = _slotFrames[i] / sr;
    if (_recUxStatus == 1 && i == (_selectedSlot & 3U)) {
      snprintf(line, sizeof(line), "S%u ARMED L%u", (unsigned)(i + 1), (unsigned)_triggerLevel);
    } else if ((_recUxStatus == 2 || _recUxStatus == 3) && i == (_selectedSlot & 3U)) {
      const uint32_t rs = _recordFrames / sr;
      const uint32_t rt = ((_recordFrames % sr) * 10UL) / sr;
      snprintf(line, sizeof(line), "S%u REC %02lu.%luS", (unsigned)(i + 1), (unsigned long)(rs % 100UL), (unsigned long)rt);
    } else if (_slotRecorded[i] && !_slotRecording[i] && !_slotPlaying[i]) {
      snprintf(line, sizeof(line), "S%u %-5s %02lus", (unsigned)(i + 1), st, (unsigned long)(sec % 100UL));
    } else {
      snprintf(line, sizeof(line), "S%u %-5s", (unsigned)(i + 1), st);
    }

    int yy = listY + i * rowH;
    bool sel = (i == _sel);
    gui.text(listX, yy, sel ? ">" : " ", false);
    gui.text(listX + 5, yy, line, false);
  }

  const char *action = "RECORD";
  if (_recording || _playing) action = "STOP";
  else if (_slotRecorded[_selectedSlot]) action = "OVERWRITE";

  const char* extras[4] = { action, "KEYBOARD", "MULTISAMPLE", "EXIT" };
  for (uint8_t j = 0; j < 4; ++j) {
    uint8_t idx = j + 4;
    int yy = listY + idx * rowH;
    bool sel = (idx == _sel);
    gui.text(listX, yy, sel ? ">" : " ", false);
    gui.text(listX + 5, yy, extras[j], false);
  }

  // v0.5.6: The former ACTIVE Sx label overlapped the KEYBOARD row and
  // became visually corrupted when KEYBOARD was selected/inverted.
  // The active slot is now shown in the footer center instead.
  char active[10];
  snprintf(active, sizeof(active), "ACTIVE S%u", (unsigned)(_selectedSlot + 1));
  if (_recUxStatus == 1) {
    char left[14];
    snprintf(left, sizeof(left), "S%u ARMED", (unsigned)((_selectedSlot & 3U) + 1U));
    gui.drawClassicHelp(left, "7CANCEL", "8BACK");
  } else if (_recUxStatus == 2 || _recUxStatus == 3) {
    const uint32_t sr = phxNonZero(_sampleRate);
    const uint32_t sec = _recordFrames / sr;
    const uint32_t tenth = ((_recordFrames % sr) * 10UL) / sr;
    const uint32_t leftFrames = (_recordCapacityFrames > _recordFrames) ? (_recordCapacityFrames - _recordFrames) : 0U;
    const uint32_t leftSec = leftFrames / sr;
    const uint32_t leftTenth = ((leftFrames % sr) * 10UL) / sr;
    char left[14], mid[18];
    snprintf(left, sizeof(left), "S%u %02lu.%luS", (unsigned)((_selectedSlot & 3U) + 1U), (unsigned long)(sec % 100UL), (unsigned long)tenth);
    snprintf(mid, sizeof(mid), "L%02lu.%lu 7STOP", (unsigned long)(leftSec % 100UL), (unsigned long)leftTenth);
    gui.drawClassicHelp(left, mid, "8BACK");
  } else if (_recUxStatus == 4) {
    gui.drawClassicHelp("REC DONE", "7DO", "8BACK");
  } else if (_recUxStatus == 6) {
    char msg[18];
    snprintf(msg, sizeof(msg), _analysisTrimApplied ? "TRIM P%u" : "OPEN P%u", (unsigned)_analysisPeakPercent);
    gui.drawClassicHelp(msg, "7DO", "8BACK");
  } else if (_recording || _playing) gui.drawClassicHelp("1SLOT", "7STOP", "8BACK");
  else gui.drawClassicHelp("1SLOT", "7DO", "8BACK");
}


void PhoenixScreenManager::drawPitchConverter(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "PITCH CONVERTER");
  if (_pitchEditMode) gui.text(x + w - 22, y + 2, "EDIT", false);

  const uint8_t sl = _selectedSlot & 3;
  const int labelX = x + 6;
  const int valueX = x + 76;  // v0.7.5 FIX1: align ROOT NOTE value with SEMITONE/FINE
  const int firstY = y + 14;
  const int rowH = 7;

  char value[18];
  char note[8];
  const char *labels[6] = { "SEMITONE", "FINE", "ROOT NOTE", "OCTAVE", "PITCH TRK", "BEND RNG" };

  for (uint8_t i = 0; i < 6; ++i) {
    int yy = firstY + i * rowH;
    bool sel = (i == _pitchParam);

    if (i == 0) snprintf(value, sizeof(value), "%+d", (int)_pitchSemitone[sl]);
    else if (i == 1) snprintf(value, sizeof(value), "%+dC", (int)_pitchFineCent[sl]);
    else if (i == 2) { phxNoteName(_pitchRootNote[sl], note, sizeof(note)); snprintf(value, sizeof(value), "%s", note); }
    else if (i == 3) snprintf(value, sizeof(value), "%+d", (int)_keyboardOctave[sl]);
    else if (i == 4) snprintf(value, sizeof(value), "%s", _pitchTrack[sl] ? "ON" : "OFF");
    else snprintf(value, sizeof(value), "+/-%u", (unsigned)_pitchBendRange[sl]);

    // v0.7.13: Keyboard Engine parameters share the same Browse/Edit workflow.
    gui.text(labelX, yy, sel ? ">" : " ", false);
    gui.text(labelX + 5, yy, labels[i], false);
    gui.text(valueX, yy, value, false);
  }

  if (_playing || _recording) {
    gui.drawClassicHelp(_pitchEditMode ? "1DOWN" : "1PREV", "7STOP", "8BACK");
  } else if (_pitchEditMode) {
    gui.drawClassicHelp("1DOWN", "PUSH DONE", "8BACK");
  } else {
    gui.drawClassicHelp("F6-ADSR", "PUSH EDIT", "8BACK");
  }
}



void PhoenixScreenManager::drawEnvelope(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X, y = PhoenixLayout::WIN_Y, w = PhoenixLayout::WIN_W, h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "AMPLITUDE ADSR");
  if (_envEditMode) gui.text(x + w - 22, y + 2, "EDIT", false);
  const uint8_t sl = _selectedSlot & 3;
  const char *labels[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
  char value[18];
  for (uint8_t i = 0; i < 4; ++i) {
    const int yy = y + 15 + i * 10;
    gui.text(x + 6, yy, (i == _envParam) ? ">" : " ", false);
    gui.text(x + 11, yy, labels[i], false);
    if (i == 0) snprintf(value, sizeof(value), "%u MS", (unsigned)_envAttackMs[sl]);
    else if (i == 1) snprintf(value, sizeof(value), "%u MS", (unsigned)_envDecayMs[sl]);
    else if (i == 2) snprintf(value, sizeof(value), "%u %%", (unsigned)_envSustainPct[sl]);
    else snprintf(value, sizeof(value), "%u MS", (unsigned)_envReleaseMs[sl]);
    // Keep four-digit ADSR values and the unit clear of the right window frame.
    gui.text(x + 68, yy, value, false);
  }
  if (_envEditMode) gui.drawClassicHelp("1DOWN", "PUSH DONE", "F8-PITCH");
  else gui.drawClassicHelp("7PLAY", "PUSH EDIT", "F8-PITCH");
}

void PhoenixScreenManager::drawEcho(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "ECHO");
  if (_echoEditMode) gui.text(x + w - 22, y + 2, "EDIT", false);

  char line[24];
  char value[18];
  const int labelX = x + 6;
  const int valueX = x + 70;
  const int firstY = y + 11;
  const int rowH = 6;

  const char *labels[7] = { "DELAY", "FEEDBACK", "MIX", "SEND S1", "SEND S2", "SEND S3", "SEND S4" };
  for (uint8_t i = 0; i < 7; ++i) {
    int yy = firstY + i * rowH;
    bool sel = (i == _echoParam);

    if (i == 0) snprintf(value, sizeof(value), "%uMS", (unsigned)_echoDelayMs);
    else if (i == 1) snprintf(value, sizeof(value), "%u%%", (unsigned)_echoFeedback);
    else if (i == 2) snprintf(value, sizeof(value), "%u%%", (unsigned)_echoMix);
    else snprintf(value, sizeof(value), "%u%%", (unsigned)_echoSend[i - 3]);

    // v0.7.5 FIX1: selection uses only the Phoenix arrow.
    // Edit mode is indicated by EDIT in the title area; no < > marker.
    gui.text(labelX, yy, sel ? ">" : " ", false);
    gui.text(labelX + 5, yy, labels[i], false);
    gui.text(valueX, yy, value, false);
  }

  if (_playing || _recording) {
    gui.drawClassicHelp(_echoEditMode ? "1DOWN" : "1PREV", "7STOP", "8BACK");
  } else if (_echoEditMode) {
    gui.drawClassicHelp("1DOWN", "PUSH DONE", "8BACK");
  } else {
    gui.drawClassicHelp("7PLAY", "PUSH EDIT", "8BACK");
  }
}




void PhoenixScreenManager::drawSampleEditor(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  const uint8_t sl = _selectedSlot & 3;
  char editorTitle[20];
  snprintf(editorTitle, sizeof(editorTitle), _bankDirty ? "S%u SAMPLE EDITOR*" : "S%u SAMPLE EDITOR", (unsigned)(sl + 1U));
  gui.drawWindow(x, y, w, h, "");
  gui.text(x + 4, y + 2, editorTitle, false);
  if (_playing) gui.text(x + w - 24, y + 2, _playingReverse ? "REV" : "PLAY", false);

  const uint32_t frames = _slotFrames[sl];
  uint32_t sampleStart = _slotSampleStart[sl];
  uint32_t sampleEnd = _slotSampleEnd[sl];
  uint32_t loopStart = _slotLoopStart[sl];
  uint32_t loopEnd = _slotLoopEnd[sl];

  if (frames == 0U) {
    sampleStart = loopStart = loopEnd = sampleEnd = 0U;
    _editorCursor = 0U;
  } else {
    if (sampleEnd == 0U || sampleEnd > frames) sampleEnd = frames;
    if (sampleStart >= sampleEnd) sampleStart = 0U;
    if (loopStart < sampleStart) loopStart = sampleStart;
    if (loopEnd == 0U || loopEnd > sampleEnd) loopEnd = sampleEnd;
    if (loopEnd <= loopStart + 1U) { loopStart = sampleStart; loopEnd = sampleEnd; }
    if (_editorCursor >= frames) _editorCursor = frames - 1U;
  }

  const char *labels[4] = { "S.START", "L.START", "L.END", "S.END" };
  const uint32_t values[4] = { sampleStart, loopStart, loopEnd, sampleEnd };
  char line[24];
  for (uint8_t i = 0; i < 4; ++i) {
    snprintf(line, sizeof(line), "%c %-7s %07lu", (_editorMarker == i) ? '>' : ' ', labels[i], (unsigned long)values[i]);
    gui.text(x + 6, y + 10 + i * 6, line, false);
  }

  // C015: precise selected-marker readout in both samples and time. Integer
  // arithmetic keeps display formatting out of the real-time audio path.
  const uint32_t selectedFrame = values[_editorMarker & 3U];
  const uint32_t sr = phxNonZero(_sampleRate);
  const uint32_t selectedMs = (uint32_t)(((uint64_t)selectedFrame * 1000ULL) / sr);
  char detail[28];
  if (selectedMs < 100000UL) {
    snprintf(detail, sizeof(detail), "%-7s %07lu %05luMS", labels[_editorMarker & 3U],
             (unsigned long)selectedFrame, (unsigned long)selectedMs);
  } else {
    const uint32_t sec10 = selectedMs / 100UL;
    snprintf(detail, sizeof(detail), "%-7s %07lu %04lu.%1luS", labels[_editorMarker & 3U],
             (unsigned long)selectedFrame, (unsigned long)(sec10 / 10UL),
             (unsigned long)(sec10 % 10UL));
  }
  gui.text(x + 6, y + 34, detail, false);

  auto posQ = [&](uint32_t v, bool exclusiveEnd = false) -> uint16_t {
    if (frames <= 1U) return 0U;
    if (exclusiveEnd && v > 0U) --v;
    if (v >= frames) v = frames - 1U;
    uint32_t q = (uint32_t)(((uint64_t)v * 10000ULL) / (uint64_t)(frames - 1U));
    if (q > 10000UL) q = 10000UL;
    return (uint16_t)q;
  };

  const bool playheadActive = _samplePlayheadActive;
  uint32_t playFrame = playheadActive ? _samplePlayheadFrame : _editorCursor;
  if (frames > 0U && playFrame >= frames) playFrame = frames - 1U;
  gui.setWaveformView(_editorZoom, posQ(_editorCursor));
  gui.drawSampleEditorWaveform(x + 4, y + 40, w - 8, 14,
                               posQ(sampleStart), posQ(loopStart), posQ(loopEnd, true), posQ(sampleEnd, true),
                               posQ(_editorCursor), posQ(playFrame), playheadActive, _editorMarker);
  gui.setWaveformView(1, 5000);

  if (frames == 0U) gui.text(x + 40, y + 44, "NO SAMPLE", false);

  // C020: the SAMPLE EDITOR uses one continuous 31-character footer.
  // PhoenixFont advances four pixels per character, so the complete 124-pixel
  // line fits the 128-pixel OLED without zone clipping or uneven gaps.
  char footer[40];
  snprintf(footer, sizeof(footer), "1T%c 2ZO 3DC%c 4NM%c 5MEN 6LO 7%c 8",
           _slotTrimEnabled[sl] ? '+' : '-',
           _slotDcEnabled[sl] ? '+' : '-',
           _slotNormalizeEnabled[sl] ? '+' : '-',
           (_playing || _recording) ? 'S' : 'P');
  gui.text(1, PhoenixLayout::HELP_Y, footer, false);
}

void PhoenixScreenManager::drawLoop(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"LOOP SETTINGS");
  const uint8_t sl=_selectedSlot&3U;
  char slotLine[8]; snprintf(slotLine,sizeof(slotLine),"S%u",(unsigned)(sl+1)); gui.text(x+w-14,y+2,slotLine,false);
  const uint8_t mode=(_slotLoopMode[sl]<=2U)?_slotLoopMode[sl]:0U;
  const char *modeName=(mode==1U)?"FORWARD":(mode==2U)?"ALTERNATE":"OFF";
  char xf[12]; if(_slotLoopXfadeMs[sl]) snprintf(xf,sizeof(xf),"%u MS",(unsigned)_slotLoopXfadeMs[sl]); else strcpy(xf,"OFF");
  gui.text(x+6,y+16,_loopParam==0?"> MODE":"  MODE",false); gui.text(x+58,y+16,modeName,false);
  gui.text(x+6,y+28,_loopParam==1?"> XFADE":"  XFADE",false); gui.text(x+58,y+28,xf,false);
  if(mode==2U) gui.text(x+6,y+40,"ALT: REFLECTION",false);
  else if(mode==1U && _slotLoopXfadeMs[sl]) gui.text(x+6,y+40,"END -> START BLEND",false);
  else gui.text(x+6,y+40,"NO CROSSFADE",false);
  gui.drawClassicHelp("1-/3+","5SEL 7PLAY","8BACK");
}


void PhoenixScreenManager::drawTriggerLevel(PhoenixGUI &gui) {
  // v0.7.43d: Edit mode remains functional, but values are never inverted.
  // The selection arrow is the only visual marker, matching the Phoenix UI style.
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "RECORD SETTINGS");

  const int labelX = x + 6;
  const int valueX = x + 72;
  const int firstY = y + 16;
  const int rowH = 10;

  gui.text(labelX, firstY, (_triggerParam == 0) ? ">" : " ", false);
  gui.text(labelX + 5, firstY, "MODE", false);
  gui.text(valueX, firstY, _triggerAuto ? "AUTO" : "MANUAL", false);

  char lvl[12];
  snprintf(lvl, sizeof(lvl), "%u", (unsigned)_triggerLevel);
  gui.text(labelX, firstY + rowH, (_triggerParam == 1) ? ">" : " ", false);
  gui.text(labelX + 5, firstY + rowH, "LEVEL", false);
  gui.text(valueX, firstY + rowH, lvl, false);

  gui.text(labelX, firstY + rowH * 2, "PRE-TRIG", false);
  gui.text(valueX, firstY + rowH * 2, _triggerAuto ? "32 MS" : "OFF", false);

  if (_triggerTestActive) {
    const uint32_t now = millis();
    const bool latched = _triggerTestTrigUntilMs != 0U &&
                         (int32_t)(_triggerTestTrigUntilMs - now) > 0;
    gui.text(x + 6, y + 46, (_triggerTestAbove || latched) ? "TEST: TRIGGER!" : "TEST: WAITING", false);
  } else if (_triggerAuto) {
    gui.text(x + 6, y + 46, "WAIT + 32MS HISTORY", false);
  } else {
    gui.text(x + 6, y + 46, "START IMMEDIATELY", false);
  }

  if (_recording || _playing) gui.drawClassicHelp("1DOWN", "PUSH 7STOP", "8BACK");
  else if (_triggerTestActive) gui.drawClassicHelp("1DOWN", "PUSH 7STOP", "8BACK");
  else if (_triggerEditMode) gui.drawClassicHelp("1DOWN", "PUSH 7TEST", "8BACK");
  else gui.drawClassicHelp("1PREV", "PUSH 7TEST", "8BACK");
}


void PhoenixScreenManager::drawVintageSampler(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X, y=PhoenixLayout::WIN_Y, w=PhoenixLayout::WIN_W, h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"VINTAGE SAMPLER");
  if (_vintageEditMode) gui.text(x+w-22,y+2,"EDIT",false);
  static const char* presets[8]={"CLEAN","SP12","MPC60","AMIGA","C64","LOFI","DARK","CUSTOM"};
  static const char* rates[9]={"32K","24K","22.05K","16K","12K","11.025K","8K","6K","4K"};
  static const char* bits[7]={"16 BIT","14 BIT","12 BIT","10 BIT","8 BIT","6 BIT","4 BIT"};
  static const char* filters[4]={"OFF","BRIGHT","CLASSIC","DARK"};
  const uint8_t sl=_selectedSlot&3; const int lx=x+5, vx=x+67, y0=y+13, rh=8;
  const char* labels[5]={"PRESET","SAMPLE RATE","BIT DEPTH","FILTER","JITTER"};
  char jitter[10]; snprintf(jitter,sizeof(jitter),"%u%%",(unsigned)_vintageJitter[sl]);
  const char* vals[5]={presets[_vintagePreset[sl]],rates[_vintageRate[sl]],bits[_vintageBits[sl]],filters[_vintageFilter[sl]],jitter};
  for(uint8_t i=0;i<5;++i){ gui.text(lx,y0+i*rh,(i==_vintageParam)?">":" ",false); gui.text(lx+5,y0+i*rh,labels[i],false); gui.text(vx,y0+i*rh,vals[i],false); }
  gui.drawClassicHelp("1PREV",_vintageEditMode?"PUSH DONE":"PUSH EDIT", "8BACK");
}

void PhoenixScreenManager::drawMidiType(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"MIDI CONTROL");
  if(_midiResetConfirm){
    gui.text(x+8,y+18,"RESET MIDI DEFAULTS?",false);
    gui.text(x+18,y+31,"7 YES   8 NO",false);
    return;
  }
  if (_midiEditMode && _sel == 5) gui.text(x+w-22,y+2,"EDIT",false);
  const int labelX=x+PhoenixLayout::MENU_X_PAD,valueX=x+82,listY=y+PhoenixLayout::MENU_FIRST_Y,rowH=PhoenixLayout::MENU_ROW_H;
  const bool states[5]={_midiEnabled,_midiClockEnabled,_midiNoteEnabled,_midiCcEnabled,_midiPcEnabled};
  // v0.7.41f: all eight MIDI CONTROL entries fit in the available window.
  // Keep the complete list visible so MIDI MONITOR and RESET DEFAULTS no
  // longer require scrolling.
  for(uint8_t row=0;row<8;++row){ uint8_t i=row; int yy=listY+row*rowH;
    gui.text(labelX,yy,(i==_sel)?">":" ",false); gui.text(labelX+5,yy,MIDI_CONTROL_ITEMS[i],false);
    if(i<5) gui.text(valueX,yy,states[i]?"ON":"OFF",false);
    else if(i==5){ char ch[5]; snprintf(ch,sizeof(ch),"%u",(unsigned)_midiChannel); gui.text(valueX,yy,ch,false); }
  }
  if(_midiEditMode && _sel==5) gui.drawClassicHelp("1-3 CH","PUSH DONE","8BACK");
  else gui.drawClassicHelp("1UP 3DOWN","7/PUSH","8BACK");
}

void PhoenixScreenManager::drawMidiMonitor(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.drawWindow(x,y,w,h,"MIDI MONITOR");
  PhoenixMidiMonitorSnapshot m=phxMidiMonitorSnapshot();
  const char *type="WAITING";
  switch(m.type){
    case PHX_MMON_NOTE_ON:type="NOTE ON";break; case PHX_MMON_NOTE_OFF:type="NOTE OFF";break;
    case PHX_MMON_CC:type="CONTROL CC";break; case PHX_MMON_PROGRAM:type="PROGRAM";break;
    case PHX_MMON_PITCH_BEND:type="PITCH BEND";break; case PHX_MMON_CHANNEL_PRESSURE:type="PRESSURE";break;
    case PHX_MMON_CLOCK:type="CLOCK";break; case PHX_MMON_START:type="START";break;
    case PHX_MMON_CONTINUE:type="CONTINUE";break; case PHX_MMON_STOP:type="STOP";break; default:break;
  }

  char line[28];
  gui.text(x+7,y+12,type,false);
  if(m.type!=PHX_MMON_NONE){
    // System realtime messages have no MIDI channel. Do not display the
    // misleading CH 0 used internally by the parser.
    if(m.type==PHX_MMON_START||m.type==PHX_MMON_CONTINUE||m.type==PHX_MMON_STOP||m.type==PHX_MMON_CLOCK){
      snprintf(line,sizeof(line),"EVENT %lu",(unsigned long)m.counter);
      gui.text(x+7,y+22,line,false);
    } else {
      snprintf(line,sizeof(line),"CH %u",(unsigned)m.channel);
      gui.text(x+7,y+21,line,false);
      if(m.type==PHX_MMON_CC) snprintf(line,sizeof(line),"CC %03u  VAL %03d",(unsigned)m.data1,(int)m.value);
      else if(m.type==PHX_MMON_NOTE_ON||m.type==PHX_MMON_NOTE_OFF) snprintf(line,sizeof(line),"NOTE %03u VEL %03d",(unsigned)m.data1,(int)m.value);
      else if(m.type==PHX_MMON_PROGRAM) snprintf(line,sizeof(line),"PROGRAM %03u",(unsigned)m.data1);
      else if(m.type==PHX_MMON_PITCH_BEND) snprintf(line,sizeof(line),"VALUE %d",(int)m.value);
      else if(m.type==PHX_MMON_CHANNEL_PRESSURE) snprintf(line,sizeof(line),"VALUE %03d",(int)m.value);
      else line[0]=0;
      if(line[0]) gui.text(x+7,y+30,line,false);
    }
  }

  // Compact clock diagnosis remains available even while individual F8 ticks
  // are filtered out of the event stream.
  snprintf(line,sizeof(line),"CLOCK %s",m.clockVisible?"SHOW":"HIDE");
  gui.text(x+7,y+40,line,false);
  if(m.clockPresent && m.clockBpm10){
    // Keep the compact clock readout safely inside the 124 px window.
    // "RX 120.0" is sufficient here; the page context already identifies BPM.
    snprintf(line,sizeof(line),"RX %u.%u",(unsigned)(m.clockBpm10/10U),(unsigned)(m.clockBpm10%10U));
  } else {
    snprintf(line,sizeof(line),"RX --");
  }
  // Right-align the clock readout with a 3 px inner margin.  The longest
  // current string ("RX 300.0") ends at x+w-3 and cannot cross the frame.
  const int rxX = x + w - 3 - (int)strlen(line) * 4;
  gui.text(rxX,y+40,line,false);
  gui.drawClassicHelp("","7 CLOCK","8BACK");
}

void PhoenixScreenManager::drawMidiChannel(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  char title[20];
  snprintf(title, sizeof(title), "MIDI CHANNEL #%u", (unsigned)_midiChannel);
  gui.drawWindow(x, y, w, h, title);
  if (_midiEditMode) gui.text(x + w - 22, y + 2, "EDIT", false);

  char ch[12];
  snprintf(ch, sizeof(ch), "%u", (unsigned)_midiChannel);
  gui.text(x + 6, y + 18, "> CHANNEL", false);
  gui.text(x + 76, y + 18, ch, false);
  // v0.7.13c FIX8A: Only show editable content here.
  // OMNI and MIDI IN are managed under MIDI CONTROL or reserved for later.
  gui.drawClassicHelp(_midiEditMode ? "1DOWN" : "", _midiEditMode ? "PUSH DONE" : "PUSH EDIT", "8BACK");
}

void PhoenixScreenManager::drawDiskUtilities(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "DISK UTILITIES");

  const int listX = x + PhoenixLayout::MENU_X_PAD;
  const int listY = y + PhoenixLayout::MENU_FIRST_Y;
  const int rowH  = PhoenixLayout::MENU_ROW_H;
  for (uint8_t i = 0; i < 5; ++i) {
    int yy = listY + i * rowH;
    bool sel = (i == _sel);
    gui.text(listX, yy, sel ? ">" : " ", false);
    gui.text(listX + 5, yy, DISK_ITEMS[i], false);
  }

  gui.fill(x + 4, y + 47, w - 8, 7, false);
  gui.text(x + 6, y + 47, _diskStatus, false);
  gui.drawClassicHelp("5BANK-", "6BANK+", "7DO 8BACK");
}

void PhoenixScreenManager::drawSampleBrowser(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;
  gui.drawWindow(x, y, w, h, "SAMPLE BROWSER");

  char crumb[48];
  phxMakeBreadcrumb(_browserPath, crumb, sizeof(crumb));
  gui.text(x + 6, y + 12, crumb, false);

  const int listX = x + PhoenixLayout::MENU_X_PAD;
  const int listY = y + 18;
  const int rowH = 7;
  const uint8_t visibleRows = 4;
  uint8_t first = 0;
  if (_browserCount > visibleRows && _browserSel >= visibleRows) first = _browserSel - visibleRows + 1;

  if (_browserCount == 0) {
    gui.text(listX, listY, "EMPTY FOLDER", false);
  } else {
    for (uint8_t r = 0; r < visibleRows; ++r) {
      uint8_t idx = first + r;
      if (idx >= _browserCount) break;
      int yy = listY + r * rowH;
      bool sel = (idx == _browserSel);
      gui.text(listX, yy, sel ? ">" : " ", false);
      gui.text(listX + 6, yy, _browserEntries[idx].isDir ? "DIR" : "WAV", false);
      char name[24];
      strncpy(name, _browserEntries[idx].name, sizeof(name));
      name[sizeof(name)-1] = 0;
      // Keep file browsing readable on the 128px display.
      if (strlen(name) > 20) {
        name[17] = '.';
        name[18] = '.';
        name[19] = '.';
        name[20] = 0;
      }
      gui.text(listX + 28, yy, name, false);
    }
  }

  // v0.7.13j: operational feedback has priority over static WAV metadata.
  // Previously LOADING PREV / PREVIEW PLAY was hidden by the metadata line.
  if (_browserStatus[0]) {
    gui.fill(x + 4, y + 47, w - 8, 7, false);
    gui.text(x + 6, y + 47, _browserStatus, false);
  } else if (_browserCount > 0 && _browserSel < _browserCount && !_browserEntries[_browserSel].isDir) {
    const BrowserEntry &e = _browserEntries[_browserSel];
    char info[36];
    if (e.wavInfoValid) {
      char dur[10];
      phxFormatDurationTenths(e.durationTenths, dur, sizeof(dur));
      const char *ch = (e.channels == 2) ? "ST" : "MO";
      snprintf(info, sizeof(info), "%luk %uB %s %s", (unsigned long)(e.sampleRate / 1000UL), (unsigned)e.bitsPerSample, ch, dur);
    } else {
      snprintf(info, sizeof(info), "UNSUPPORTED WAV");
    }
    gui.fill(x + 4, y + 47, w - 8, 7, false);
    gui.text(x + 6, y + 47, info, false);
  }

  if (_autoMapBrowserMode) {
    gui.drawClassicHelp("1ROOT","5MAP","8UP");
    return;
  } else if (_browserImportState == 1) {
    gui.fill(x + 10, y + 18, w - 20, 28, false);
    gui.text(x + 16, y + 21, "TARGET SLOT", false);
    char sl[16];
    snprintf(sl, sizeof(sl), "SLOT %u", (unsigned)(_browserImportSlot + 1));
    gui.text(x + 16, y + 31, sl, false);
    gui.drawClassicHelp("1SLOT", "7OK", "8CANCEL");
    return;
  }
  if (_browserImportState == 2) {
    gui.fill(x + 10, y + 18, w - 20, 28, false);
    char line[24];
    snprintf(line, sizeof(line), "SLOT %u USED", (unsigned)(_browserImportSlot + 1));
    gui.text(x + 16, y + 21, line, false);
    gui.text(x + 16, y + 31, "OVERWRITE ?", false);
    gui.drawClassicHelp("1NO", "7YES", "F8-NO");
    return;
  }

  if (_multisampleAutoMapMode) {
    gui.drawClassicHelp("1ROOT", "6MAP 7OPEN", phxIsWavRoot(_browserPath)?"8BACK":"8UP");
  } else if (_keygroupImportMode) {
    gui.drawClassicHelp("1ROOT", "6ASGN 7OPEN", phxIsWavRoot(_browserPath)?"8BACK":"8UP");
  } else if (phxIsWavRoot(_browserPath)) {
    gui.drawClassicHelp("1ROOT", "6IMP 7PREV", "8BACK");
  } else {
    gui.drawClassicHelp("1ROOT", "6IMP 7PREV", "8UP");
  }
}


static void phxSeqNoteName(uint8_t note, char *out, size_t outSize) {
  static const char * const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int octave = (int)(note / 12) - 2;
  snprintf(out, outSize, "%s%d", names[note % 12], octave);
}

void PhoenixScreenManager::drawSequencer(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X;
  const int y = PhoenixLayout::WIN_Y;
  const int w = PhoenixLayout::WIN_W;
  const int h = PhoenixLayout::WIN_H;

  // v0.7.34b: compact, non-overlapping sequencer header.
  gui.frame(x, y, w, h);
  char seqTitle[10];snprintf(seqTitle,sizeof(seqTitle),"SEQ P%u",(unsigned)(_seqPattern+1));gui.text(x+4,y+2,seqTitle,false);
  // v0.7.39f: the edit arrow belongs to the CLOCK object, not to its value.
  // It therefore remains visible for both INT and EXT when CLOCK is selected.
  gui.text(x + 39, y + 2, (_seqParam == 5) ? ">" : " ", false);
  char header[20];
  if (_seqExternalClock) {
    if (_seqExternalClockPresent) snprintf(header, sizeof(header), "EXT %3u", (unsigned)_seqExternalBpm);
    else snprintf(header, sizeof(header), "EXT WAIT");
  } else {
    snprintf(header, sizeof(header), "INT %3u", (unsigned)_seqBpm);
  }
  gui.text(x + 45, y + 2, header, false);
  gui.text(x + 94, y + 2, _seqRun ? ">" : "-", false);
  gui.hline(x + 1, y + PhoenixLayout::TITLE_LINE_Y, w - 2);

  const uint8_t tr = _seqTrack & 3;
  const uint8_t st = _seqStep & 15;
  char txt[24];
  char noteName[8];
  phxSeqNoteName(_seqNote[_seqPattern][tr][st], noteName, sizeof(noteName));

  // Compact aligned parameter area. The cursor arrow identifies the active editor.
  snprintf(txt, sizeof(txt), "TRK%u", (unsigned)(tr + 1));
  gui.text(x + 4, y + 11, txt, false);
  gui.text(x + 28, y + 11, (_seqParam == 4) ? ">" : " ", false);
  snprintf(txt, sizeof(txt), "LEN%02u", (unsigned)_seqLength[_seqPattern][tr]);
  gui.text(x + 34, y + 11, txt, false);
  gui.text(x + 63, y + 11, (_seqParam == 1) ? ">" : " ", false);
  snprintf(txt, sizeof(txt), "NOTE %s", noteName);
  gui.text(x + 69, y + 11, txt, false);

  gui.text(x + 4, y + 19, (_seqParam == 2) ? ">" : " ", false);
  snprintf(txt, sizeof(txt), "VEL  %3u", (unsigned)_seqVelocity[_seqPattern][tr][st]);
  gui.text(x + 10, y + 19, txt, false);
  const int barX = x + 58;
  const int barW = 38;
  const int barH = 5;
  gui.frame(barX, y + 19, barW, barH);
  int velFill = ((barW - 2) * _seqVelocity[_seqPattern][tr][st]) / 127;
  if (velFill > 0) gui.fill(barX + 1, y + 20, velFill, barH - 2, true);

  gui.text(x + 4, y + 27, (_seqParam == 3) ? ">" : " ", false);
  snprintf(txt, sizeof(txt), "GATE %3u%%", (unsigned)_seqGate[_seqPattern][tr][st]);
  gui.text(x + 10, y + 27, txt, false);
  gui.frame(barX, y + 27, barW, barH);
  int gateFill = ((barW - 2) * _seqGate[_seqPattern][tr][st]) / 100;
  if (gateFill > 0) gui.fill(barX + 1, y + 28, gateFill, barH - 2, true);

  // Two rows of eight steps. Filled = enabled, outer frame = edit cursor,
  // short underline = current playhead. No duplicated STEP xx ON/OFF text.
  const int gridX = x + 8;
  const int gridY = y + 37;
  const int cellW = 8;
  const int cellH = 6;
  const int gapX = 3;
  const int gapY = 4;

  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t col = i & 7;
    const uint8_t row = i >> 3;
    const int cx = gridX + col * (cellW + gapX);
    const int cy = gridY + row * (cellH + gapY);
    const bool inLength = i < _seqLength[_seqPattern][tr];
    const bool on = inLength && _seqOn[_seqPattern][tr][i];
    const bool selected = inLength && (_seqParam == 0) && (i == st);
    const bool playing = inLength && _seqRun && (i == (_seqPlayhead[tr] % _seqLength[_seqPattern][tr]));

    if (!inLength) {
      gui.hline(cx + 2, cy + (cellH / 2), cellW - 4);
    } else if (on) {
      gui.fill(cx + 1, cy + 1, cellW - 2, cellH - 2, true);
      gui.frame(cx, cy, cellW, cellH);
    } else {
      gui.frame(cx, cy, cellW, cellH);
    }

    if (selected) gui.frame(cx - 1, cy - 1, cellW + 2, cellH + 2);
    if (playing) gui.hline(cx + 1, cy + cellH + 1, cellW - 2);
  }

  if (_seqExternalClock)
    gui.drawClassicHelp("1-2T 3OBJ", "4PAT 5-6TR", _seqRun ? "7STOP 8BACK" : "7WAIT 8BACK");
  else
    gui.drawClassicHelp("1-2T 3OBJ", "4PAT 5-6TR", _seqRun ? "7STOP 8BACK" : "7PLAY 8BACK");
}


void PhoenixScreenManager::drawSongEditor(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;
  gui.frame(x,y,w,h);
  gui.text(x+4,y+2,"SONG",false);
  gui.text(x+58,y+2,_songPlaying?"PLAY":"READY",false);
  gui.hline(x+1,y+PhoenixLayout::TITLE_LINE_Y,w-2);

  const uint8_t first=(uint8_t)((_songEditPos/4U)*4U);
  for(uint8_t r=0;r<4;++r){
    const uint8_t i=(uint8_t)(first+r); const int yy=y+11+r*8; char line[24];
    const bool edit=i==_songEditPos; const bool play=_songPlaying&&i==_songPlayPos;
    // RTAL GUI rule: edit selection and playback position are independent.
    // The arrow marks the row being edited; a thin underline marks the row playing.
    gui.text(x+4,yy,edit?">":" ",false);
    if(_songEnd[i]) snprintf(line,sizeof(line),"%02u END",(unsigned)(i+1));
    else snprintf(line,sizeof(line),"%02u P%u x%02u",(unsigned)(i+1),(unsigned)(_songPattern[i]+1),(unsigned)_songRepeats[i]);
    gui.text(x+10,yy,line,false);
    if(play){
      gui.hline(x+10,yy+6,55);
      char rp[12];snprintf(rp,sizeof(rp),"%u/%u",(unsigned)(_songPlayRepeat+1),(unsigned)_songPlayRepeatTarget);
      gui.text(x+76,yy,rp,false);
    }
  }

  char bottom[28];
  if(_songEditField==0)snprintf(bottom,sizeof(bottom),"OBJ PAT/END");
  else if(_songEditField==1)snprintf(bottom,sizeof(bottom),"OBJ REPEAT");
  else if(_songEditField==2)snprintf(bottom,sizeof(bottom),"ACT %s",_songAction==0?"INSERT":"DELETE");
  else if(_songEditField==3)snprintf(bottom,sizeof(bottom),"LOOP START %02u",(unsigned)(_songLoopStart+1));
  else snprintf(bottom,sizeof(bottom),"LOOP END %02u",(unsigned)(_songLoopEnd+1));
  gui.text(x+4,y+44,bottom,false);
  char loopTxt[22];
  if(_songLoopMode==2) snprintf(loopTxt,sizeof(loopTxt),"LOOP PATTERN INF");
  else if(_songLoopMode==1) snprintf(loopTxt,sizeof(loopTxt),"LOOP SONG %02u-%02u",(unsigned)(_songLoopStart+1),(unsigned)(_songLoopEnd+1));
  else snprintf(loopTxt,sizeof(loopTxt),"LOOP OFF");
  gui.text(x+4,y+51,loopTxt,false);
  gui.drawClassicHelp("1-2POS", "3OBJ", _songPlaying ? "7STOP 8BACK" : "7PLAY 8BACK");
}

void PhoenixScreenManager::drawPatternManager(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);const int x=PhoenixLayout::WIN_X,y=PhoenixLayout::WIN_Y,w=PhoenixLayout::WIN_W,h=PhoenixLayout::WIN_H;gui.frame(x,y,w,h);gui.text(x+4,y+2,"PATTERN MANAGER",false);gui.hline(x+1,y+PhoenixLayout::TITLE_LINE_Y,w-2);
  char txt[24];
  snprintf(txt,sizeof(txt)," CURRENT     P%u",(unsigned)(_seqPattern+1));gui.text(x+6,y+14,txt,false);
  const char*act=_patternAction==0?"SELECT":(_patternAction==1?"COPY TO":"CLEAR");
  snprintf(txt,sizeof(txt),">ACTION      %s",act);gui.text(x+6,y+24,txt,false);
  snprintf(txt,sizeof(txt)," TARGET      P%u",(unsigned)(_patternTarget+1));gui.text(x+6,y+34,txt,false);
  if(_patternAction==1){snprintf(txt,sizeof(txt),"P%u -> P%u",(unsigned)(_seqPattern+1),(unsigned)(_patternTarget+1));gui.text(x+32,y+44,txt,false);}else if(_patternAction==2)gui.text(x+28,y+44,"F7 CLEAR",false);else gui.text(x+24,y+44,"F7 SELECT",false);
  gui.drawClassicHelp("1-2PAT","3ACT 4SNG","8BACK");
}

void PhoenixScreenManager::drawRecordingComplete(PhoenixGUI &gui) {
  gui.drawClassicLevelMeter(0);
  const int x = PhoenixLayout::WIN_X, y = PhoenixLayout::WIN_Y;
  char title[25];
  snprintf(title, sizeof(title), "S%u RECORDING COMPLETE", (unsigned)(_recordCompleteSlot + 1U));
  gui.drawWindow(x, y, PhoenixLayout::WIN_W, PhoenixLayout::WIN_H, title);
  char info[24];
  const uint32_t sr = _sampleRate ? _sampleRate : 32000U;
  const uint32_t sec = _sampleFrames / sr;
  const uint32_t tenth = ((_sampleFrames % sr) * 10UL) / sr;
  snprintf(info, sizeof(info), "%lu.%luS  %lu FRAMES", (unsigned long)sec, (unsigned long)tenth, (unsigned long)_sampleFrames);
  gui.text(x + 6, y + 17, info, false);
  if (_recordCompleteSaveState > 0) gui.text(x + 41, y + 29, "BANK SAVED", false);
  else if (_recordCompleteSaveState == 0) gui.text(x + 39, y + 29, "SAVE FAILED", false);
  else gui.text(x + 30, y + 29, "READY TO EDIT", false);
  gui.drawClassicHelp("5EDIT", "6PLAY 7SAVE", "8BACK");
}

void PhoenixScreenManager::drawOverwritePopup(PhoenixGUI &gui) {
  char title[24];
  snprintf(title, sizeof(title), "S%u CONTAINS SAMPLE", (unsigned)(_overwriteSlot + 1));

  static const char * const lines[] = {
    "F7 OVERWRITE",
    "F8 CANCEL"
  };
  gui.drawDialog(title, lines, 2, "");
}

void PhoenixScreenManager::draw(PhoenixGUI &gui) {
  gui.clear();

  switch (_screen) {
    case SCR_BOOT:
      gui.drawBoot();
      return;

    case SCR_SELFTEST:
      gui.drawClassicMenu("HARDWARE SELF TEST", SELF_ITEMS, 5, _sel, true);
      gui.text(29, 42, _psramOk ? "PSRAM OK" : "PSRAM FAIL", false);
      gui.text(75, 42, _audioOk ? "I2S OK" : "I2S FAIL", false);
      gui.drawClassicHelp("1UP", "3DOWN", "7SELECT");
      break;

    case SCR_AUDIO_OUT: {
      gui.drawClassicLevelMeter(0);
      gui.drawWindow(PhoenixLayout::WIN_X, PhoenixLayout::WIN_Y, PhoenixLayout::WIN_W, PhoenixLayout::WIN_H, "AUDIO TEST");

      const int x = PhoenixLayout::WIN_X + 8;
      const int y = PhoenixLayout::WIN_Y + 14;
      const int rowH = 8;
      char line[32];

      for (uint8_t i = 0; i < 4; ++i) {
        int yy = y + i * rowH;
        gui.text(x, yy, (i == _audioTestParam) ? ">" : " ", false);
        if (i == 0) {
          snprintf(line, sizeof(line), "WAVEFORM  %s", phxAudioWaveName(_audioTestWaveform));
        } else if (i == 1) {
          snprintf(line, sizeof(line), "FREQUENCY %u HZ", (unsigned)_audioTestFreqHz);
        } else if (i == 2) {
          snprintf(line, sizeof(line), "LEVEL     %d DB", (int)_audioTestLevelDb);
        } else {
          snprintf(line, sizeof(line), "OUTPUT    %s", _audioTestOutput ? "ON" : "OFF");
        }
        gui.text(x + 6, yy, line, false);
      }

      if (_audioTestEditMode) gui.text(104, 3, "EDIT", false);
      gui.drawClassicHelp("1UP", _audioTestOutput ? "F7-OFF" : "F7-ON", "8BACK");
      break;
    }

    case SCR_AUDIO_IN: {
      gui.drawClassicLevelMeter(0);
      gui.drawWindow(25, 0, 102, PhoenixLayout::WIN_H, "AUDIO INPUT TEST");
      char line[24];
      snprintf(line, sizeof(line), "CLIPS %04lu", (unsigned long)(_clipCount % 10000UL));
      gui.text(31, 12, line, false);
      snprintf(line, sizeof(line), "DC L %+d", (int)_dcL);
      gui.text(31, 20, line, false);
      snprintf(line, sizeof(line), "DC R %+d", (int)_dcR);
      gui.text(31, 28, line, false);
      snprintf(line, sizeof(line), "DMA ERR %lu", (unsigned long)(_underruns % 100000UL));
      gui.text(31, 36, line, false);
      snprintf(line, sizeof(line), "MIDI %lu", (unsigned long)(_midiBytes % 100000UL));
      gui.text(31, 46, line, false);
      gui.drawClassicHelp("", "", "F8 EXIT");
      break;
    }

    case SCR_BUTTONS: {
      gui.drawClassicLevelMeter(0);
      gui.drawWindow(25, 0, 102, PhoenixLayout::WIN_H, "BUTTON TEST");
      for (uint8_t i = 0; i < 4; ++i) {
        char s[12];
        snprintf(s, sizeof(s), "F%u %s", (unsigned)(i + 1), _btnSeen[i] ? "OK" : "--");
        gui.text(31, 12 + i * 7, s, false);
      }
      for (uint8_t i = 4; i < 8; ++i) {
        char s[12];
        snprintf(s, sizeof(s), "F%u %s", (unsigned)(i + 1), _btnSeen[i] ? "OK" : "--");
        gui.text(75, 12 + (i - 4) * 7, s, false);
      }
      gui.text(31, 43, _encSeen ? "ENC OK" : "ENC --", false);
      gui.text(75, 43, _encSwSeen ? "SW OK" : "SW --", false);
      gui.drawClassicHelp("", "", "F8 EXIT");
      break;
    }

    case SCR_USB_STORAGE: {
      gui.drawClassicLevelMeter(0);
      const int x=PhoenixLayout::WIN_X, y=PhoenixLayout::WIN_Y;
      gui.drawWindow(x,y,PhoenixLayout::WIN_W,PhoenixLayout::WIN_H,"USB MASS STORAGE");
      if (_usbStorageActive) {
        gui.text(x+6,y+12,"PHOENIX SD CARD",false);
        gui.text(x+6,y+21,_usbStorageWritable ? "READ / WRITE" : "READ ONLY",false);
        if (_usbStorageExitConfirm) {
          gui.text(x+6,y+30,"WINDOWS DRIVE EJECTED?",false);
          gui.text(x+6,y+39,"CONFIRM ONLY IF SAFE",false);
          gui.text(x+6,y+48,"F7 YES   F8 CANCEL",false);
          gui.drawClassicHelp("","7YES","8NO");
        } else {
          gui.text(x+6,y+30,_usbStorageWritable ? "EJECT ON PC FIRST" : "COPY FILES ON PC",false);
          gui.text(x+6,y+39,_usbStorageEjected ? "SAFE TO RETURN" : "PC HAS SD CONTROL",false);
          gui.text(x+6,y+48,_usbStorageStatus,false);
          // In RW mode F8 now opens a guarded manual confirmation if automatic
          // host-eject detection is unavailable; it is no longer a dead-end lock.
          gui.drawClassicHelp("","",(!_usbStorageWritable || _usbStorageEjected) ? "8EXIT" : "8CHECK");
        }
      } else {
        gui.text(x+6,y+12,_usbStorageAvailable ? "MODE" : "USB OTG REQUIRED",false);
        gui.text(x+34,y+12,_usbStorageReadWriteSelected ? "READ / WRITE" : "READ ONLY",false);
        gui.text(x+6,y+23,_usbStorageReadWriteSelected ? "PC MAY CHANGE SD" : "SAFE COPY MODE",false);
        gui.text(x+6,y+34,"AUDIO WILL STOP",false);
        gui.text(x+6,y+45,_usbStorageStatus,false);
        gui.drawClassicHelp("5MODE","","7START 8BACK");
      }
      break;
    }

    case SCR_DIAGNOSTICS: {
      gui.drawClassicLevelMeter(0);
      const int x = PhoenixLayout::WIN_X, y = PhoenixLayout::WIN_Y;
      gui.drawWindow(x, y, PhoenixLayout::WIN_W, PhoenixLayout::WIN_H, "SYSTEM DIAGNOSTICS");
      char line[28];
      const uint32_t sec = _diagElapsedMs / 1000UL;
      snprintf(line, sizeof(line), "TIME %02lu:%02lu:%02lu",
               (unsigned long)((sec / 3600UL) % 100UL),
               (unsigned long)((sec / 60UL) % 60UL),
               (unsigned long)(sec % 60UL));
      gui.text(x + 5, y + 11, line, false);
      snprintf(line, sizeof(line), "VOICE %02u/%02u PEAK %02u",
               (unsigned)_diagVoices, (unsigned)_diagVoiceCapacity, (unsigned)_diagVoicePeak);
      gui.text(x + 5, y + 18, line, false);
      snprintf(line, sizeof(line), "AUDIO %4lu P%4lu US",
               (unsigned long)(_diagAudioAvgUs % 10000UL),
               (unsigned long)(_diagAudioPeakUs % 10000UL));
      gui.text(x + 5, y + 25, line, false);
      snprintf(line, sizeof(line), "RISK %04lu OVR %04lu",
               (unsigned long)(_diagRiskCount % 10000UL),
               (unsigned long)(_diagOverrunCount % 10000UL));
      gui.text(x + 5, y + 32, line, false);
      snprintf(line, sizeof(line), "DMA %04lu STL %04lu",
               (unsigned long)(_diagUnderruns % 10000UL),
               (unsigned long)(_diagVoiceSteals % 10000UL));
      gui.text(x + 5, y + 39, line, false);
      snprintf(line, sizeof(line), "H%4lu M%4lu P%4luK",
               (unsigned long)(_diagHeapFreeKb % 10000UL),
               (unsigned long)(_diagHeapMinKb % 10000UL),
               (unsigned long)(_diagPsramFreeKb % 10000UL));
      gui.text(x + 5, y + 46, line, false);
      if (_diagnosticsStatusUntilMs && (int32_t)(millis() - _diagnosticsStatusUntilMs) < 0)
        gui.text(113, 3, _diagnosticsLastAction == 2U ? "LOG" : "RST", false);
      gui.drawClassicHelp("1RESET", "", "7LOG 8BACK");
      break;
    }

    case SCR_MAIN: {
      const char *items[8] = {
        "SOUND SAMPLING",
        "QUATTRO SAMPLING",
        "PITCH CONVERTER",
        "ECHO",
        "DISK UTILITIES",
        "VINTAGE SAMPLER",
        "STEP SEQUENCER",
        "MIDI CONTROL"
      };
      gui.drawClassicMenu("PHOENIX SOUND SAMPLER", items, 8, _sel, true);
      gui.drawClassicHelp("1UP", "2DIAG 3DN", "7SELECT");
      break;
    }

    case SCR_SOUND:
      drawSoundSampling(gui);
      break;

    case SCR_PITCH:
      drawPitchConverter(gui);
      break;

    case SCR_ENVELOPE:
      drawEnvelope(gui);
      break;

    case SCR_ECHO:
      drawEcho(gui);
      break;

    case SCR_DISK:
      drawDiskUtilities(gui);
      break;

    case SCR_SAMPLE_BROWSER:
      drawSampleBrowser(gui);
      break;

    case SCR_LOOP:
      drawLoop(gui);
      break;

    case SCR_TRIGGER:
      drawTriggerLevel(gui);
      break;

    case SCR_VINTAGE:
      drawVintageSampler(gui);
      break;

    case SCR_MIDI_TYPE:
      drawMidiType(gui);
      break;

    case SCR_MIDI_CH:
      drawMidiChannel(gui);
      break;
    case SCR_MIDI_MONITOR:
      drawMidiMonitor(gui);
      break;

    case SCR_SEQUENCER:
      drawSequencer(gui);
      break;
    case SCR_PATTERN_MANAGER:
      drawPatternManager(gui);
      break;
    case SCR_SONG_EDITOR:
      drawSongEditor(gui);
      break;

    case SCR_QUATTRO:
      drawQuattro(gui);
      break;

    case SCR_KEYBOARD:
      drawQuattroKeyboard(gui);
      break;

    case SCR_VOICE:
      drawVoiceAllocation(gui);
      break;
    case SCR_MIXER:
      drawQuattroMixer(gui);
      break;
    case SCR_FILTER:
      drawQuattroFilter(gui);
      break;
    case SCR_FILTER_ENV:
      drawFilterEnvelope(gui);
      break;
    case SCR_MULTISAMPLE:
      drawMultisampleEditor(gui);
      break;

    case SCR_SAMPLE_EDITOR:
      drawSampleEditor(gui);
      break;

    case SCR_RECORD_COMPLETE:
      drawRecordingComplete(gui);
      break;

    case SCR_WAVE: {
      uint16_t posQ = 0U;
      if (_sampleFrames > 1U) {
        uint32_t frame = _playing ? _playFrames : _waveCursorFrame;
        if (frame >= _sampleFrames) frame = _sampleFrames - 1U;
        posQ = (uint16_t)(((uint64_t)frame * 10000ULL) / (uint64_t)(_sampleFrames - 1U));
      }
      gui.setWaveformView(_waveZoom, posQ);
      gui.drawClassicWaveform("DRAW WAVEFORM", posQ);
      gui.setWaveformView(1, 5000);
      if (_slotLoop[_selectedSlot & 3]) gui.text(PhoenixLayout::WIN_X + PhoenixLayout::WIN_W - 14, PhoenixLayout::WIN_Y + 2, "LP", false);

      // v0.6.3: time display directly in the Waveform Editor.
      // Shows current cursor/playback position and total sample length.
      const int x = PhoenixLayout::WIN_X;
      const int y = PhoenixLayout::WIN_Y;
      char timeLine[24];
      uint32_t sr = phxNonZero(_sampleRate);
      uint32_t curFrame = _playing ? _playFrames : _waveCursorFrame;
      if (_sampleFrames > 0 && curFrame >= _sampleFrames) curFrame = _sampleFrames - 1;
      uint32_t curSec = curFrame / sr;
      uint32_t curCs  = ((curFrame % sr) * 100UL) / sr;
      uint32_t lenSec = _sampleFrames / sr;
      uint32_t lenCs  = ((_sampleFrames % sr) * 100UL) / sr;
      if (_sampleReady && _sampleFrames > 0) {
        if ((_playing && _playingReverse) || (!_playing && _waveReverseMode)) snprintf(timeLine, sizeof(timeLine), "REV  %02lu.%02lu/%02lu.%02lu",
                              (unsigned long)(curSec % 100), (unsigned long)curCs,
                              (unsigned long)(lenSec % 100), (unsigned long)lenCs);
        else if (_playing) snprintf(timeLine, sizeof(timeLine), "PLAY %02lu.%02lu/%02lu.%02lu",
                              (unsigned long)(curSec % 100), (unsigned long)curCs,
                              (unsigned long)(lenSec % 100), (unsigned long)lenCs);
        else snprintf(timeLine, sizeof(timeLine), "CUR  %02lu.%02lu/%02lu.%02lu",
                      (unsigned long)(curSec % 100), (unsigned long)curCs,
                      (unsigned long)(lenSec % 100), (unsigned long)lenCs);
      } else {
        snprintf(timeLine, sizeof(timeLine), "NO SAMPLE");
      }
      gui.fill(x + 4, y + 45, PhoenixLayout::WIN_W - 8, 7, false);
      gui.text(x + 6, y + 47, timeLine, false);

      char zoomLabel[10];
      if (_waveZoom <= 1) snprintf(zoomLabel, sizeof(zoomLabel), "F1-FIT");
      else snprintf(zoomLabel, sizeof(zoomLabel), "F1-X%u", (unsigned)_waveZoom);
      if (_playing || _recording) gui.drawClassicHelp(zoomLabel, "PUSH/7STOP", "8BACK");
      else gui.drawClassicHelp(zoomLabel, "PUSH/7PLAY", "8BACK");
      break;
    }
  }

  if (_about) gui.drawAboutPopup();
  if (_overwriteConfirm) drawOverwritePopup(gui);
  if (_midiLearnActive) drawMidiLearnPopup(gui);
  // v0.7.41f: place the activity marker in the free header area between the
  // centered title and the right frame.  The clear box stays fully inside the
  // window and no longer erases the final title character or the border.
  if (phxMidiActivityActive()) { gui.fill(118,1,7,7,false); gui.text(120,2,"M",false); }
  gui.send();
}

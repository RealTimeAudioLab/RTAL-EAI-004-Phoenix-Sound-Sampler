#include "../PhoenixSystem/PhoenixSerial.h"
#include "PhoenixAudioManager.h"

// C034 keeps the C029e stable O3 audio baseline unchanged.
#pragma GCC optimize ("O3")

static constexpr uint8_t PHX_DEFAULT_POLY_VOICE_LIMIT = 12;
static constexpr bool PHX_ENABLE_VOICE_AUDIT = false;
// C034: production default remains profiler OFF; set to 1 only for targeted DSP profiling.
#ifndef PHX_DSP_PROFILER_ENABLED
#define PHX_DSP_PROFILER_ENABLED 0
#endif
static constexpr bool PHX_ENABLE_DSP_PROFILER = (PHX_DSP_PROFILER_ENABLED != 0);
static constexpr uint8_t PHX_DSP_PROFILE_EVERY_N_BLOCKS = 32;
#include "driver/i2s.h"
#include <math.h>
#include <SD.h>
#include <new>
#include "esp_cpu.h"

#ifndef I2S_COMM_FORMAT_STAND_I2S
  #define PHX_I2S_COMM_FORMAT ((i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB))
#else
  #define PHX_I2S_COMM_FORMAT I2S_COMM_FORMAT_STAND_I2S
#endif

static inline uint32_t phxAbs32(int32_t v) {
  if (v == INT32_MIN) return 0x7FFFFFFFUL;
  return (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
}

static inline int16_t phxClamp16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

// v0.7.19d: soft master limiter with a gentle knee at -2.5 dBFS.
// Signals below the knee pass unchanged. Peaks above the knee are compressed
// smoothly towards full scale instead of being hard-clipped.
static inline int16_t phxSoftLimit16(int32_t v) {
  const uint32_t threshold = 24576U; // 75 % of full scale
  const uint32_t kneeRange = 8192U;

  const bool negative = (v < 0);
  uint32_t magnitude;
  if (v == INT32_MIN) magnitude = 0x7FFFFFFFUL;
  else magnitude = negative ? (uint32_t)(-v) : (uint32_t)v;

  if (magnitude <= threshold) return (int16_t)v;

  const uint32_t over = magnitude - threshold;
  uint32_t limited = threshold + (uint32_t)(((uint64_t)over * kneeRange) / (over + kneeRange));
  if (limited > 32767U) limited = 32767U;

  return negative ? (int16_t)(-(int32_t)limited) : (int16_t)limited;
}

static const int16_t PHX_SINE_1024_Q15[1024] = {
       0,    201,    402,    603,    804,   1005,   1206,   1407,   1608,   1809,   2009,   2210,   2410,   2611,   2811,   3012,
    3212,   3412,   3612,   3811,   4011,   4210,   4410,   4609,   4808,   5007,   5205,   5404,   5602,   5800,   5998,   6195,
    6393,   6590,   6786,   6983,   7179,   7375,   7571,   7767,   7962,   8157,   8351,   8545,   8739,   8933,   9126,   9319,
    9512,   9704,   9896,  10087,  10278,  10469,  10659,  10849,  11039,  11228,  11417,  11605,  11793,  11980,  12167,  12353,
   12539,  12725,  12910,  13094,  13279,  13462,  13645,  13828,  14010,  14191,  14372,  14553,  14732,  14912,  15090,  15269,
   15446,  15623,  15800,  15976,  16151,  16325,  16499,  16673,  16846,  17018,  17189,  17360,  17530,  17700,  17869,  18037,
   18204,  18371,  18537,  18703,  18868,  19032,  19195,  19357,  19519,  19680,  19841,  20000,  20159,  20317,  20475,  20631,
   20787,  20942,  21096,  21250,  21403,  21554,  21705,  21856,  22005,  22154,  22301,  22448,  22594,  22739,  22884,  23027,
   23170,  23311,  23452,  23592,  23731,  23870,  24007,  24143,  24279,  24413,  24547,  24680,  24811,  24942,  25072,  25201,
   25329,  25456,  25582,  25708,  25832,  25955,  26077,  26198,  26319,  26438,  26556,  26674,  26790,  26905,  27019,  27133,
   27245,  27356,  27466,  27575,  27683,  27790,  27896,  28001,  28105,  28208,  28310,  28411,  28510,  28609,  28706,  28803,
   28898,  28992,  29085,  29177,  29268,  29358,  29447,  29534,  29621,  29706,  29791,  29874,  29956,  30037,  30117,  30195,
   30273,  30349,  30424,  30498,  30571,  30643,  30714,  30783,  30852,  30919,  30985,  31050,  31113,  31176,  31237,  31297,
   31356,  31414,  31470,  31526,  31580,  31633,  31685,  31736,  31785,  31833,  31880,  31926,  31971,  32014,  32057,  32098,
   32137,  32176,  32213,  32250,  32285,  32318,  32351,  32382,  32412,  32441,  32469,  32495,  32521,  32545,  32567,  32589,
   32609,  32628,  32646,  32663,  32678,  32692,  32705,  32717,  32728,  32737,  32745,  32752,  32757,  32761,  32765,  32766,
   32767,  32766,  32765,  32761,  32757,  32752,  32745,  32737,  32728,  32717,  32705,  32692,  32678,  32663,  32646,  32628,
   32609,  32589,  32567,  32545,  32521,  32495,  32469,  32441,  32412,  32382,  32351,  32318,  32285,  32250,  32213,  32176,
   32137,  32098,  32057,  32014,  31971,  31926,  31880,  31833,  31785,  31736,  31685,  31633,  31580,  31526,  31470,  31414,
   31356,  31297,  31237,  31176,  31113,  31050,  30985,  30919,  30852,  30783,  30714,  30643,  30571,  30498,  30424,  30349,
   30273,  30195,  30117,  30037,  29956,  29874,  29791,  29706,  29621,  29534,  29447,  29358,  29268,  29177,  29085,  28992,
   28898,  28803,  28706,  28609,  28510,  28411,  28310,  28208,  28105,  28001,  27896,  27790,  27683,  27575,  27466,  27356,
   27245,  27133,  27019,  26905,  26790,  26674,  26556,  26438,  26319,  26198,  26077,  25955,  25832,  25708,  25582,  25456,
   25329,  25201,  25072,  24942,  24811,  24680,  24547,  24413,  24279,  24143,  24007,  23870,  23731,  23592,  23452,  23311,
   23170,  23027,  22884,  22739,  22594,  22448,  22301,  22154,  22005,  21856,  21705,  21554,  21403,  21250,  21096,  20942,
   20787,  20631,  20475,  20317,  20159,  20000,  19841,  19680,  19519,  19357,  19195,  19032,  18868,  18703,  18537,  18371,
   18204,  18037,  17869,  17700,  17530,  17360,  17189,  17018,  16846,  16673,  16499,  16325,  16151,  15976,  15800,  15623,
   15446,  15269,  15090,  14912,  14732,  14553,  14372,  14191,  14010,  13828,  13645,  13462,  13279,  13094,  12910,  12725,
   12539,  12353,  12167,  11980,  11793,  11605,  11417,  11228,  11039,  10849,  10659,  10469,  10278,  10087,   9896,   9704,
    9512,   9319,   9126,   8933,   8739,   8545,   8351,   8157,   7962,   7767,   7571,   7375,   7179,   6983,   6786,   6590,
    6393,   6195,   5998,   5800,   5602,   5404,   5205,   5007,   4808,   4609,   4410,   4210,   4011,   3811,   3612,   3412,
    3212,   3012,   2811,   2611,   2410,   2210,   2009,   1809,   1608,   1407,   1206,   1005,    804,    603,    402,    201,
       0,   -201,   -402,   -603,   -804,  -1005,  -1206,  -1407,  -1608,  -1809,  -2009,  -2210,  -2410,  -2611,  -2811,  -3012,
   -3212,  -3412,  -3612,  -3811,  -4011,  -4210,  -4410,  -4609,  -4808,  -5007,  -5205,  -5404,  -5602,  -5800,  -5998,  -6195,
   -6393,  -6590,  -6786,  -6983,  -7179,  -7375,  -7571,  -7767,  -7962,  -8157,  -8351,  -8545,  -8739,  -8933,  -9126,  -9319,
   -9512,  -9704,  -9896, -10087, -10278, -10469, -10659, -10849, -11039, -11228, -11417, -11605, -11793, -11980, -12167, -12353,
  -12539, -12725, -12910, -13094, -13279, -13462, -13645, -13828, -14010, -14191, -14372, -14553, -14732, -14912, -15090, -15269,
  -15446, -15623, -15800, -15976, -16151, -16325, -16499, -16673, -16846, -17018, -17189, -17360, -17530, -17700, -17869, -18037,
  -18204, -18371, -18537, -18703, -18868, -19032, -19195, -19357, -19519, -19680, -19841, -20000, -20159, -20317, -20475, -20631,
  -20787, -20942, -21096, -21250, -21403, -21554, -21705, -21856, -22005, -22154, -22301, -22448, -22594, -22739, -22884, -23027,
  -23170, -23311, -23452, -23592, -23731, -23870, -24007, -24143, -24279, -24413, -24547, -24680, -24811, -24942, -25072, -25201,
  -25329, -25456, -25582, -25708, -25832, -25955, -26077, -26198, -26319, -26438, -26556, -26674, -26790, -26905, -27019, -27133,
  -27245, -27356, -27466, -27575, -27683, -27790, -27896, -28001, -28105, -28208, -28310, -28411, -28510, -28609, -28706, -28803,
  -28898, -28992, -29085, -29177, -29268, -29358, -29447, -29534, -29621, -29706, -29791, -29874, -29956, -30037, -30117, -30195,
  -30273, -30349, -30424, -30498, -30571, -30643, -30714, -30783, -30852, -30919, -30985, -31050, -31113, -31176, -31237, -31297,
  -31356, -31414, -31470, -31526, -31580, -31633, -31685, -31736, -31785, -31833, -31880, -31926, -31971, -32014, -32057, -32098,
  -32137, -32176, -32213, -32250, -32285, -32318, -32351, -32382, -32412, -32441, -32469, -32495, -32521, -32545, -32567, -32589,
  -32609, -32628, -32646, -32663, -32678, -32692, -32705, -32717, -32728, -32737, -32745, -32752, -32757, -32761, -32765, -32766,
  -32767, -32766, -32765, -32761, -32757, -32752, -32745, -32737, -32728, -32717, -32705, -32692, -32678, -32663, -32646, -32628,
  -32609, -32589, -32567, -32545, -32521, -32495, -32469, -32441, -32412, -32382, -32351, -32318, -32285, -32250, -32213, -32176,
  -32137, -32098, -32057, -32014, -31971, -31926, -31880, -31833, -31785, -31736, -31685, -31633, -31580, -31526, -31470, -31414,
  -31356, -31297, -31237, -31176, -31113, -31050, -30985, -30919, -30852, -30783, -30714, -30643, -30571, -30498, -30424, -30349,
  -30273, -30195, -30117, -30037, -29956, -29874, -29791, -29706, -29621, -29534, -29447, -29358, -29268, -29177, -29085, -28992,
  -28898, -28803, -28706, -28609, -28510, -28411, -28310, -28208, -28105, -28001, -27896, -27790, -27683, -27575, -27466, -27356,
  -27245, -27133, -27019, -26905, -26790, -26674, -26556, -26438, -26319, -26198, -26077, -25955, -25832, -25708, -25582, -25456,
  -25329, -25201, -25072, -24942, -24811, -24680, -24547, -24413, -24279, -24143, -24007, -23870, -23731, -23592, -23452, -23311,
  -23170, -23027, -22884, -22739, -22594, -22448, -22301, -22154, -22005, -21856, -21705, -21554, -21403, -21250, -21096, -20942,
  -20787, -20631, -20475, -20317, -20159, -20000, -19841, -19680, -19519, -19357, -19195, -19032, -18868, -18703, -18537, -18371,
  -18204, -18037, -17869, -17700, -17530, -17360, -17189, -17018, -16846, -16673, -16499, -16325, -16151, -15976, -15800, -15623,
  -15446, -15269, -15090, -14912, -14732, -14553, -14372, -14191, -14010, -13828, -13645, -13462, -13279, -13094, -12910, -12725,
  -12539, -12353, -12167, -11980, -11793, -11605, -11417, -11228, -11039, -10849, -10659, -10469, -10278, -10087,  -9896,  -9704,
   -9512,  -9319,  -9126,  -8933,  -8739,  -8545,  -8351,  -8157,  -7962,  -7767,  -7571,  -7375,  -7179,  -6983,  -6786,  -6590,
   -6393,  -6195,  -5998,  -5800,  -5602,  -5404,  -5205,  -5007,  -4808,  -4609,  -4410,  -4210,  -4011,  -3811,  -3612,  -3412,
   -3212,  -3012,  -2811,  -2611,  -2410,  -2210,  -2009,  -1809,  -1608,  -1407,  -1206,  -1005,   -804,   -603,   -402,   -201
};

static const int32_t PHX_TONE_AMP_Q15 = 8192; // -12 dBFS reference level

static inline int32_t phxToneToI2S(int32_t q15, int32_t ampQ15 = PHX_TONE_AMP_Q15) {
  int32_t scaled = (q15 * ampQ15) >> 15;
  return scaled << 16;
}

static inline int32_t phxDbToAmpQ15(int8_t db) {
  if (db > -3) db = -3;
  if (db < -60) db = -60;
  float a = powf(10.0f, (float)db / 20.0f);
  int32_t q = (int32_t)(a * 32767.0f + 0.5f);
  if (q < 1) q = 1;
  if (q > 32767) q = 32767;
  return q;
}

static const uint32_t PHX_PITCH_INC_Q16[49] = {
  16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548,
  26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968,
  41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
  65536, 69433, 73562, 77936, 82570, 87481, 92682, 98193,
  104032, 110218, 116772, 123716, 131072, 138866, 147124, 155872,
  165140, 174963, 185364, 196386, 208064, 220436, 233544, 247432,
  262144
};

PhoenixAudioManager::PhoenixAudioManager(int bclkPin, int lrckPin, int doutPin, int dinPin)
: _bclkPin(bclkPin), _lrckPin(lrckPin), _doutPin(doutPin), _dinPin(dinPin),
  _testTone(TONE_THRU), _state(AUDIO_THRU), _peakLevel(0), _peakHoldLevel(0), _lastPeakLevel(0),
  _clipActive(false), _highActive(false), _audioOk(false), _psramOk(false),
  _psramFreeKb(0), _frameCounter(0), _underruns(0), _lastPeakRaw(0), _clipCount(0),
  _dcOffsetL(0), _dcOffsetR(0),
  _lastTickMs(0), _clipHoldUntilMs(0), _highHoldUntilMs(0), _peakHoldUntilMs(0),
  _peakReleaseMs(0), _phase(0), _noise(0x12345678UL), _testFrequencyHz(440), _testLevelDb(-12), _testAmpQ15(PHX_TONE_AMP_Q15),
  _keygroups(nullptr), _selectedSlot(0), _recordSlot(0), _playSlot(0), _recordPos(0), _preTriggerWrite(0), _preTriggerCount(0), _playPos(0),
  _playPosQ16(0), _playIncQ16(65536UL), _playRangeActive(false), _playRangeStart(0), _playRangeEnd(0), _transportLoopForward(true), _lastReplayReverse(false), _triggerAuto(false), _triggerLevel(4), _pitchSemitone(0),
  _previewBuffer(nullptr), _previewCapacityFrames(0), _previewFrames(0), _previewPos(0),
  _activeVoiceCount(0), _activeVoicePeak(0), _voiceAgeCounter(0), _heldAgeCounter(0),
  _voiceStealCount(0), _duplicateNoteOnCount(0), _orphanNoteOffCount(0), _sustainDeferredNoteOffCount(0), _panicKillCount(0), _lastAllocatorScanMax(0), _highestAllocatorScanMax(0), _lastAllocatedVoice(255), _lastAllocationReason(0), _voiceAuditPrintMs(0),
  _audioLoadAvgUs(0), _audioLoadMaxUs(0), _audioLoadLastUs(0), _audioRiskCount(0), _audioOverrunCount(0), _audioLoadPeakUs(0), _audioLoadAccumUs(0), _audioLoadBlocks(0), _audioLoadWindowStartMs(0), _audioStatsPrintMs(0),
  _dspProfileSamples(0), _dspSetupEnvUs(0), _dspPrepUs(0), _dspEnvelopeUs(0), _dspFetchXfadeUs(0),
  _dspVintageUs(0), _dspFilterUs(0), _dspGainMixUs(0), _dspPositionUs(0), _dspMixPosUs(0), _dspEchoOutUs(0), _dspProfilerPrintMs(0), _dspProfileDivider(0),
  _transportVintagePhase(0), _transportVintageHold(0), _transportVintageFilterState(0), _transportVintageNoise(0x13579BDFUL),
  _echoBuffer(nullptr), _echoCapacityFrames(0), _echoDelayMs(250), _echoFeedback(35), _echoMix(20), _echoWritePos(0),
  _echoDelayFramesCurrent(8000), _echoDelayFramesXfadeTo(8000), _echoDelayXfadePos(0), _echoDelayXfadeActive(false),
  _echoFeedbackQ8Current(35 * 256), _echoMixQ8Current(20 * 256),
  _echoLimitCount(0), _filterGuardCount(0), _echoParamSlewCount(0),
  _msNoteMissCount(0), _msVelocityMissCount(0), _msRrFallbackCount(0), _msSanitizeCount(0),
  _echoTailActive(false), _echoTailQuietFrames(0),
  _progressCallback(nullptr), _progressContext(nullptr), _progressBase(0), _progressSpan(100),
  _audioTaskHandle(nullptr) {
  _quattroMode = 1;
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) { _slotVoicePlayheadActive[i] = false; _slotVoicePlayheadFrame[i] = 0; _slotVoicePlayheadVoice[i] = -1; _slotVoicePlayheadAge[i] = 0; _slotMarkerRevision[i] = 1; _slotCoarse[i] = 0; _slotFine[i] = 0; _slotRoot[i] = 60; _slotPitchBendRange[i] = 2; _slotPitchBend[i] = 0; _slotOctave[i] = 0; _slotPitchTrack[i] = true; _slotAttackMs[i] = 5; _slotDecayMs[i] = 80; _slotSustainPct[i] = 100; _slotReleaseMs[i] = 80; _slotSustainDown[i] = false; _slotVintagePreset[i] = 0; _slotVintageSampleRate[i] = 0; _slotVintageBitDepth[i] = 0; _slotVintageFilter[i] = 0; _slotVintageJitter[i] = 0; _slotEchoSend[i] = 0; _slotSampleGainPct[i]=100; _slotVelocityAmount[i]=100; _slotLevel[i]=100; _slotPan[i]=0; _slotGainLQ15[i]=23170; _slotGainRQ15[i]=23170; _slotFilterCutoff[i]=100; _slotFilterResonance[i]=0; _slotFilterEnvAmount[i]=0; _slotFilterVelocityAmount[i]=0; _slotFilterKeytrack[i]=0; _slotFilterAttackMs[i]=0; _slotFilterDecayMs[i]=250; _slotFilterSustainPct[i]=0; _slotFilterReleaseMs[i]=300; _quattroKeyLow[i] = i * 32; _quattroKeyHigh[i] = (i == 3) ? 127 : (i * 32 + 31); _quattroMidiChannel[i] = i + 1; }
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) { _voices[i] = InstrumentVoice(); _voiceAllocCount[i] = 0; }
  for(uint8_t t=0;t<4;++t) for(uint8_t g=0;g<SEQ_GATES_PER_TRACK;++g){ _seqAudioGate[t][g].active=false; _seqAudioGate[t][g].voiceId=-1; _seqAudioGate[t][g].ownershipToken=0; _seqAudioGate[t][g].framesLeft=0; _seqAudioGate[t][g].eventId=0; }
  for (uint8_t i = 0; i <= VOICE_COUNT; ++i) { _voiceHistCount[i] = 0; _voiceHistSumUs[i] = 0; _voiceHistMaxUs[i] = 0; }
  // Safe defaults before any BANK.CFG is loaded.
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) { _slotVoiceMode[i]=0; _slotVoiceLimit[i]=PHX_DEFAULT_POLY_VOICE_LIMIT; _slotNotePriority[i]=0; _slotGlideMs[i]=0; }
  memset(_heldNotes, 0, sizeof(_heldNotes));
  memset(_heldVelocity, 0, sizeof(_heldVelocity));
  memset(_heldAge, 0, sizeof(_heldAge));
  memset(_noteHoldCount, 0, sizeof(_noteHoldCount));
}


void PhoenixAudioManager::reportProgress(uint8_t percent) {
  if (!_progressCallback) return;
  if (percent > 100) percent = 100;
  uint16_t mapped = (uint16_t)_progressBase + ((uint16_t)_progressSpan * percent) / 100U;
  if (mapped > 100U) mapped = 100U;
  _progressCallback((uint8_t)mapped, _progressContext);
}

void PhoenixAudioManager::begin() {
  _testTone = TONE_THRU;
  _state = AUDIO_THRU;
  _peakLevel = 0;
  _peakHoldLevel = 0;
  _lastPeakLevel = 0;
  _clipActive = false;
  _highActive = false;
  _frameCounter = 0;
  _underruns = 0;
  _lastPeakRaw = 0;
  _clipCount = 0;
  _dcOffsetL = 0;
  _dcOffsetR = 0;
  _recordPos = 0;
  _preTriggerWrite = 0;
  _preTriggerCount = 0;
  _playPos = 0;
  _previewFrames = 0;
  _previewPos = 0;
  _playPosQ16 = 0;
  _playIncQ16 = 65536UL;
  _playRangeActive = false;
  _playRangeStart = 0;
  _playRangeEnd = 0;
  _transportLoopForward = true;
  _lastReplayReverse = false;
  _triggerAuto = false;
  _triggerLevel = 4;
  _pitchSemitone = 0;
  _activeVoiceCount = 0;
  _activeVoicePeak = 0;
  _voiceAgeCounter = 0;
  _voiceStealCount = 0; _duplicateNoteOnCount = 0; _orphanNoteOffCount = 0;
  _sustainDeferredNoteOffCount = 0; _panicKillCount = 0; _lastAllocatorScanMax = 0; _highestAllocatorScanMax = 0;
  _lastAllocatedVoice = 255; _lastAllocationReason = 0; _voiceAuditPrintMs = millis();
  for (uint8_t i=0;i<VOICE_COUNT;++i) _voiceAllocCount[i]=0;
  _audioLoadAvgUs = 0; _audioLoadMaxUs = 0; _audioLoadLastUs = 0; _audioRiskCount = 0; _audioOverrunCount = 0; _audioLoadPeakUs = 0;
  _audioLoadAccumUs = 0; _audioLoadBlocks = 0; _audioLoadWindowStartMs = millis(); _audioStatsPrintMs = millis();
  _transportVintagePhase = 0; _transportVintageHold = 0; _transportVintageFilterState = 0; _transportVintageNoise = 0x13579BDFUL;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) _voices[i] = InstrumentVoice();
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) { _slotVoicePlayheadActive[i] = false; _slotVoicePlayheadFrame[i] = 0; _slotVoicePlayheadVoice[i] = -1; _slotVoicePlayheadAge[i] = 0; _slotMarkerRevision[i] = 1; _slotCoarse[i] = 0; _slotFine[i] = 0; _slotRoot[i] = 60; _slotPitchBendRange[i] = 2; _slotPitchBend[i] = 0; _slotOctave[i] = 0; _slotPitchTrack[i] = true; _slotAttackMs[i] = 5; _slotDecayMs[i] = 80; _slotSustainPct[i] = 100; _slotReleaseMs[i] = 80; _slotSustainDown[i] = false; _slotVintagePreset[i] = 0; _slotVintageSampleRate[i] = 0; _slotVintageBitDepth[i] = 0; _slotVintageFilter[i] = 0; _slotVintageJitter[i] = 0; _slotEchoSend[i] = 0; _slotSampleGainPct[i]=100; _slotVelocityAmount[i]=100; _slotLevel[i]=100; _slotPan[i]=0; _slotGainLQ15[i]=23170; _slotGainRQ15[i]=23170; _slotFilterCutoff[i]=100; _slotFilterResonance[i]=0; _slotFilterEnvAmount[i]=0; _slotFilterVelocityAmount[i]=0; _slotFilterKeytrack[i]=0; _slotFilterAttackMs[i]=0; _slotFilterDecayMs[i]=250; _slotFilterSustainPct[i]=0; _slotFilterReleaseMs[i]=300; _quattroKeyLow[i] = i * 32; _quattroKeyHigh[i] = (i == 3) ? 127 : (i * 32 + 31); _quattroMidiChannel[i] = i + 1; _slotVoiceMode[i]=0; _slotVoiceLimit[i]=PHX_DEFAULT_POLY_VOICE_LIMIT; _slotNotePriority[i]=0; _slotGlideMs[i]=0; }
  memset(_heldNotes, 0, sizeof(_heldNotes));
  memset(_heldVelocity, 0, sizeof(_heldVelocity));
  memset(_heldAge, 0, sizeof(_heldAge));
  memset(_noteHoldCount, 0, sizeof(_noteHoldCount));
  _heldAgeCounter = 0;
  _echoDelayMs = 250;
  _echoFeedback = 35;
  _echoMix = 20;
  _echoWritePos = 0;
  _echoDelayFramesCurrent = 8000;
  _echoDelayFramesXfadeTo = 8000;
  _echoDelayXfadePos = 0;
  _echoDelayXfadeActive = false;
  _echoFeedbackQ8Current = 35 * 256;
  _echoMixQ8Current = 20 * 256;
  _echoLimitCount = 0;
  _filterGuardCount = 0;
  _echoParamSlewCount = 0;
  _msNoteMissCount = 0; _msVelocityMissCount = 0; _msRrFallbackCount = 0; _msSanitizeCount = 0;
  _seqCmdDrops=0; _seqNoteOnFailCount=0; _seqOwnerFailCount=0; _seqOwnerStaleCount=0; _seqScheduleMissCount=0; _seqFrameOffsetMax=0;
  _echoTailActive = false;
  _echoTailQuietFrames = 0;
  _selectedSlot = 0;
  _recordSlot = 0;
  _playSlot = 0;
  _lastTickMs = millis();
  _phase = 0;
  _noise = 0x12345678UL;

  _psramOk = psramFound();
#if defined(ESP_ARDUINO_VERSION_MAJOR)
  _psramFreeKb = _psramOk ? (ESP.getFreePsram() / 1024UL) : 0;
#else
  _psramFreeKb = 0;
#endif

  const bool keygroupMetaOk = allocateKeygroupMetadata();
  allocateSampleBuffers();
  allocatePreviewBuffer();
  allocateEchoBuffer();
  _audioOk = keygroupMetaOk && beginI2S();
  if (_audioOk) {
    xTaskCreatePinnedToCore(audioTaskThunk, "PhoenixAudio", 8192, this, 24, &_audioTaskHandle, 1);
  }
}

bool PhoenixAudioManager::allocateKeygroupMetadata() {
  if (_keygroups) return true;

  const size_t count = (size_t)SLOT_COUNT * (size_t)MAX_KEYGROUPS;
  const size_t bytes = count * sizeof(MultisampleKeygroup);
  void *memory = nullptr;
  bool inPsram = false;

  if (psramFound()) {
    memory = ps_malloc(bytes);
    inPsram = (memory != nullptr);
  }
  // Fallback keeps Phoenix functional on an unexpected board/configuration,
  // but the normal ESP32-S3 8 MB PSRAM target always uses external RAM here.
  if (!memory) memory = malloc(bytes);
  if (!memory) {
    Serial.printf("MEM ERROR: multisample metadata allocation failed (%u bytes)\n",
                  (unsigned)bytes);
    return false;
  }

  _keygroups = reinterpret_cast<MultisampleKeygroup (*)[MAX_KEYGROUPS]>(memory);
  MultisampleKeygroup *flat = &_keygroups[0][0];
  for (size_t i = 0; i < count; ++i) {
    new (&flat[i]) MultisampleKeygroup();
  }

  PHX_INFO_PRINTF("MEM: multisample metadata %u bytes in %s\n",
                (unsigned)bytes, inPsram ? "PSRAM" : "internal heap fallback");
  return true;
}


bool PhoenixAudioManager::allocateSampleBuffers() {
  // v0.5.5: four Quattro-ready mono sample slots.
  // 10 seconds per slot at 32 kHz / 16-bit = 640 kB per slot, ~2.5 MB total.
  const uint32_t framesPerSlot = psramFound() ? (32000UL * 10UL) : (32000UL * 1UL);
  const size_t bytesPerSlot = (size_t)framesPerSlot * sizeof(int16_t);

  bool ok = true;
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (_slots[i].buffer) continue;
    _slots[i].capacityFrames = framesPerSlot;
    if (psramFound()) _slots[i].buffer = (int16_t*)ps_malloc(bytesPerSlot);
    else _slots[i].buffer = (int16_t*)malloc(bytesPerSlot);
    if (!_slots[i].buffer) {
      _slots[i].capacityFrames = 0;
      ok = false;
    } else {
      memset(_slots[i].buffer, 0, bytesPerSlot);
      _slots[i].frames = 0;
      _slots[i].recorded = false;
      _slots[i].loopEnabled = false;
      _slots[i].sampleStart = 0;
      _slots[i].sampleEnd = framesPerSlot;
      _slots[i].loopStart = 0;
      _slots[i].loopEnd = framesPerSlot;
      _slots[i].loopMode = 0;
      _slots[i].loopXfadeMs = 0;
      _slots[i].trimEnabled = false;
      _slots[i].trimUndoValid = false;
      _slots[i].trimUndoSampleStart = 0; _slots[i].trimUndoLoopStart = 0;
      _slots[i].trimUndoLoopEnd = 0; _slots[i].trimUndoSampleEnd = 0;
    }
  }
  return ok;
}


bool PhoenixAudioManager::allocatePreviewBuffer() {
  // v0.7.13i: non-destructive Sample Browser preview buffer.
  // v0.7.13j: a browser preview should start quickly, not copy a full slot.
  // Three seconds are sufficient for auditioning and reduce PSRAM and SD traffic.
  const uint32_t frames = psramFound() ? (32000UL * 3UL) : (32000UL * 1UL);
  const size_t bytes = (size_t)frames * sizeof(int16_t);
  if (_previewBuffer) return true;
  if (psramFound()) _previewBuffer = (int16_t*)ps_malloc(bytes);
  else _previewBuffer = (int16_t*)malloc(bytes);
  if (!_previewBuffer) {
    _previewCapacityFrames = 0;
    _previewFrames = 0;
    return false;
  }
  _previewCapacityFrames = frames;
  _previewFrames = 0;
  _previewPos = 0;
  memset(_previewBuffer, 0, bytes);
  return true;
}

bool PhoenixAudioManager::allocateEchoBuffer() {
  // v0.6.3: mono classic echo, up to 1000 ms at 32 kHz.
  const uint32_t frames = 32000UL;
  const size_t bytes = (size_t)frames * sizeof(int16_t);
  if (_echoBuffer) return true;
  if (psramFound()) _echoBuffer = (int16_t*)ps_malloc(bytes);
  else _echoBuffer = (int16_t*)malloc(bytes);
  if (!_echoBuffer) {
    _echoCapacityFrames = 0;
    return false;
  }
  _echoCapacityFrames = frames;
  memset(_echoBuffer, 0, bytes);
  _echoWritePos = 0;
  return true;
}

void PhoenixAudioManager::clearEchoBuffer() {
  if (_echoBuffer && _echoCapacityFrames > 0) {
    memset(_echoBuffer, 0, (size_t)_echoCapacityFrames * sizeof(int16_t));
  }
  _echoWritePos = 0;
  _echoDelayFramesXfadeTo = _echoDelayFramesCurrent;
  _echoDelayXfadePos = 0;
  _echoDelayXfadeActive = false;
  _echoTailActive = false;
  _echoTailQuietFrames = 0;
}

void PhoenixAudioManager::startEchoTail() {
  if (_echoBuffer && _echoCapacityFrames > 0 && _echoMix > 0) {
    _echoTailActive = true;
    _echoTailQuietFrames = 0;
  } else {
    _echoTailActive = false;
    _echoTailQuietFrames = 0;
  }
}

void PhoenixAudioManager::stopEchoTail() {
  _echoTailActive = false;
  _echoTailQuietFrames = 0;
}

void PhoenixAudioManager::setEchoParams(uint16_t delayMs, uint8_t feedback, uint8_t mix) {
  if (delayMs < 50) delayMs = 50;
  if (delayMs > 1000) delayMs = 1000;
  if (feedback > 90) feedback = 90;
  if (mix > 100) mix = 100;
  _echoDelayMs = delayMs;
  _echoFeedback = feedback;
  _echoMix = mix;
}

void PhoenixAudioManager::setEchoSend(uint8_t slot, uint8_t send) {
  _slotEchoSend[safeSlot(slot)] = (send > 100) ? 100 : send;
}

void PhoenixAudioManager::setMixerParams(uint8_t slot, uint8_t level, int8_t pan) {
  slot=safeSlot(slot); _slotLevel[slot]=(uint8_t)constrain((int)level,0,100); _slotPan[slot]=(int8_t)constrain((int)pan,-100,100);
  const float pos=((float)_slotPan[slot]+100.0f)*0.005f;
  const float lev=(float)_slotLevel[slot]*0.01f;
  _slotGainLQ15[slot]=(uint16_t)constrain((int)(sqrtf(1.0f-pos)*lev*32767.0f+0.5f),0,32767);
  _slotGainRQ15[slot]=(uint16_t)constrain((int)(sqrtf(pos)*lev*32767.0f+0.5f),0,32767);
}

int16_t PhoenixAudioManager::processEcho(int16_t dry, int16_t send) {
  if (!_echoBuffer || _echoCapacityFrames < 2) return dry;

  // C034a: Control/UI writes only targets. Feedback and mix are slewed as in
  // C034, but DELAY TIME no longer moves one read head through the buffer.
  // A moving read head audibly pitch-shifts/discontinuously jumps through stored
  // audio during fast edits. Instead, keep the old tap fixed, open a second tap
  // at the requested delay and crossfade the two for 256 samples (~8 ms).
  uint32_t targetDelayFrames = ((uint32_t)_echoDelayMs * 32000UL) / 1000UL;
  if (targetDelayFrames < 1U) targetDelayFrames = 1U;
  if (targetDelayFrames >= _echoCapacityFrames) targetDelayFrames = _echoCapacityFrames - 1U;
  if (_echoDelayFramesCurrent < 1U || _echoDelayFramesCurrent >= _echoCapacityFrames) {
    _echoDelayFramesCurrent = targetDelayFrames;
    _echoDelayFramesXfadeTo = targetDelayFrames;
    _echoDelayXfadePos = 0;
    _echoDelayXfadeActive = false;
  }
  if (!_echoDelayXfadeActive && targetDelayFrames != _echoDelayFramesCurrent) {
    _echoDelayFramesXfadeTo = targetDelayFrames;
    _echoDelayXfadePos = 0;
    _echoDelayXfadeActive = true;
  }

  const int32_t targetFeedbackQ8 = (int32_t)_echoFeedback * 256;
  const int32_t targetMixQ8 = (int32_t)_echoMix * 256;
  const int32_t slewQ8 = 8; // full-scale transition is ~100 ms at 32 kHz
  if (_echoFeedbackQ8Current < targetFeedbackQ8) { _echoFeedbackQ8Current += min(slewQ8, targetFeedbackQ8 - _echoFeedbackQ8Current); ++_echoParamSlewCount; }
  else if (_echoFeedbackQ8Current > targetFeedbackQ8) { _echoFeedbackQ8Current -= min(slewQ8, _echoFeedbackQ8Current - targetFeedbackQ8); ++_echoParamSlewCount; }
  if (_echoMixQ8Current < targetMixQ8) { _echoMixQ8Current += min(slewQ8, targetMixQ8 - _echoMixQ8Current); ++_echoParamSlewCount; }
  else if (_echoMixQ8Current > targetMixQ8) { _echoMixQ8Current -= min(slewQ8, _echoMixQ8Current - targetMixQ8); ++_echoParamSlewCount; }

  const uint32_t readPosA = (_echoWritePos + _echoCapacityFrames - _echoDelayFramesCurrent) % _echoCapacityFrames;
  const int16_t delayedA = _echoBuffer[readPosA];
  int16_t delayed = delayedA;
  if (_echoDelayXfadeActive) {
    const uint32_t readPosB = (_echoWritePos + _echoCapacityFrames - _echoDelayFramesXfadeTo) % _echoCapacityFrames;
    const int16_t delayedB = _echoBuffer[readPosB];
    const uint16_t x = _echoDelayXfadePos; // 0..255
    delayed = (int16_t)((((int32_t)delayedA * (int32_t)(256U - x)) +
                         ((int32_t)delayedB * (int32_t)x)) >> 8);
    ++_echoParamSlewCount;
    if (++_echoDelayXfadePos >= 256U) {
      _echoDelayFramesCurrent = _echoDelayFramesXfadeTo;
      _echoDelayXfadePos = 0;
      _echoDelayXfadeActive = false;
    }
  }

  // Dry path stays unchanged. Per-slot send feeds the shared echo and MIX is
  // the global return. Q8 smoothing keeps all arithmetic deterministic/integer.
  const int32_t wet = ((int32_t)delayed * _echoMixQ8Current) / (100 * 256);
  const int32_t feedback = ((int32_t)delayed * _echoFeedbackQ8Current) / (100 * 256);
  const int32_t out = (int32_t)dry + wet;
  const int32_t fb = (int32_t)send + feedback;
  if (out > 32767 || out < -32768 || fb > 32767 || fb < -32768) ++_echoLimitCount;
  _echoBuffer[_echoWritePos] = phxSoftLimit16(fb);
  if (++_echoWritePos >= _echoCapacityFrames) _echoWritePos = 0;

  return phxSoftLimit16(out);
}

bool PhoenixAudioManager::beginI2S() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
  cfg.sample_rate = 32000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = PHX_I2S_COMM_FORMAT;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = _bclkPin;
  pins.ws_io_num = _lrckPin;
  pins.data_out_num = _doutPin;
  pins.data_in_num = _dinPin;

  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) return false;

  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

void PhoenixAudioManager::updatePlaybackIncrement(uint8_t slot) {
  slot = safeSlot(slot);
  const int cents = (int)_slotCoarse[slot] * 100 + (int)_slotFine[slot];
  float ratio = powf(2.0f, (float)cents / 1200.0f);
  if (ratio < 0.25f) ratio = 0.25f;
  if (ratio > 4.0f) ratio = 4.0f;
  _playIncQ16 = (uint32_t)(ratio * 65536.0f + 0.5f);
  if (_playIncQ16 == 0) _playIncQ16 = 1;
  _pitchSemitone = _slotCoarse[slot];
}

void PhoenixAudioManager::setInstrumentParams(uint8_t slot, int8_t semitone, int16_t fineCent, uint8_t rootNote) {
  slot = safeSlot(slot);
  if (semitone < -24) semitone = -24;
  if (semitone > 24) semitone = 24;
  if (fineCent < -100) fineCent = -100;
  if (fineCent > 100) fineCent = 100;
  if (rootNote > 127) rootNote = 127;

  _slotCoarse[slot] = semitone;
  _slotFine[slot] = fineCent;
  _slotRoot[slot] = rootNote;
  if (slot == _playSlot || slot == _selectedSlot) updatePlaybackIncrement(slot);
}



void PhoenixAudioManager::setKeyboardParams(uint8_t slot, int8_t octaveOffset, bool pitchTracking) {
  slot = safeSlot(slot);
  if (octaveOffset < -2) octaveOffset = -2;
  if (octaveOffset > 2) octaveOffset = 2;
  _slotOctave[slot] = octaveOffset;
  _slotPitchTrack[slot] = pitchTracking;
}


void PhoenixAudioManager::setFilterParams(uint8_t slot, uint8_t cutoff, uint8_t resonance, int8_t envAmount, uint8_t velocityAmount, uint8_t keytrack) {
  slot=safeSlot(slot);
  _slotFilterCutoff[slot]=(uint8_t)constrain((int)cutoff,0,100);
  _slotFilterResonance[slot]=(uint8_t)constrain((int)resonance,0,100);
  _slotFilterEnvAmount[slot]=(int8_t)constrain((int)envAmount,-100,100);
  _slotFilterVelocityAmount[slot]=(uint8_t)constrain((int)velocityAmount,0,100);
  _slotFilterKeytrack[slot]=(uint8_t)constrain((int)keytrack,0,100);
}

void PhoenixAudioManager::setFilterEnvelopeParams(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs) {
  slot=safeSlot(slot);
  _slotFilterAttackMs[slot]=(uint16_t)constrain((int)attackMs,0,2000);
  _slotFilterDecayMs[slot]=(uint16_t)constrain((int)decayMs,0,5000);
  _slotFilterSustainPct[slot]=(uint8_t)constrain((int)sustainPct,0,100);
  _slotFilterReleaseMs[slot]=(uint16_t)constrain((int)releaseMs,0,5000);
}

int16_t PhoenixAudioManager::processVoiceFilter(InstrumentVoice &v, int16_t input) {
  const uint8_t slot=safeSlot(v.slot);
  if (_slotFilterCutoff[slot]>=100 && _slotFilterResonance[slot]==0 && _slotFilterEnvAmount[slot]==0 && _slotFilterVelocityAmount[slot]==0 && _slotFilterKeytrack[slot]==0 && v.velocityFilterAmount==0) return input;
  if (v.releasing) v.filterEnvStage=3;
  if (v.filterEnvStage==0) { uint32_t e=(uint32_t)v.filterEnvQ15+v.filterAttackStepQ15; if(e>=32767UL){v.filterEnvQ15=32767;v.filterEnvStage=1;}else v.filterEnvQ15=(uint16_t)e; }
  else if(v.filterEnvStage==1){ if(v.filterEnvQ15<=v.filterEnvSustainQ15 || v.filterDecayStepQ15>=v.filterEnvQ15-v.filterEnvSustainQ15){v.filterEnvQ15=v.filterEnvSustainQ15;v.filterEnvStage=2;}else v.filterEnvQ15-=v.filterDecayStepQ15; }
  else if(v.filterEnvStage==2) v.filterEnvQ15=v.filterEnvSustainQ15;
  else { if(v.filterEnvQ15<=v.filterReleaseStepQ15)v.filterEnvQ15=0; else v.filterEnvQ15-=v.filterReleaseStepQ15; }
  int32_t c=(int32_t)_slotFilterCutoff[slot]*327;
  c += ((int32_t)_slotFilterEnvAmount[slot]*(int32_t)v.filterEnvQ15*120)/10000;
  c += ((int32_t)_slotFilterVelocityAmount[slot]*(int32_t)v.velocity*80)/100;
  c += ((int32_t)v.velocityFilterAmount*(int32_t)v.velocity*80)/100;
  c += ((int32_t)_slotFilterKeytrack[slot]*((int32_t)v.note-(int32_t)_slotRoot[slot])*70)/100;
  c=constrain(c,250,15500);
  const int32_t damp=30000-((int32_t)_slotFilterResonance[slot]*220);
  // C034: the previous 32-bit products could overflow at high resonance /
  // strong modulation because c*high can exceed INT32 even though the final
  // shifted result is perfectly valid. Keep the filter topology and sound, but
  // perform the two state updates in 64 bit and guard the bounded state.
  int32_t high=(int32_t)input-v.filterLow-(int32_t)(((int64_t)v.filterBand*(int64_t)damp)>>15);
  int64_t nextBand=(int64_t)v.filterBand+(((int64_t)c*(int64_t)high)>>15);
  if (nextBand < -131072LL || nextBand > 131071LL) ++_filterGuardCount;
  if (nextBand < -131072LL) nextBand = -131072LL;
  else if (nextBand > 131071LL) nextBand = 131071LL;
  v.filterBand=(int32_t)nextBand;
  int64_t nextLow=(int64_t)v.filterLow+(((int64_t)c*(int64_t)v.filterBand)>>15);
  if (nextLow < -131072LL || nextLow > 131071LL) ++_filterGuardCount;
  if (nextLow < -131072LL) nextLow = -131072LL;
  else if (nextLow > 131071LL) nextLow = 131071LL;
  v.filterLow=(int32_t)nextLow;
  return phxClamp16(v.filterLow);
}
void PhoenixAudioManager::setVoiceConfig(uint8_t slot, uint8_t mode, uint8_t limit, uint8_t priority, uint16_t glideMs) {
  slot = safeSlot(slot);
  _slotVoiceMode[slot] = (mode > 2) ? 2 : mode;
  _slotVoiceLimit[slot] = (uint8_t)constrain((int)limit, 1, (int)VOICE_COUNT);
  _slotNotePriority[slot] = (priority > 2) ? 2 : priority;
  _slotGlideMs[slot] = (uint16_t)constrain((int)glideMs, 0, 2000);
  if (_slotVoiceMode[slot] != 0) _slotVoiceLimit[slot] = 1;
}

int PhoenixAudioManager::findActiveVoiceForSlot(uint8_t slot) const {
  for (uint8_t i=0;i<VOICE_COUNT;++i) if (_voices[i].active && _voices[i].slot==slot) return i;
  return -1;
}

int PhoenixAudioManager::selectHeldNote(uint8_t slot) const {
  slot = safeSlot(slot); int best=-1;
  if (_slotNotePriority[slot] == 1) { for (int n=0;n<128;++n) if (_heldNotes[slot][n]) return n; }
  else if (_slotNotePriority[slot] == 2) { for (int n=127;n>=0;--n) if (_heldNotes[slot][n]) return n; }
  else { uint32_t age=0; for (int n=0;n<128;++n) if (_heldNotes[slot][n] && _heldAge[slot][n]>=age) { age=_heldAge[slot][n]; best=n; } }
  return best;
}

int PhoenixAudioManager::allocateVoiceForSlot(uint8_t slot) {
  slot = safeSlot(slot);
  uint8_t count=0; int oldest=-1; uint32_t age=0xFFFFFFFFUL;
  for (uint8_t i=0;i<VOICE_COUNT;++i) {
    if (_voices[i].active && _voices[i].slot==slot) {
      ++count;
      if (_voices[i].age<age) {age=_voices[i].age; oldest=i;}
    }
  }
  if (count >= _slotVoiceLimit[slot] && oldest >= 0) {
    _lastAllocatorScanMax = VOICE_COUNT-1;
    if (_lastAllocatorScanMax > _highestAllocatorScanMax) _highestAllocatorScanMax = _lastAllocatorScanMax;
    _lastAllocatedVoice = (uint8_t)oldest;
    _lastAllocationReason = 3;
    ++_voiceStealCount;
    ++_voiceAllocCount[(uint8_t)oldest];
    return oldest;
  }
  return allocateVoice();
}

bool PhoenixAudioManager::enqueueMidiCommand(const MidiAudioCommand &cmd) {
  const uint8_t write = __atomic_load_n(&_midiCmdWrite, __ATOMIC_RELAXED);
  const uint8_t next = (uint8_t)((write + 1U) % MIDI_CMD_CAPACITY);
  const uint8_t read = __atomic_load_n(&_midiCmdRead, __ATOMIC_ACQUIRE);
  if (next == read) {
    __atomic_add_fetch(&_midiCmdDrops, 1U, __ATOMIC_RELAXED);
    return false;
  }
  _midiCmdRing[write] = cmd;
  __atomic_store_n(&_midiCmdWrite, next, __ATOMIC_RELEASE);
  return true;
}

bool PhoenixAudioManager::queueMidiNoteOn(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse) {
  MidiAudioCommand c = { MIDI_CMD_NOTE_ON, safeSlot(slot), midiNote, velocity, 0, (uint8_t)(reverse ? 1U : 0U) };
  return enqueueMidiCommand(c);
}

bool PhoenixAudioManager::queueMidiNoteOff(uint8_t slot, uint8_t midiNote) {
  MidiAudioCommand c = { MIDI_CMD_NOTE_OFF, safeSlot(slot), midiNote, 0, 0, 0 };
  return enqueueMidiCommand(c);
}

bool PhoenixAudioManager::queueMidiPitchBend(uint8_t slot, int16_t bend14) {
  MidiAudioCommand c = { MIDI_CMD_PITCH_BEND, safeSlot(slot), 0, 0, bend14, 0 };
  return enqueueMidiCommand(c);
}

bool PhoenixAudioManager::queueMidiSustain(uint8_t slot, bool down) {
  MidiAudioCommand c = { MIDI_CMD_SUSTAIN, safeSlot(slot), 0, 0, 0, (uint8_t)(down ? 1U : 0U) };
  return enqueueMidiCommand(c);
}

bool PhoenixAudioManager::queueMidiAllNotesOff(uint8_t slot, bool immediate) {
  MidiAudioCommand c = { MIDI_CMD_ALL_NOTES_OFF, safeSlot(slot), 0, 0, 0, (uint8_t)(immediate ? 1U : 0U) };
  return enqueueMidiCommand(c);
}

bool PhoenixAudioManager::enqueueSequencerCommand(const SequencerAudioCommand &cmd) {
  const uint8_t write = __atomic_load_n(&_seqCmdWrite, __ATOMIC_RELAXED);
  const uint8_t next = (uint8_t)((write + 1U) % SEQ_CMD_CAPACITY);
  const uint8_t read = __atomic_load_n(&_seqCmdRead, __ATOMIC_ACQUIRE);
  if (next == read) { __atomic_add_fetch(&_seqCmdDrops, 1U, __ATOMIC_RELAXED); return false; }
  _seqCmdRing[write] = cmd; __atomic_store_n(&_seqCmdWrite, next, __ATOMIC_RELEASE); return true;
}

bool PhoenixAudioManager::queueSequencerNote(uint8_t track, uint8_t note, uint8_t velocity, uint32_t gateFrames) {
  static volatile uint32_t fallbackEventId = 1;
  const uint32_t id = __atomic_add_fetch(&fallbackEventId, 1U, __ATOMIC_RELAXED);
  return queueSequencerNoteScheduled(track,note,velocity,gateFrames,micros(),id);
}

bool PhoenixAudioManager::queueSequencerNoteScheduled(uint8_t track, uint8_t note, uint8_t velocity, uint32_t gateFrames, uint32_t targetUs, uint32_t eventId) {
  SequencerAudioCommand c = { SEQ_CMD_NOTE, (uint8_t)(track & 3U), (uint8_t)(note & 127U),
                              (uint8_t)constrain((int)velocity,1,127), gateFrames < 32U ? 32U : gateFrames,
                              targetUs, eventId };
  return enqueueSequencerCommand(c);
}

bool PhoenixAudioManager::queueSequencerStop() {
  SequencerAudioCommand c = { SEQ_CMD_STOP,0,0,0,0,0,0 };
  return enqueueSequencerCommand(c);
}

void PhoenixAudioManager::releaseAllSequencerGates() {
  for (uint8_t t=0;t<4;++t) for(uint8_t g=0;g<SEQ_GATES_PER_TRACK;++g){
    auto &ev=_seqAudioGate[t][g];
    if(ev.active){
      const uint32_t current = (ev.voiceId>=0) ? voiceOwnershipToken(ev.voiceId) : 0U;
      if(current == ev.ownershipToken && current != 0U) {
        if(!releaseOwnedVoice(ev.voiceId,ev.ownershipToken)) ++_seqOwnerFailCount;
      } else {
        ++_seqOwnerStaleCount; // sample ended/stolen/reassigned: benign stale gate
      }
    }
    ev.active=false; ev.voiceId=-1; ev.ownershipToken=0; ev.framesLeft=0; ev.eventId=0;
  }
}

void PhoenixAudioManager::processSequencerCommandQueue(uint32_t blockStartUs, uint32_t blockFrames) {
  const uint32_t blockUs = (uint32_t)(((uint64_t)blockFrames * 1000000ULL) / 32000ULL);
  const uint32_t blockEndUs = blockStartUs + blockUs;
  // C036d: STOP must cancel already pre-scheduled future notes immediately.
  {
    const uint8_t read=__atomic_load_n(&_seqCmdRead,__ATOMIC_RELAXED), write=__atomic_load_n(&_seqCmdWrite,__ATOMIC_ACQUIRE);
    uint8_t scan=read; bool foundStop=false; uint8_t afterStop=read;
    while(scan!=write){ if(_seqCmdRing[scan].type==SEQ_CMD_STOP){ foundStop=true; afterStop=(uint8_t)((scan+1U)%SEQ_CMD_CAPACITY); } scan=(uint8_t)((scan+1U)%SEQ_CMD_CAPACITY); }
    if(foundStop){ releaseAllSequencerGates(); __atomic_store_n(&_seqCmdRead,afterStop,__ATOMIC_RELEASE); }
  }
  for(;;){
    const uint8_t read=__atomic_load_n(&_seqCmdRead,__ATOMIC_RELAXED);
    const uint8_t write=__atomic_load_n(&_seqCmdWrite,__ATOMIC_ACQUIRE);
    if(read==write) break;
    const SequencerAudioCommand c=_seqCmdRing[read];
    if(c.type==SEQ_CMD_NOTE && (int32_t)(c.targetUs-blockEndUs) > 0) break; // future block: keep queued
    __atomic_store_n(&_seqCmdRead,(uint8_t)((read+1U)%SEQ_CMD_CAPACITY),__ATOMIC_RELEASE);
    if(c.type==SEQ_CMD_STOP){ releaseAllSequencerGates(); continue; }
    if(c.type!=SEQ_CMD_NOTE) continue;

    uint32_t frameOffset=0;
    const int32_t until=(int32_t)(c.targetUs-blockStartUs);
    if(until>0){
      frameOffset=(uint32_t)(((uint64_t)(uint32_t)until*32000ULL)/1000000ULL);
      if(frameOffset>=blockFrames) frameOffset=blockFrames-1U;
    } else {
      const uint32_t late=(uint32_t)(-until);
      if(late>blockUs) ++_seqScheduleMissCount;
    }
    if(frameOffset>_seqFrameOffsetMax) _seqFrameOffsetMax=frameOffset;

    const uint8_t track=c.track&3U;
    const int8_t voiceId=startPlaybackMidiOwned(track,c.note,c.velocity);
    if(voiceId<0){++_seqNoteOnFailCount;continue;}
    const uint32_t token=voiceOwnershipToken(voiceId);
    if(!token){++_seqOwnerFailCount;continue;}
    _voices[(uint8_t)voiceId].seqStartDelayFrames=(uint16_t)frameOffset;

    int8_t freeGate=-1; uint8_t oldest=0; uint32_t least=0xFFFFFFFFUL;
    for(uint8_t g=0;g<SEQ_GATES_PER_TRACK;++g){
      if(!_seqAudioGate[track][g].active){freeGate=(int8_t)g;break;}
      if(_seqAudioGate[track][g].framesLeft<least){least=_seqAudioGate[track][g].framesLeft;oldest=g;}
    }
    const uint8_t gi=freeGate>=0?(uint8_t)freeGate:oldest;
    auto &ev=_seqAudioGate[track][gi];
    if(ev.active){
      const uint32_t current=(ev.voiceId>=0)?voiceOwnershipToken(ev.voiceId):0U;
      if(current==ev.ownershipToken && current!=0U){
        if(!releaseOwnedVoice(ev.voiceId,ev.ownershipToken)) ++_seqOwnerFailCount;
      } else ++_seqOwnerStaleCount;
    }
    ev.active=true; ev.voiceId=voiceId; ev.ownershipToken=token;
    // Gate countdown is sample-accurate and starts at the scheduled onset frame.
    ev.framesLeft=c.gateFrames + frameOffset + 1U; ev.eventId=c.eventId;
  }
}

void PhoenixAudioManager::advanceSequencerGates(uint32_t frames) {
  for(uint8_t t=0;t<4;++t) for(uint8_t g=0;g<SEQ_GATES_PER_TRACK;++g){
    auto &ev=_seqAudioGate[t][g]; if(!ev.active) continue;
    if(ev.framesLeft<=frames){
      const uint32_t current=(ev.voiceId>=0)?voiceOwnershipToken(ev.voiceId):0U;
      if(current==ev.ownershipToken && current!=0U){
        if(!releaseOwnedVoice(ev.voiceId,ev.ownershipToken)) ++_seqOwnerFailCount;
      } else ++_seqOwnerStaleCount;
      ev.active=false;ev.voiceId=-1;ev.ownershipToken=0;ev.framesLeft=0;ev.eventId=0;
    } else ev.framesLeft-=frames;
  }
}

void PhoenixAudioManager::processMidiCommandQueue() {
  for (;;) {
    const uint8_t read = __atomic_load_n(&_midiCmdRead, __ATOMIC_RELAXED);
    const uint8_t write = __atomic_load_n(&_midiCmdWrite, __ATOMIC_ACQUIRE);
    if (read == write) break;
    const MidiAudioCommand c = _midiCmdRing[read];
    __atomic_store_n(&_midiCmdRead, (uint8_t)((read + 1U) % MIDI_CMD_CAPACITY), __ATOMIC_RELEASE);
    switch (c.type) {
      case MIDI_CMD_NOTE_ON: startPlaybackMidi(c.slot, c.note, c.velocity, (c.flags & 1U) != 0U); break;
      case MIDI_CMD_NOTE_OFF: noteOffMidi(c.slot, c.note); break;
      case MIDI_CMD_PITCH_BEND: setPitchBend(c.slot, c.value); break;
      case MIDI_CMD_SUSTAIN: setSustain(c.slot, (c.flags & 1U) != 0U); break;
      case MIDI_CMD_ALL_NOTES_OFF: allNotesOff(c.slot, (c.flags & 1U) != 0U); break;
      default: break;
    }
  }
}

bool PhoenixAudioManager::startPlaybackMidi(uint8_t slot, uint8_t midiNote, uint8_t velocity) {
  return startPlaybackMidiVoice(slot, midiNote, velocity, false, false) >= 0;
}

bool PhoenixAudioManager::startPlaybackMidi(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse) {
  return startPlaybackMidiVoice(slot, midiNote, velocity, reverse, false) >= 0;
}

int8_t PhoenixAudioManager::startPlaybackMidiOwned(uint8_t slot, uint8_t midiNote, uint8_t velocity) {
  return startPlaybackMidiOwned(slot, midiNote, velocity, false);
}

int8_t PhoenixAudioManager::startPlaybackMidiOwned(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse) {
  const int voiceId = startPlaybackMidiVoice(slot, midiNote, velocity, reverse, true);
  return (voiceId >= 0 && voiceId < 128) ? (int8_t)voiceId : (int8_t)-1;
}

uint32_t PhoenixAudioManager::voiceOwnershipToken(int8_t voiceId) const {
  if (voiceId < 0 || voiceId >= (int8_t)VOICE_COUNT) return 0;
  const InstrumentVoice &v = _voices[(uint8_t)voiceId];
  return v.active ? v.age : 0;
}

void PhoenixAudioManager::beginVoiceRelease(InstrumentVoice &v) {
  v.sustained = false;
  v.releasing = true;
  v.envStage = 3;

  // A sustain loop is left immediately on Note Off. For an alternate loop,
  // continue from the current sample position in the forward direction so the
  // release tail always reaches the user-defined Sample End marker.
  if (!v.reverse && v.loopMode == LOOP_ALTERNATE) v.loopForward = true;
}

bool PhoenixAudioManager::releaseOwnedVoice(int8_t voiceId, uint32_t ownershipToken) {
  if (voiceId < 0 || voiceId >= (int8_t)VOICE_COUNT || ownershipToken == 0) return false;
  InstrumentVoice &v = _voices[(uint8_t)voiceId];

  // A stolen/reused voice has a different age token. Never release the new owner.
  if (!v.active || v.age != ownershipToken) return false;

  const uint8_t slot = safeSlot(v.slot);
  const uint8_t note = v.note & 127U;
  if (_noteHoldCount[slot][note] > 0U) --_noteHoldCount[slot][note];
  v.keyHeld = false;

  // One-shot keygroups deliberately ignore gate/note-off and run to sample end,
  // unless transport STOP, choke, exclusive play or panic terminates them.
  if (!(v.keygroup < MAX_KEYGROUPS && _keygroups[slot][v.keygroup].oneShot)) {
    if (_slotSustainDown[slot]) {
      v.sustained = true;
    } else {
      beginVoiceRelease(v);
    }
  }

  // Keep the held-note table true while another independently owned voice of
  // the same slot/note is still held. This preserves overlapping equal notes.
  bool anotherHeld = false;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    if (i == (uint8_t)voiceId) continue;
    const InstrumentVoice &other = _voices[i];
    if (other.active && other.slot == slot && other.note == note && other.keyHeld) {
      anotherHeld = true;
      break;
    }
  }
  _heldNotes[slot][note] = anotherHeld || (_noteHoldCount[slot][note] != 0U);
  return true;
}

int PhoenixAudioManager::findOldestHeldVoiceForNote(uint8_t slot, uint8_t midiNote) const {
  slot = safeSlot(slot);
  int best = -1;
  uint32_t bestAge = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    const InstrumentVoice &v = _voices[i];
    if (!v.active || v.slot != slot || v.note != midiNote || !v.keyHeld) continue;
    if (v.age < bestAge) { bestAge = v.age; best = (int)i; }
  }
  return best;
}

int PhoenixAudioManager::findVoiceForNote(uint8_t slot, uint8_t midiNote) const {
  slot = safeSlot(slot);
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    if (_voices[i].active && _voices[i].slot == slot && _voices[i].note == midiNote) return (int)i;
  }
  return -1;
}

int PhoenixAudioManager::allocateVoice() {
  _lastAllocatorScanMax = 0;
  _lastAllocationReason = 0;

  // 1. Always use a genuinely free voice first.
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    _lastAllocatorScanMax = i;
    if (!_voices[i].active) {
      _lastAllocatedVoice = i;
      ++_voiceAllocCount[i];
      if (_lastAllocatorScanMax > _highestAllocatorScanMax) _highestAllocatorScanMax = _lastAllocatorScanMax;
      return (int)i;
    }
  }

  // 2. Prefer stealing the oldest voice that is already in its release phase.
  int oldestRelease = -1;
  uint32_t oldestReleaseAge = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    _lastAllocatorScanMax = i;
    if (_voices[i].releasing && _voices[i].age < oldestReleaseAge) {
      oldestRelease = (int)i;
      oldestReleaseAge = _voices[i].age;
    }
  }
  if (_lastAllocatorScanMax > _highestAllocatorScanMax) _highestAllocatorScanMax = _lastAllocatorScanMax;
  if (oldestRelease >= 0) {
    _lastAllocatedVoice = (uint8_t)oldestRelease;
    _lastAllocationReason = 1;
    ++_voiceStealCount;
    ++_voiceAllocCount[(uint8_t)oldestRelease];
    return oldestRelease;
  }

  // 3. All voices are held: replace the globally oldest voice.
  uint8_t oldest = 0;
  uint32_t oldestAge = _voices[0].age;
  for (uint8_t i = 1; i < VOICE_COUNT; ++i) {
    _lastAllocatorScanMax = i;
    if (_voices[i].age < oldestAge) { oldest = i; oldestAge = _voices[i].age; }
  }
  if (_lastAllocatorScanMax > _highestAllocatorScanMax) _highestAllocatorScanMax = _lastAllocatorScanMax;
  _lastAllocatedVoice = oldest;
  _lastAllocationReason = 2;
  ++_voiceStealCount;
  ++_voiceAllocCount[oldest];
  return (int)oldest;
}

void PhoenixAudioManager::refreshActiveVoiceCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) if (_voices[i].active) ++n;
  _activeVoiceCount = n;
  if (n > _activeVoicePeak) _activeVoicePeak = n;
}

void PhoenixAudioManager::resetDiagnostics() {
  _audioLoadAvgUs = 0;
  _audioLoadMaxUs = 0;
  _audioLoadLastUs = 0;
  _audioLoadPeakUs = 0;
  _audioRiskCount = 0;
  _audioOverrunCount = 0;
  _underruns = 0;
  _voiceStealCount = 0;
  _duplicateNoteOnCount = 0;
  _orphanNoteOffCount = 0;
  _sustainDeferredNoteOffCount = 0;
  _panicKillCount = 0;
  _echoLimitCount = 0;
  _filterGuardCount = 0;
  _echoParamSlewCount = 0;
  _msNoteMissCount = 0; _msVelocityMissCount = 0; _msRrFallbackCount = 0; _msSanitizeCount = 0;
  _seqCmdDrops=0; _seqNoteOnFailCount=0; _seqOwnerFailCount=0; _seqOwnerStaleCount=0; _seqScheduleMissCount=0; _seqFrameOffsetMax=0;
  _activeVoicePeak = _activeVoiceCount;
  // The 64-bit audio-window accumulator belongs to AudioTask/Core 1.
  // Leave it untouched here to avoid a cross-core torn write; the next
  // one-second window naturally publishes a fresh average.
  _audioLoadWindowStartMs = millis();
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) _voiceAllocCount[i] = 0;
  _dspProfileSamples = 0; _dspSetupEnvUs = 0; _dspVintageUs = 0; _dspFilterUs = 0; _dspMixPosUs = 0; _dspEchoOutUs = 0;
  for (uint8_t i = 0; i <= VOICE_COUNT; ++i) { _voiceHistCount[i] = 0; _voiceHistSumUs[i] = 0; _voiceHistMaxUs[i] = 0; }
  Serial.println(F("C034 DIAGNOSTICS RESET"));
}

void PhoenixAudioManager::updateSlotVoicePlayheads() {
  for (uint8_t slot = 0; slot < SLOT_COUNT; ++slot) {
    bool haveVoice = false;
    uint32_t newestAge = 0U;
    uint32_t frame = _slotVoicePlayheadFrame[slot];
    int8_t owner = -1;

    // First try the explicitly tracked owner. This is the normal fast path and
    // avoids losing the GUI playhead because of unrelated source-buffer state.
    const int8_t tracked = _slotVoicePlayheadVoice[slot];
    const uint32_t trackedAge = _slotVoicePlayheadAge[slot];
    if (tracked >= 0 && tracked < (int8_t)VOICE_COUNT) {
      const InstrumentVoice &v = _voices[(uint8_t)tracked];
      if (v.active && safeSlot(v.slot) == slot && v.keygroup == 255U && v.age == trackedAge) {
        haveVoice = true;
        newestAge = v.age;
        owner = tracked;
        frame = (uint32_t)(v.posQ16 >> 16);
      }
    }

    // If the tracked voice ended or was stolen, find the newest remaining
    // normal slot-sample voice. Multisample/keygroup voices deliberately do
    // not drive this waveform because they can use a different source sample.
    if (!haveVoice) {
      for (uint8_t vi = 0; vi < VOICE_COUNT; ++vi) {
        const InstrumentVoice &v = _voices[vi];
        if (!v.active || safeSlot(v.slot) != slot || v.keygroup != 255U) continue;
        if (!haveVoice || v.age >= newestAge) {
          haveVoice = true;
          newestAge = v.age;
          owner = (int8_t)vi;
          frame = (uint32_t)(v.posQ16 >> 16);
        }
      }
    }

    const uint32_t frames = _slots[slot].frames;
    if (frames > 0U && frame >= frames) frame = frames - 1U;

    // Publish payload first and validity last. The GUI core therefore never
    // sees an active flag paired with an older owner/frame snapshot.
    _slotVoicePlayheadFrame[slot] = frame;
    _slotVoicePlayheadVoice[slot] = owner;
    _slotVoicePlayheadAge[slot] = haveVoice ? newestAge : 0U;
    _slotVoicePlayheadActive[slot] = haveVoice;
  }
}

void PhoenixAudioManager::printVoiceAudit() {
  if (!PHX_ENABLE_VOICE_AUDIT) return;
  uint8_t active=0, releasing=0, heldVoices=0, freeVoices=0;
  uint16_t heldKeys=0;
  for (uint8_t slot=0; slot<SLOT_COUNT; ++slot)
    for (uint8_t note=0; note<128; ++note) if (_heldNotes[slot][note]) ++heldKeys;

  char map[VOICE_COUNT+1];
  for (uint8_t i=0; i<VOICE_COUNT; ++i) {
    const InstrumentVoice &v=_voices[i];
    if (!v.active) { ++freeVoices; map[i]='.'; }
    else if (v.releasing) { ++active; ++releasing; map[i]='R'; }
    else if (v.keyHeld) { ++active; ++heldVoices; map[i]='H'; }
    else { ++active; map[i]='A'; }
  }
  map[VOICE_COUNT]=0;

  Serial.printf("VOICE CONFIG configured=%u pool=%u active=%u releasing=%u heldVoices=%u free=%u heldKeys=%u maxScan=%u lastScan=%u lastVoice=%d reason=%u steals=%lu\n",
                (unsigned)VOICE_COUNT, (unsigned)(sizeof(_voices)/sizeof(_voices[0])),
                (unsigned)active, (unsigned)releasing, (unsigned)heldVoices, (unsigned)freeVoices,
                (unsigned)heldKeys, (unsigned)_highestAllocatorScanMax, (unsigned)_lastAllocatorScanMax,
                _lastAllocatedVoice==255?-1:(int)_lastAllocatedVoice, (unsigned)_lastAllocationReason,
                (unsigned long)_voiceStealCount);
  Serial.printf("VOICE MAP %s | SLOT LIMITS S1=%u/%s S2=%u/%s S3=%u/%s S4=%u/%s\n", map,
                (unsigned)_slotVoiceLimit[0], _slotVoiceMode[0]==0?"POLY":(_slotVoiceMode[0]==1?"MONO":"LEG"),
                (unsigned)_slotVoiceLimit[1], _slotVoiceMode[1]==0?"POLY":(_slotVoiceMode[1]==1?"MONO":"LEG"),
                (unsigned)_slotVoiceLimit[2], _slotVoiceMode[2]==0?"POLY":(_slotVoiceMode[2]==1?"MONO":"LEG"),
                (unsigned)_slotVoiceLimit[3], _slotVoiceMode[3]==0?"POLY":(_slotVoiceMode[3]==1?"MONO":"LEG"));
  Serial.print("VOICE USAGE");
  for (uint8_t i=0;i<VOICE_COUNT;++i) Serial.printf(" V%02u=%lu", (unsigned)i, (unsigned long)_voiceAllocCount[i]);
  Serial.println();
}

int PhoenixAudioManager::startPlaybackMidiVoice(uint8_t slot, uint8_t midiNote, uint8_t velocity, bool reverse, bool forceNewVoice) {
  slot = safeSlot(slot);
  midiNote = (uint8_t)constrain((int)midiNote, 0, 127);
  // C031: count overlapping equal Note On events.
  if (_noteHoldCount[slot][midiNote] > 0U) ++_duplicateNoteOnCount;
  if (_noteHoldCount[slot][midiNote] < 255U) ++_noteHoldCount[slot][midiNote];
  _heldNotes[slot][midiNote] = true;
  _heldVelocity[slot][midiNote] = velocity;
  _heldAge[slot][midiNote] = ++_heldAgeCounter;
  if (_slotVoiceMode[slot] != 0) {
    const int chosen = selectHeldNote(slot);
    if (chosen >= 0 && chosen != midiNote) { const int heldVoice=findVoiceForNote(slot,(uint8_t)chosen); return heldVoice>=0?heldVoice:0; } // LOW/HIGH priority keeps the preferred held note
  }
  if (keygroupCount(slot) > 0) return startMultisampleMidiOwned(slot, midiNote, velocity, reverse);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || s.frames == 0 || velocity == 0) return -1;

  uint32_t start = 0U, end = 0U, loopStart = 0U, loopEnd = 0U;
  uint8_t resolvedLoopMode = LOOP_OFF;
  resolveSlotPlaybackMarkers(slot, start, end, loopStart, loopEnd, resolvedLoopMode);

  int cents = (int)_slotCoarse[slot] * 100 + (int)_slotFine[slot];
  if (_slotPitchTrack[slot]) {
    int semis = (int)midiNote - (int)_slotRoot[slot] + ((int)_slotOctave[slot] * 12);
    if (semis < -48) semis = -48;
    if (semis > 48) semis = 48;
    cents += semis * 100;
  }
  float ratio = powf(2.0f, (float)cents / 1200.0f);
  if (ratio < 0.25f) ratio = 0.25f;
  if (ratio > 4.0f) ratio = 4.0f;
  uint32_t inc = (uint32_t)(ratio * 65536.0f + 0.5f);
  if (inc == 0) inc = 1;

  // C031: overlapping equal notes in POLY mode must own independent voices.
  // The first instance may reuse an existing matching voice for legacy behavior;
  // a duplicate key-down always allocates a distinct voice so each Note Off can
  // release exactly one instance.
  const bool duplicatePolyNote = (_slotVoiceMode[slot] == 0 && _noteHoldCount[slot][midiNote] > 1U);
  int vi = forceNewVoice ? allocateVoiceForSlot(slot)
                         : ((_slotVoiceMode[slot] == 0)
                              ? (duplicatePolyNote ? allocateVoiceForSlot(slot) : findVoiceForNote(slot, midiNote))
                              : findActiveVoiceForSlot(slot));
  const bool hadMonoVoice = (!forceNewVoice && vi >= 0 && _slotVoiceMode[slot] != 0);
  if (vi < 0) vi = (_slotVoiceMode[slot] == 0) ? allocateVoiceForSlot(slot) : allocateVoice();
  if (PHX_ENABLE_VOICE_AUDIT) {
    Serial.printf("VOICE ALLOC slot=%u note=%u vel=%u voice=%d limit=%u mode=%u scan=%u reason=%u\n",
                  (unsigned)(slot+1), (unsigned)midiNote, (unsigned)velocity, vi,
                  (unsigned)_slotVoiceLimit[slot], (unsigned)_slotVoiceMode[slot],
                  (unsigned)_lastAllocatorScanMax, (unsigned)_lastAllocationReason);
  }
  InstrumentVoice &v = _voices[vi];
  const uint32_t oldInc = v.incQ16;
  const bool legatoNoRetrigger = hadMonoVoice && _slotVoiceMode[slot] == 2 && v.keyHeld;
  v.active = false; // publish all parameters before activating the voice
  v.releasing = false;
  v.keyHeld = true;
  v.sustained = false;
  if (!legatoNoRetrigger) v.envStage = 0;
  v.slot = slot;
  v.note = midiNote;
  v.velocity = velocity;
  v.velocityLevel = (uint8_t)(127U - (((uint32_t)(127U - velocity) * (uint32_t)_slotVelocityAmount[slot]) / 100U));
  v.velocityFilterAmount = 0;
  v.keygroup = 255;
  v.sourceBuffer = s.buffer;
  v.sourceFrames = s.frames;
  v.sourceGainLQ15 = 32767;
  v.sourceGainRQ15 = 32767;
  v.reverse = reverse;
  v.rangeStart = start;
  v.rangeEnd = end;
  v.loopForward = true;
  v.loopStartFrame = loopStart;
  v.loopEndFrame = loopEnd;
  v.loopMode = reverse ? LOOP_OFF : resolvedLoopMode;
  { uint32_t xf=(uint32_t)s.loopXfadeMs*32U; const uint32_t ll=(v.loopEndFrame>v.loopStartFrame)?(v.loopEndFrame-v.loopStartFrame):0U; if(xf>ll/2U)xf=ll/2U; v.loopXfadeFrames=(uint16_t)min((uint32_t)65535U,xf); }
  v.markerRevision = _slotMarkerRevision[slot];
  if (!legatoNoRetrigger) v.posQ16 = ((uint64_t)(reverse ? (end - 1) : start)) << 16;
  v.baseIncQ16 = inc;
  const float bendSemis = ((float)_slotPitchBend[slot] / 8192.0f) * (float)_slotPitchBendRange[slot];
  float bendRatio = powf(2.0f, bendSemis / 12.0f);
  v.targetIncQ16 = (uint32_t)((float)inc * bendRatio + 0.5f);
  if (v.targetIncQ16 == 0) v.targetIncQ16 = 1;
  const uint32_t glideFrames = (uint32_t)_slotGlideMs[slot] * 32UL;
  if (hadMonoVoice && glideFrames > 0) {
    v.incQ16 = oldInc ? oldInc : v.targetIncQ16;
    v.glideFramesLeft = glideFrames;
    v.glideStepQ16 = ((int32_t)v.targetIncQ16 - (int32_t)v.incQ16) / (int32_t)glideFrames;
    if (v.glideStepQ16 == 0 && v.incQ16 != v.targetIncQ16) v.glideStepQ16 = (v.targetIncQ16 > v.incQ16) ? 1 : -1;
  } else { v.incQ16 = v.targetIncQ16; v.glideFramesLeft = 0; v.glideStepQ16 = 0; }
  if (!legatoNoRetrigger) v.envQ15 = 0;
  v.sustainQ15 = (uint16_t)(((uint32_t)_slotSustainPct[slot] * 32767UL) / 100UL);
  const uint32_t attackFrames = (uint32_t)_slotAttackMs[slot] * 32UL;
  const uint32_t decayFrames = (uint32_t)_slotDecayMs[slot] * 32UL;
  const uint32_t releaseFrames = (uint32_t)_slotReleaseMs[slot] * 32UL;
  uint32_t attackStep = (attackFrames == 0U) ? 32767U : (32767U / attackFrames);
  if (attackStep < 1U) attackStep = 1U;
  if (attackStep > 32767U) attackStep = 32767U;
  v.attackStepQ15 = (uint16_t)attackStep;

  const uint32_t decaySpan = 32767U - (uint32_t)v.sustainQ15;
  uint32_t decayStep = (decayFrames == 0U || decaySpan == 0U)
      ? 32767U
      : (decaySpan / decayFrames);
  if (decayStep < 1U) decayStep = 1U;
  if (decayStep > 32767U) decayStep = 32767U;
  v.decayStepQ15 = (uint16_t)decayStep;

  uint32_t releaseStep = (releaseFrames == 0U) ? 32767U : (32767U / releaseFrames);
  if (releaseStep < 1U) releaseStep = 1U;
  if (releaseStep > 32767U) releaseStep = 32767U;
  v.releaseStepQ15 = (uint16_t)releaseStep;
  v.age = ++_voiceAgeCounter;
  v.vintagePhase = 0; v.vintageHold = 0; v.vintageFilterState = 0; v.vintageNoise = 0xA5A55A5AUL ^ ((uint32_t)midiNote << 16) ^ v.age;
  if (!legatoNoRetrigger) {
    v.filterEnvStage = 0;
    v.filterEnvQ15 = 0;
    v.filterLow = 0;
    v.filterBand = 0;
    v.filterEnvSustainQ15 = (uint16_t)(((uint32_t)_slotFilterSustainPct[slot] * 32767UL) / 100UL);
    const uint32_t faf = (uint32_t)_slotFilterAttackMs[slot] * 32UL;
    const uint32_t fdf = (uint32_t)_slotFilterDecayMs[slot] * 32UL;
    const uint32_t frf = (uint32_t)_slotFilterReleaseMs[slot] * 32UL;
    uint32_t fas = faf ? (32767UL / faf) : 32767UL; if (fas < 1UL) fas = 1UL; if (fas > 32767UL) fas = 32767UL;
    uint32_t fspan = 32767UL - v.filterEnvSustainQ15;
    uint32_t fds = (fdf && fspan) ? (fspan / fdf) : 32767UL; if (fds < 1UL) fds = 1UL; if (fds > 32767UL) fds = 32767UL;
    uint32_t frs = frf ? (32767UL / frf) : 32767UL; if (frs < 1UL) frs = 1UL; if (frs > 32767UL) frs = 32767UL;
    v.filterAttackStepQ15 = (uint16_t)fas;
    v.filterDecayStepQ15 = (uint16_t)fds;
    v.filterReleaseStepQ15 = (uint16_t)frs;
  }
  v.seqStartDelayFrames = 0;
  v.active = true;
  // C010: make the SAMPLE EDITOR playhead visible immediately on Note On;
  // the audio task then refreshes the frame once per rendered block.
  _slotVoicePlayheadFrame[slot] = (uint32_t)(v.posQ16 >> 16);
  _slotVoicePlayheadAge[slot] = v.age;
  _slotVoicePlayheadVoice[slot] = (int8_t)vi;
  _slotVoicePlayheadActive[slot] = true;
  _selectedSlot = slot;
  _playSlot = slot;
  _lastReplayReverse = reverse;
  _testTone = TONE_THRU;
  refreshActiveVoiceCount();
  return vi;
}


void PhoenixAudioManager::updateVelocityLayerPan(VelocityLayer &vl) {
  const float p = ((float)vl.pan + 100.0f) / 200.0f;
  const float level = (float)vl.level / 100.0f;
  vl.gainLQ15 = (uint16_t)constrain((int)(sqrtf(1.0f - p) * level * 32767.0f + 0.5f), 0, 32767);
  vl.gainRQ15 = (uint16_t)constrain((int)(sqrtf(p) * level * 32767.0f + 0.5f), 0, 32767);
}

bool PhoenixAudioManager::keygroupEnabled(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].enabled; }
uint8_t PhoenixAudioManager::keygroupLow(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].lowNote; }
uint8_t PhoenixAudioManager::keygroupHigh(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].highNote; }
uint8_t PhoenixAudioManager::keygroupRoot(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].rootNote; }
uint8_t PhoenixAudioManager::keygroupChokeGroup(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].chokeGroup; }
bool PhoenixAudioManager::keygroupOneShot(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].oneShot; }
uint16_t PhoenixAudioManager::keygroupChokeFadeMs(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].chokeFadeMs; }
uint8_t PhoenixAudioManager::keygroupPlayMode(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].playMode; }
uint8_t PhoenixAudioManager::keygroupStartPct(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].startPct; }
uint8_t PhoenixAudioManager::keygroupEndPct(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].endPct; }
bool PhoenixAudioManager::keygroupReverse(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].reversePlayback; }
int8_t PhoenixAudioManager::keygroupTranspose(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].transposeSemitone; }
int16_t PhoenixAudioManager::keygroupFineCent(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].fineCent; }
uint8_t PhoenixAudioManager::keygroupKeytrackPct(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].keytrackPct; }
uint8_t PhoenixAudioManager::keygroupLayerLoopMode(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].loopMode; }
uint8_t PhoenixAudioManager::keygroupLayerLoopStartPct(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].loopStartPct; }
uint8_t PhoenixAudioManager::keygroupLayerLoopEndPct(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].loopEndPct; }
uint8_t PhoenixAudioManager::keygroupLayerLoopXfadeMs(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].loopXfadeMs; }
uint8_t PhoenixAudioManager::keygroupLayerVelocityToLevelPct(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].velocityToLevelPct; }
uint8_t PhoenixAudioManager::keygroupLayerVelocityToFilterPct(uint8_t slot,uint8_t group,uint8_t layer) const { return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].velocityToFilterPct; }
uint8_t PhoenixAudioManager::keygroupExclusiveGroup(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].exclusiveGroup; }
bool PhoenixAudioManager::keygroupRetriggerLegato(uint8_t slot, uint8_t group) const { return _keygroups[safeSlot(slot)][group & 15].retriggerLegato; }
uint32_t PhoenixAudioManager::keygroupFrames(uint8_t slot, uint8_t group) const { return keygroupLayerFrames(slot,group,0); }
uint8_t PhoenixAudioManager::keygroupLevel(uint8_t slot,uint8_t group) const{return keygroupLayerLevel(slot,group,0);}
int8_t PhoenixAudioManager::keygroupPan(uint8_t slot,uint8_t group) const{return keygroupLayerPan(slot,group,0);}
const char* PhoenixAudioManager::keygroupPath(uint8_t slot, uint8_t group) const { return keygroupLayerPath(slot,group,0); }
bool PhoenixAudioManager::keygroupLayerEnabled(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].enabled;}
uint8_t PhoenixAudioManager::keygroupLayerVelocityLow(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].velocityLow;}
uint8_t PhoenixAudioManager::keygroupLayerVelocityHigh(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].velocityHigh;}
uint32_t PhoenixAudioManager::keygroupLayerFrames(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].rr[0].frames;}
uint8_t PhoenixAudioManager::keygroupLayerLevel(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].level;}
int8_t PhoenixAudioManager::keygroupLayerPan(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].pan;}
const char* PhoenixAudioManager::keygroupLayerPath(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].rr[0].path;}
uint8_t PhoenixAudioManager::keygroupLayerRoundRobinMode(uint8_t slot,uint8_t group,uint8_t layer) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].rrMode;}
uint8_t PhoenixAudioManager::keygroupLayerRoundRobinCount(uint8_t slot,uint8_t group,uint8_t layer) const{const VelocityLayer &vl=_keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS];uint8_t n=0;for(uint8_t i=0;i<MAX_RR_VARIANTS;++i)if(vl.rr[i].buffer&&vl.rr[i].frames)++n;return n;}
const char* PhoenixAudioManager::keygroupLayerVariantPath(uint8_t slot,uint8_t group,uint8_t layer,uint8_t variant) const{return _keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS].rr[variant%MAX_RR_VARIANTS].path;}
bool PhoenixAudioManager::setKeygroupLayerRoundRobinMode(uint8_t slot,uint8_t group,uint8_t layer,uint8_t mode){if(mode!=0&&mode!=2&&mode!=3&&mode!=4&&mode!=5)return false;VelocityLayer &vl=_keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS];vl.rrMode=mode;vl.rrCounter=0;return true;}
uint8_t PhoenixAudioManager::keygroupCount(uint8_t slot) const {
  uint8_t n=0; slot=safeSlot(slot);
  for(uint8_t g=0;g<MAX_KEYGROUPS;++g){
    const MultisampleKeygroup &kg=_keygroups[slot][g];
    bool loaded=false;
    for(uint8_t l=0;l<MAX_VELOCITY_LAYERS&&!loaded;++l){
      if(!kg.layer[l].enabled) continue;
      for(uint8_t r=0;r<MAX_RR_VARIANTS;++r)
        if(kg.layer[l].rr[r].buffer&&kg.layer[l].rr[r].frames){loaded=true;break;}
    }
    if(kg.enabled&&loaded)++n;
  }
  return n;
}

uint32_t PhoenixAudioManager::sanitizeMultisampleSlot(uint8_t slot){
  slot=safeSlot(slot);
  uint32_t fixes=0;
  for(uint8_t g=0;g<MAX_KEYGROUPS;++g){
    MultisampleKeygroup &kg=_keygroups[slot][g];
    if(kg.highNote<kg.lowNote){kg.highNote=kg.lowNote;++fixes;}
    if(kg.endPct<=kg.startPct){kg.endPct=(uint8_t)min(100,(int)kg.startPct+1);++fixes;}
    bool groupPlayable=false;
    for(uint8_t l=0;l<MAX_VELOCITY_LAYERS;++l){
      VelocityLayer &vl=kg.layer[l];
      uint8_t available=0;
      for(uint8_t r=0;r<MAX_RR_VARIANTS;++r) if(vl.rr[r].buffer&&vl.rr[r].frames) ++available;
      if(vl.enabled&&available==0){vl.enabled=false;++fixes;}
      if(vl.enabled&&vl.velocityHigh<vl.velocityLow){vl.velocityHigh=vl.velocityLow;++fixes;}
      if(vl.loopEndPct<=vl.loopStartPct+1){vl.loopMode=0;vl.loopEndPct=(uint8_t)min(100,(int)vl.loopStartPct+2);++fixes;}
      if(vl.rrMode!=0&&vl.rrMode!=2&&vl.rrMode!=3&&vl.rrMode!=4&&vl.rrMode!=5){vl.rrMode=0;vl.rrCounter=0;++fixes;}
      if(vl.rrMode>=2&&vl.rrMode<=4&&available<vl.rrMode){
        const uint8_t fixed=(available>=2)?available:0;
        if(vl.rrMode!=fixed){vl.rrMode=fixed;vl.rrCounter=0;++fixes;}
      }
      if(vl.enabled&&available) groupPlayable=true;
      updateVelocityLayerPan(vl);
    }
    if(kg.enabled&&!groupPlayable){kg.enabled=false;++fixes;}
    else if(!kg.enabled&&groupPlayable){kg.enabled=true;++fixes;}
  }
  _msSanitizeCount+=fixes;
  return fixes;
}

void PhoenixAudioManager::setKeygroupMapping(uint8_t slot,uint8_t group,bool enabled,uint8_t low,uint8_t high,uint8_t root,uint8_t level,int8_t pan){
  slot=safeSlot(slot);group&=15;MultisampleKeygroup &kg=_keygroups[slot][group];kg.enabled=enabled;kg.lowNote=constrain((int)low,0,127);kg.highNote=constrain((int)high,0,127);if(kg.highNote<kg.lowNote)kg.highNote=kg.lowNote;kg.rootNote=constrain((int)root,0,127);setKeygroupLayerMapping(slot,group,0,enabled,1,127,level,pan);
}
void PhoenixAudioManager::setKeygroupChokeGroup(uint8_t slot,uint8_t group,uint8_t chokeGroup){
  _keygroups[safeSlot(slot)][group&15].chokeGroup=constrain((int)chokeGroup,0,8);
}
void PhoenixAudioManager::setKeygroupOneShot(uint8_t slot,uint8_t group,bool oneShot){
  _keygroups[safeSlot(slot)][group&15].oneShot=oneShot;
}
void PhoenixAudioManager::setKeygroupChokeFadeMs(uint8_t slot,uint8_t group,uint16_t fadeMs){
  _keygroups[safeSlot(slot)][group&15].chokeFadeMs=(uint16_t)constrain((int)fadeMs,0,250);
}
void PhoenixAudioManager::setKeygroupPlayMode(uint8_t slot,uint8_t group,uint8_t playMode){
  _keygroups[safeSlot(slot)][group&15].playMode=(uint8_t)constrain((int)playMode,0,2);
}
void PhoenixAudioManager::setKeygroupExclusiveGroup(uint8_t slot,uint8_t group,uint8_t exclusiveGroup){
  _keygroups[safeSlot(slot)][group&15].exclusiveGroup=(uint8_t)constrain((int)exclusiveGroup,0,8);
}
void PhoenixAudioManager::setKeygroupRetriggerLegato(uint8_t slot,uint8_t group,bool legato){
  _keygroups[safeSlot(slot)][group&15].retriggerLegato=legato;
}
void PhoenixAudioManager::setKeygroupStartPct(uint8_t slot,uint8_t group,uint8_t pct){ auto &kg=_keygroups[safeSlot(slot)][group&15]; kg.startPct=(uint8_t)constrain((int)pct,0,99); if(kg.startPct>=kg.endPct)kg.startPct=kg.endPct-1; }
void PhoenixAudioManager::setKeygroupEndPct(uint8_t slot,uint8_t group,uint8_t pct){ auto &kg=_keygroups[safeSlot(slot)][group&15]; kg.endPct=(uint8_t)constrain((int)pct,1,100); if(kg.endPct<=kg.startPct)kg.endPct=kg.startPct+1; }
void PhoenixAudioManager::setKeygroupReverse(uint8_t slot,uint8_t group,bool reverse){ _keygroups[safeSlot(slot)][group&15].reversePlayback=reverse; }
void PhoenixAudioManager::setKeygroupTranspose(uint8_t slot,uint8_t group,int8_t semitone){ _keygroups[safeSlot(slot)][group&15].transposeSemitone=(int8_t)constrain((int)semitone,-24,24); }
void PhoenixAudioManager::setKeygroupFineCent(uint8_t slot,uint8_t group,int16_t cents){ _keygroups[safeSlot(slot)][group&15].fineCent=(int16_t)constrain((int)cents,-100,100); }
void PhoenixAudioManager::setKeygroupKeytrackPct(uint8_t slot,uint8_t group,uint8_t pct){ _keygroups[safeSlot(slot)][group&15].keytrackPct=(uint8_t)constrain((int)pct,0,100); }
void PhoenixAudioManager::setKeygroupLayerLoop(uint8_t slot,uint8_t group,uint8_t layer,uint8_t mode,uint8_t startPct,uint8_t endPct,uint8_t xfadeMs){
  VelocityLayer &vl=_keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS];
  vl.loopMode=(mode>2)?2:mode; vl.loopStartPct=(uint8_t)constrain((int)startPct,0,98); vl.loopEndPct=(uint8_t)constrain((int)endPct,1,100);
  if(vl.loopEndPct<=vl.loopStartPct+1) vl.loopEndPct=(uint8_t)min(100,(int)vl.loopStartPct+2); vl.loopXfadeMs=(uint8_t)constrain((int)xfadeMs,0,50);
}
void PhoenixAudioManager::setKeygroupLayerVelocityResponse(uint8_t slot,uint8_t group,uint8_t layer,uint8_t velToLevelPct,uint8_t velToFilterPct){
  VelocityLayer &vl=_keygroups[safeSlot(slot)][group&15].layer[layer%MAX_VELOCITY_LAYERS];
  vl.velocityToLevelPct=(uint8_t)constrain((int)velToLevelPct,0,100);
  vl.velocityToFilterPct=(uint8_t)constrain((int)velToFilterPct,0,100);
}
void PhoenixAudioManager::setKeygroupLayerMapping(uint8_t slot,uint8_t group,uint8_t layer,bool enabled,uint8_t velocityLow,uint8_t velocityHigh,uint8_t level,int8_t pan){
  slot=safeSlot(slot);group&=15;layer%=MAX_VELOCITY_LAYERS;VelocityLayer &vl=_keygroups[slot][group].layer[layer];vl.enabled=enabled;vl.velocityLow=constrain((int)velocityLow,1,127);vl.velocityHigh=constrain((int)velocityHigh,0,127);if(vl.enabled&&vl.velocityHigh<vl.velocityLow)vl.velocityHigh=vl.velocityLow;vl.level=constrain((int)level,0,100);vl.pan=constrain((int)pan,-100,100);updateVelocityLayerPan(vl);if(enabled)_keygroups[slot][group].enabled=true;
}

void PhoenixAudioManager::removeKeygroup(uint8_t slot,uint8_t group){slot=safeSlot(slot);group&=15;MultisampleKeygroup &kg=_keygroups[slot][group];for(uint8_t i=0;i<PHX_VOICE_COUNT;++i)if(_voices[i].active&&_voices[i].slot==slot&&_voices[i].keygroup==group)_voices[i].active=false;for(uint8_t l=0;l<MAX_VELOCITY_LAYERS;++l){for(uint8_t r=0;r<MAX_RR_VARIANTS;++r){if(kg.layer[l].rr[r].buffer){free(kg.layer[l].rr[r].buffer);kg.layer[l].rr[r].buffer=nullptr;kg.layer[l].rr[r].frames=0;}}}kg=MultisampleKeygroup();}

void PhoenixAudioManager::clearMultisample(uint8_t slot){slot=safeSlot(slot);allNotesOff(slot,true);for(uint8_t g=0;g<MAX_KEYGROUPS;++g){MultisampleKeygroup &kg=_keygroups[slot][g];for(uint8_t l=0;l<MAX_VELOCITY_LAYERS;++l)for(uint8_t r=0;r<MAX_RR_VARIANTS;++r)if(kg.layer[l].rr[r].buffer){free(kg.layer[l].rr[r].buffer);kg.layer[l].rr[r].buffer=nullptr;kg.layer[l].rr[r].frames=0;}kg=MultisampleKeygroup();}}

bool PhoenixAudioManager::startMultisampleMidi(uint8_t slot,uint8_t midiNote,uint8_t velocity,bool reverse){
  return startMultisampleMidiOwned(slot,midiNote,velocity,reverse) >= 0;
}

int PhoenixAudioManager::startMultisampleMidiOwned(uint8_t slot,uint8_t midiNote,uint8_t velocity,bool reverse){
  int firstVoice=-1; slot=safeSlot(slot); bool noteMapped=false; bool velocityMapped=false;
  for(uint8_t g=0;g<MAX_KEYGROUPS;++g){
    MultisampleKeygroup &kg=_keygroups[slot][g];
    if(!kg.enabled||midiNote<kg.lowNote||midiNote>kg.highNote) continue;
    noteMapped=true;
    int selected=-1;
    for(uint8_t l=0;l<MAX_VELOCITY_LAYERS;++l){
      VelocityLayer &vl=kg.layer[l];
      if(vl.enabled&&keygroupLayerRoundRobinCount(slot,g,l)>0&&velocity>=vl.velocityLow&&velocity<=vl.velocityHigh){selected=l;break;}
    }
    if(selected>=0){
      velocityMapped=true;
      const int voiceId=startKeygroupVoiceOwned(slot,g,(uint8_t)selected,midiNote,velocity,reverse);
      if(firstVoice<0&&voiceId>=0) firstVoice=voiceId;
    }
    if(_slotVoiceMode[slot]!=0) break;
  }
  if(!noteMapped) ++_msNoteMissCount;
  else if(!velocityMapped) ++_msVelocityMissCount;
  return firstVoice;
}

bool PhoenixAudioManager::startKeygroupVoice(uint8_t slot,uint8_t group,uint8_t layer,uint8_t midiNote,uint8_t velocity,bool reverse){
  return startKeygroupVoiceOwned(slot,group,layer,midiNote,velocity,reverse) >= 0;
}

int PhoenixAudioManager::startKeygroupVoiceOwned(uint8_t slot,uint8_t group,uint8_t layer,uint8_t midiNote,uint8_t velocity,bool reverse){
  MultisampleKeygroup &kg=_keygroups[safeSlot(slot)][group&15];VelocityLayer &vl=kg.layer[layer%MAX_VELOCITY_LAYERS];if(!kg.enabled||!vl.enabled||velocity==0)return -1;
  // v0.7.29 Play Modes. LEGATO leaves an already active voice of this keygroup untouched.
  if(kg.retriggerLegato){for(uint8_t i=0;i<PHX_VOICE_COUNT;++i){InstrumentVoice &cv=_voices[i];if(cv.active&&cv.slot==safeSlot(slot)&&cv.keygroup==(group&15))return (int)i;}}
  if(kg.playMode==1){for(uint8_t i=0;i<PHX_VOICE_COUNT;++i){InstrumentVoice &cv=_voices[i];if(cv.active&&cv.slot==safeSlot(slot)&&cv.keygroup==(group&15)){cv.active=false;cv.releasing=false;cv.envQ15=0;}}}
  else if(kg.playMode==2&&kg.exclusiveGroup){for(uint8_t i=0;i<PHX_VOICE_COUNT;++i){InstrumentVoice &cv=_voices[i];if(!cv.active||cv.slot!=safeSlot(slot)||cv.keygroup>=MAX_KEYGROUPS)continue;const MultisampleKeygroup &other=_keygroups[safeSlot(slot)][cv.keygroup];if(other.playMode==2&&other.exclusiveGroup==kg.exclusiveGroup){cv.active=false;cv.releasing=false;cv.envQ15=0;}}}
  // Choke Group: stop matching voices immediately or with a short anti-click fade.
  if(kg.chokeGroup){
    for(uint8_t i=0;i<PHX_VOICE_COUNT;++i){
      InstrumentVoice &cv=_voices[i];
      if(!cv.active||cv.slot!=safeSlot(slot)||cv.keygroup>=MAX_KEYGROUPS)continue;
      if(_keygroups[safeSlot(slot)][cv.keygroup].chokeGroup!=kg.chokeGroup)continue;
      if(kg.chokeFadeMs==0){cv.active=false;cv.releasing=false;cv.envQ15=0;}
      else{uint32_t frames=(uint32_t)kg.chokeFadeMs*32U;uint32_t step=frames?((uint32_t)cv.envQ15/frames):32767U;if(step<1U)step=1U;if(step>32767U)step=32767U;cv.releaseStepQ15=(uint16_t)step;cv.keyHeld=false;cv.sustained=false;cv.releasing=true;cv.envStage=3;}
    }
  }
  uint8_t loaded[MAX_RR_VARIANTS];uint8_t available=0;for(uint8_t r=0;r<MAX_RR_VARIANTS;++r)if(vl.rr[r].buffer&&vl.rr[r].frames)loaded[available++]=r;if(!available)return -1;if(vl.rrMode>=2&&vl.rrMode<=4&&available<vl.rrMode)++_msRrFallbackCount;uint8_t useCount=1;if(vl.rrMode==2||vl.rrMode==3||vl.rrMode==4)useCount=vl.rrMode;else if(vl.rrMode==5)useCount=available;if(useCount>available)useCount=available;uint8_t pick=0;if(vl.rrMode==5&&useCount>1){uint32_t x=(uint32_t)vl.rrCounter*1664525UL+1013904223UL+_voiceAgeCounter;pick=(uint8_t)(x%useCount);vl.rrCounter++;}else if(useCount>1){pick=(uint8_t)(vl.rrCounter%useCount);vl.rrCounter=(uint8_t)((vl.rrCounter+1)%useCount);}uint8_t rrIndex=loaded[pick];RoundRobinVariant *rv=&vl.rr[rrIndex];
  int vi=allocateVoiceForSlot(slot);if(vi<0)vi=allocateVoice();InstrumentVoice &v=_voices[vi];v.active=false;v.releasing=false;v.keyHeld=true;v.sustained=false;v.envStage=0;v.slot=slot;v.note=midiNote;v.velocity=velocity;v.velocityLevel=(uint8_t)(127U-(((uint32_t)(127U-velocity)*(uint32_t)vl.velocityToLevelPct)/100U));v.velocityFilterAmount=vl.velocityToFilterPct;v.keygroup=group&15;v.velocityLayer=layer%MAX_VELOCITY_LAYERS;v.sourceBuffer=rv->buffer;v.sourceFrames=rv->frames;v.sourceGainLQ15=vl.gainLQ15;v.sourceGainRQ15=vl.gainRQ15;v.reverse=(reverse^kg.reversePlayback);v.rangeStart=(uint32_t)(((uint64_t)rv->frames*kg.startPct)/100ULL);v.rangeEnd=(uint32_t)(((uint64_t)rv->frames*kg.endPct)/100ULL);if(v.rangeEnd>rv->frames)v.rangeEnd=rv->frames;if(v.rangeEnd<=v.rangeStart)v.rangeEnd=(v.rangeStart<rv->frames)?v.rangeStart+1:rv->frames;v.loopMode=v.reverse?0:vl.loopMode;v.loopForward=true;const uint32_t span=v.rangeEnd-v.rangeStart;v.loopStartFrame=v.rangeStart+(uint32_t)(((uint64_t)span*vl.loopStartPct)/100ULL);v.loopEndFrame=v.rangeStart+(uint32_t)(((uint64_t)span*vl.loopEndPct)/100ULL);if(v.loopEndFrame>v.rangeEnd)v.loopEndFrame=v.rangeEnd;if(v.loopEndFrame<=v.loopStartFrame+1)v.loopMode=0;uint32_t xf=(uint32_t)vl.loopXfadeMs*32U;if(xf>(v.loopEndFrame-v.loopStartFrame)/2U)xf=(v.loopEndFrame-v.loopStartFrame)/2U;v.loopXfadeFrames=(uint16_t)min((uint32_t)65535U,xf);v.posQ16=((uint64_t)(v.reverse?(v.rangeEnd-1):v.rangeStart))<<16;
  int cents=(int)_slotCoarse[slot]*100+(int)_slotFine[slot]+(int)kg.transposeSemitone*100+(int)kg.fineCent;if(_slotPitchTrack[slot]){int semis=(int)midiNote-(int)kg.rootNote+((int)_slotOctave[slot]*12);semis=constrain(semis,-48,48);cents+=(semis*100*(int)kg.keytrackPct)/100;}float ratio=powf(2.0f,(float)cents/1200.0f);ratio=constrain(ratio,0.25f,4.0f);v.baseIncQ16=(uint32_t)(ratio*65536.0f+0.5f);const float bendSemis=((float)_slotPitchBend[slot]/8192.0f)*(float)_slotPitchBendRange[slot];v.targetIncQ16=(uint32_t)((float)v.baseIncQ16*powf(2.0f,bendSemis/12.0f)+0.5f);if(!v.targetIncQ16)v.targetIncQ16=1;v.incQ16=v.targetIncQ16;v.glideFramesLeft=0;v.glideStepQ16=0;
  v.envQ15=0;v.sustainQ15=(uint16_t)(((uint32_t)_slotSustainPct[slot]*32767U)/100U);const uint32_t af=(uint32_t)_slotAttackMs[slot]*32U,df=(uint32_t)_slotDecayMs[slot]*32U,rf=(uint32_t)_slotReleaseMs[slot]*32U;uint32_t as=af?32767U/af:32767U;if(as<1U)as=1U;if(as>32767U)as=32767U;const uint32_t envSpan=32767U-(uint32_t)v.sustainQ15;uint32_t ds=(df&&envSpan)?envSpan/df:32767U;if(ds<1U)ds=1U;if(ds>32767U)ds=32767U;uint32_t rs=rf?32767U/rf:32767U;if(rs<1U)rs=1U;if(rs>32767U)rs=32767U;v.attackStepQ15=(uint16_t)as;v.decayStepQ15=(uint16_t)ds;v.releaseStepQ15=(uint16_t)rs;
  v.age=++_voiceAgeCounter;v.vintagePhase=0;v.vintageHold=0;v.vintageFilterState=0;v.vintageNoise=0x5A5A1234UL^v.age;v.filterEnvStage=0;v.filterEnvQ15=0;v.filterLow=0;v.filterBand=0;v.filterEnvSustainQ15=(uint16_t)(((uint32_t)_slotFilterSustainPct[slot]*32767UL)/100UL);v.filterAttackStepQ15=32767;v.filterDecayStepQ15=1;v.filterReleaseStepQ15=1;v.seqStartDelayFrames=0;v.active=true;_selectedSlot=slot;_playSlot=slot;_testTone=TONE_THRU;refreshActiveVoiceCount();return vi;
}

void PhoenixAudioManager::setPitchBendRange(uint8_t slot, uint8_t semitones) {
  slot = safeSlot(slot);
  static const uint8_t allowed[] = {1,2,3,5,7,12,24};
  uint8_t best = 2;
  uint8_t bestDiff = 255;
  for (uint8_t i=0;i<sizeof(allowed);++i) {
    uint8_t d = (allowed[i] > semitones) ? (allowed[i]-semitones) : (semitones-allowed[i]);
    if (d < bestDiff) { bestDiff=d; best=allowed[i]; }
  }
  _slotPitchBendRange[slot] = best;
  setPitchBend(slot, _slotPitchBend[slot]);
}

void PhoenixAudioManager::setPitchBend(uint8_t slot, int16_t bend14) {
  slot = safeSlot(slot);
  if (bend14 < -8192) bend14 = -8192;
  if (bend14 > 8191) bend14 = 8191;
  _slotPitchBend[slot] = bend14;
  const float bendSemis = ((float)bend14 / 8192.0f) * (float)_slotPitchBendRange[slot];
  const float ratio = powf(2.0f, bendSemis / 12.0f);
  for (uint8_t i=0;i<VOICE_COUNT;++i) {
    InstrumentVoice &v = _voices[i];
    if (v.active && v.slot == slot) {
      uint32_t inc = (uint32_t)((float)v.baseIncQ16 * ratio + 0.5f);
      v.targetIncQ16 = inc ? inc : 1;
      if (v.glideFramesLeft == 0) v.incQ16 = v.targetIncQ16;
    }
  }
}

void PhoenixAudioManager::noteOffMidi(uint8_t midiNote) {
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    InstrumentVoice &v = _voices[i];
    if (!v.active || v.note != midiNote) continue;
    if (v.keygroup < MAX_KEYGROUPS && _keygroups[safeSlot(v.slot)][v.keygroup].oneShot) continue;
    v.keyHeld = false;
    if (_slotSustainDown[safeSlot(v.slot)]) {
      v.sustained = true;
    } else {
      beginVoiceRelease(v);
    }
  }
}

void PhoenixAudioManager::noteOffMidi(uint8_t slot, uint8_t midiNote) {
  slot = safeSlot(slot);
  midiNote &= 127U;

  // C031: an orphan Note Off must never release a newly stolen/reused voice.
  if (_noteHoldCount[slot][midiNote] == 0U) { ++_orphanNoteOffCount; return; }
  --_noteHoldCount[slot][midiNote];
  _heldNotes[slot][midiNote] = (_noteHoldCount[slot][midiNote] != 0U);

  if (_slotVoiceMode[slot] != 0) {
    int vi = findActiveVoiceForSlot(slot);
    if (vi >= 0 && _voices[vi].note == midiNote) {
      int next = selectHeldNote(slot);
      if (next >= 0) {
        const uint8_t savedDepth = _noteHoldCount[slot][next];
        startPlaybackMidi(slot, (uint8_t)next, _heldVelocity[slot][next], _voices[vi].reverse);
        _noteHoldCount[slot][next] = savedDepth;
        _heldNotes[slot][next] = (savedDepth != 0U);
        return;
      }
    } else return;
  }

  // Poly mode: one Note Off consumes one matching held voice (FIFO).
  const int vi = findOldestHeldVoiceForNote(slot, midiNote);
  if (vi < 0) { ++_orphanNoteOffCount; return; }
  InstrumentVoice &v = _voices[(uint8_t)vi];
  if (v.keygroup < MAX_KEYGROUPS && _keygroups[slot][v.keygroup].oneShot) { v.keyHeld = false; return; }
  v.keyHeld = false;
  if (_slotSustainDown[slot]) { v.sustained = true; ++_sustainDeferredNoteOffCount; }
  else beginVoiceRelease(v);
}

void PhoenixAudioManager::setSustain(uint8_t slot, bool down) {
  slot = safeSlot(slot);
  const bool wasDown = _slotSustainDown[slot];
  _slotSustainDown[slot] = down;
  if (wasDown && !down) {
    for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
      InstrumentVoice &v = _voices[i];
      if (v.active && v.slot == slot && !v.keyHeld && v.sustained && !(v.keygroup < MAX_KEYGROUPS && _keygroups[slot][v.keygroup].oneShot)) {
        v.sustained = false;
        beginVoiceRelease(v);
      }
    }
  }
}

void PhoenixAudioManager::allNotesOff(uint8_t slot, bool immediate) {
  slot = safeSlot(slot);
  for (uint16_t n=0;n<128;++n) { _heldNotes[slot][n]=false; _noteHoldCount[slot][n]=0; }
  _slotSustainDown[slot] = false;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    InstrumentVoice &v = _voices[i];
    if (!v.active || v.slot != slot) continue;
    if (immediate) ++_panicKillCount;
    v.keyHeld = false;
    v.sustained = false;
    if (immediate) {
      v.active = false;
      v.releasing = false;
      v.envQ15 = 0;
    } else {
      beginVoiceRelease(v);
    }
  }
  refreshActiveVoiceCount();
}

void PhoenixAudioManager::allNotesOff() {
  for (uint8_t slot = 0; slot < SLOT_COUNT; ++slot) allNotesOff(slot, true);
}


void PhoenixAudioManager::setEnvelopeParams(uint8_t slot, uint16_t attackMs, uint16_t decayMs, uint8_t sustainPct, uint16_t releaseMs) {
  slot = safeSlot(slot);
  _slotAttackMs[slot] = constrain((int)attackMs, 0, 2000);
  _slotDecayMs[slot] = constrain((int)decayMs, 0, 5000);
  _slotSustainPct[slot] = constrain((int)sustainPct, 0, 100);
  _slotReleaseMs[slot] = constrain((int)releaseMs, 0, 5000);
}


void PhoenixAudioManager::setQuattroConfig(uint8_t mode, const uint8_t low[4], const uint8_t high[4], const uint8_t channel[4]) {
  _quattroMode = mode ? 1 : 0;
  for (uint8_t i=0;i<SLOT_COUNT;++i) {
    _quattroKeyLow[i] = low ? (uint8_t)constrain((int)low[i],0,127) : (uint8_t)(i*32);
    _quattroKeyHigh[i] = high ? (uint8_t)constrain((int)high[i],0,127) : (uint8_t)((i==3)?127:(i*32+31));
    if (_quattroKeyHigh[i] < _quattroKeyLow[i]) _quattroKeyHigh[i]=_quattroKeyLow[i];
    _quattroMidiChannel[i] = channel ? constrain((int)channel[i],1,16) : (i+2);
  }
}

void PhoenixAudioManager::setVintageParams(uint8_t slot, uint8_t preset, uint8_t sampleRateIndex, uint8_t bitDepthIndex, uint8_t filterMode, uint8_t jitter) {
  slot = safeSlot(slot);
  _slotVintagePreset[slot] = (preset > 7) ? 7 : preset;
  _slotVintageSampleRate[slot] = (sampleRateIndex > 8) ? 8 : sampleRateIndex;
  _slotVintageBitDepth[slot] = (bitDepthIndex > 6) ? 6 : bitDepthIndex;
  _slotVintageFilter[slot] = (filterMode > 3) ? 3 : filterMode;
  _slotVintageJitter[slot] = (jitter > 100) ? 100 : jitter;
}

int16_t PhoenixAudioManager::processVintage(uint8_t slot, int16_t input, uint32_t &phase, int16_t &hold, int32_t &filterState, uint32_t &noise) {
  slot = safeSlot(slot);
  // C029c neutral Vintage fast path. At 32 kHz / 16 bit / FILTER OFF /
  // JITTER 0 the Vintage stage is exactly transparent, so do no per-sample
  // RNG/hold/filter bookkeeping. If Vintage is enabled while a note is already
  // sounding, the Vintage state starts from its last active state/voice default
  // on the next sample; the neutral audio output itself remains bit-identical.
  if (_slotVintageSampleRate[slot] == 0 && _slotVintageBitDepth[slot] == 0 &&
      _slotVintageFilter[slot] == 0 && _slotVintageJitter[slot] == 0) return input;
  static const uint16_t rates[9] = {32000, 24000, 22050, 16000, 12000, 11025, 8000, 6000, 4000};
  static const uint8_t bits[7] = {16, 14, 12, 10, 8, 6, 4};
  const uint16_t targetRate = rates[_slotVintageSampleRate[slot]];
  noise = noise * 1664525UL + 1013904223UL;
  int32_t jitter = 0;
  if (_slotVintageJitter[slot] > 0) {
    int32_t rnd = (int32_t)((noise >> 24) & 0xFF) - 128;
    jitter = (rnd * (int32_t)_slotVintageJitter[slot] * 96) / 100;
  }
  int32_t inc = (int32_t)targetRate * 2048 + jitter;
  if (inc < 1024) inc = 1024;
  if (phase == 0) hold = input;
  phase += (uint32_t)inc;
  if (phase >= 32000UL * 2048UL) {
    phase -= 32000UL * 2048UL;
    hold = input;
  }
  int32_t x = hold;
  const uint8_t bd = bits[_slotVintageBitDepth[slot]];
  if (bd < 16) {
    const uint8_t shift = 16 - bd;
    x = (x >> shift) << shift;
  }
  const uint8_t fm = _slotVintageFilter[slot];
  if (fm > 0) {
    static const uint8_t alphaShift[4] = {0, 2, 3, 4};
    filterState += ((x << 8) - filterState) >> alphaShift[fm];
    x = filterState >> 8;
  } else {
    filterState = x << 8;
  }
  return phxClamp16(x);
}
uint32_t PhoenixAudioManager::effectiveSampleStart(const SampleSlot &s) const {
  // C019: S.START and S.END are always the audible sample boundaries.
  // TRIM is now an automatic marker operation with undo, not a playback gate.
  return (s.sampleStart < s.frames) ? s.sampleStart : 0U;
}

uint32_t PhoenixAudioManager::effectiveSampleEnd(const SampleSlot &s) const {
  uint32_t end = s.sampleEnd;
  if (end == 0U || end > s.frames) end = s.frames;
  return end;
}

void PhoenixAudioManager::resolveSlotPlaybackMarkers(uint8_t slot, uint32_t &sampleStart, uint32_t &sampleEnd,
                                                       uint32_t &loopStart, uint32_t &loopEnd, uint8_t &loopMode) const {
  // C032: one canonical interpretation of S.START/L.START/L.END/S.END for
  // MIDI voices, live marker edits and the Sample Editor transport. End
  // markers are exclusive: audible sample frames are [S.START, S.END), and
  // loop frames are [L.START, L.END).
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  sampleStart = effectiveSampleStart(s);
  sampleEnd = effectiveSampleEnd(s);
  if (sampleEnd > s.frames) sampleEnd = s.frames;
  if (sampleStart >= s.frames) sampleStart = 0U;
  if (sampleEnd <= sampleStart + 1U) {
    sampleStart = 0U;
    sampleEnd = s.frames;
  }

  loopStart = s.loopStart;
  loopEnd = s.loopEnd;
  if (loopStart < sampleStart || loopStart >= sampleEnd) loopStart = sampleStart;
  if (loopEnd == 0U || loopEnd > sampleEnd) loopEnd = sampleEnd;

  loopMode = (s.loopEnabled && s.loopMode != LOOP_OFF)
      ? ((s.loopMode <= LOOP_ALTERNATE) ? s.loopMode : LOOP_FORWARD)
      : LOOP_OFF;
  if (loopEnd <= loopStart + 1U) loopMode = LOOP_OFF;
}

void PhoenixAudioManager::calculateSlotProcessing(SampleSlot &s) {
  s.dcOffset = 0;
  s.normalizeGainQ16 = 65536UL;
  if (!s.buffer || s.frames == 0U || (!s.dcCorrectionEnabled && !s.normalizeEnabled)) return;
  uint32_t begin = s.sampleStart;
  uint32_t end = s.sampleEnd;
  if (end == 0U || end > s.frames) end = s.frames;
  if (begin >= end) { begin = 0U; end = s.frames; }
  int64_t sum = 0;
  for (uint32_t i = begin; i < end; ++i) sum += s.buffer[i];
  const uint32_t count = end - begin;
  const int32_t dc = count ? (int32_t)(sum / (int64_t)count) : 0;
  if (s.dcCorrectionEnabled) s.dcOffset = (int16_t)constrain(dc, -32768, 32767);
  if (s.normalizeEnabled) {
    uint32_t peak = 0U;
    const int32_t appliedDc = s.dcCorrectionEnabled ? s.dcOffset : 0;
    for (uint32_t i = begin; i < end; ++i) {
      int32_t v = (int32_t)s.buffer[i] - appliedDc;
      uint32_t a = (uint32_t)(v < 0 ? -v : v);
      if (a > peak) peak = a;
    }
    // -1 dBFS target (29204), Q16 gain, never amplify beyond 16x.
    if (peak > 0U) {
      uint64_t g = (29204ULL << 16) / peak;
      if (g > (16ULL << 16)) g = (16ULL << 16);
      s.normalizeGainQ16 = (uint32_t)g;
    }
  }
}

void PhoenixAudioManager::refreshSlotProcessing(uint8_t slot) {
  slot = safeSlot(slot);
  calculateSlotProcessing(_slots[slot]);
}

void PhoenixAudioManager::setTrimEnabled(uint8_t slot, bool enabled) {
  // Compatibility/state setter. The SAMPLE EDITOR uses applyAutoTrim()/
  // undoAutoTrim() so the previous marker set can be restored exactly.
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (s.trimEnabled == enabled) return;
  s.trimEnabled = enabled;
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, false);
}

bool PhoenixAudioManager::applyAutoTrim(uint8_t slot, uint32_t detectedStart, uint32_t detectedEnd) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || !s.recorded || s.frames < 3U || s.trimEnabled) return false;

  if (detectedStart >= s.frames) detectedStart = s.frames - 1U;
  if (detectedEnd == 0U || detectedEnd > s.frames) detectedEnd = s.frames;
  if (detectedEnd <= detectedStart + 1U) return false;
  if (detectedStart == s.sampleStart && detectedEnd == s.sampleEnd) return false;

  // Save all four markers, not only S.START/S.END. Cascading boundary changes
  // may also move loop points, and a second F1 press must undo that exactly.
  s.trimUndoSampleStart = s.sampleStart;
  s.trimUndoLoopStart = s.loopStart;
  s.trimUndoLoopEnd = s.loopEnd;
  s.trimUndoSampleEnd = s.sampleEnd;
  s.trimUndoValid = true;

  const uint32_t oldStart = s.sampleStart;
  s.sampleStart = detectedStart;
  s.sampleEnd = detectedEnd;

  // Preserve existing loop points where possible; push them into the newly
  // detected sample range only when they would otherwise become invalid.
  if (s.loopStart < s.sampleStart) s.loopStart = s.sampleStart;
  if (s.loopEnd == 0U || s.loopEnd > s.sampleEnd) s.loopEnd = s.sampleEnd;
  if (s.loopEnd <= s.loopStart + 1U) {
    s.loopStart = s.sampleStart;
    s.loopEnd = s.sampleEnd;
  }

  s.trimEnabled = true;
  normalizeSlotMarkers(s);
  calculateSlotProcessing(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, s.sampleStart != oldStart);
  return true;
}

bool PhoenixAudioManager::undoAutoTrim(uint8_t slot) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.trimEnabled) return false;

  const uint32_t oldStart = s.sampleStart;
  if (s.trimUndoValid) {
    s.sampleStart = s.trimUndoSampleStart;
    s.loopStart = s.trimUndoLoopStart;
    s.loopEnd = s.trimUndoLoopEnd;
    s.sampleEnd = s.trimUndoSampleEnd;
  } else {
    // Migration fallback for C018 banks, which stored no undo snapshot.
    s.sampleStart = 0U;
    s.sampleEnd = s.frames;
    if (s.loopStart >= s.sampleEnd) s.loopStart = 0U;
    if (s.loopEnd == 0U || s.loopEnd > s.sampleEnd) s.loopEnd = s.sampleEnd;
  }

  s.trimEnabled = false;
  s.trimUndoValid = false;
  normalizeSlotMarkers(s);
  calculateSlotProcessing(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, s.sampleStart != oldStart);
  return true;
}

bool PhoenixAudioManager::getTrimUndoMarkers(uint8_t slot, uint32_t &sampleStart,
                                               uint32_t &loopStart, uint32_t &loopEnd,
                                               uint32_t &sampleEnd) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  sampleStart = s.trimUndoSampleStart;
  loopStart = s.trimUndoLoopStart;
  loopEnd = s.trimUndoLoopEnd;
  sampleEnd = s.trimUndoSampleEnd;
  return s.trimUndoValid;
}
void PhoenixAudioManager::setDcCorrectionEnabled(uint8_t slot, bool enabled) {
  slot = safeSlot(slot); SampleSlot &s = _slots[slot];
  s.dcCorrectionEnabled = enabled; calculateSlotProcessing(s);
}
void PhoenixAudioManager::setNormalizeEnabled(uint8_t slot, bool enabled) {
  slot = safeSlot(slot); SampleSlot &s = _slots[slot];
  s.normalizeEnabled = enabled; calculateSlotProcessing(s);
}
bool PhoenixAudioManager::trimEnabled(uint8_t slot) const { return _slots[safeSlot(slot)].trimEnabled; }
bool PhoenixAudioManager::dcCorrectionEnabled(uint8_t slot) const { return _slots[safeSlot(slot)].dcCorrectionEnabled; }
bool PhoenixAudioManager::normalizeEnabled(uint8_t slot) const { return _slots[safeSlot(slot)].normalizeEnabled; }
int16_t PhoenixAudioManager::slotDcOffset(uint8_t slot) const { return _slots[safeSlot(slot)].dcOffset; }
uint32_t PhoenixAudioManager::slotNormalizeGainQ16(uint8_t slot) const { return _slots[safeSlot(slot)].normalizeGainQ16; }

void PhoenixAudioManager::normalizeSlotMarkers(SampleSlot &s) {
  if (s.frames == 0U) {
    s.sampleStart = 0;
    s.sampleEnd = 0;
    s.loopStart = 0;
    s.loopEnd = 0;
    s.loopMode = LOOP_OFF;
    s.loopXfadeMs = 0;
    s.loopEnabled = false;
    return;
  }

  if (s.sampleStart >= s.frames) s.sampleStart = 0;
  if (s.sampleEnd == 0U || s.sampleEnd > s.frames) s.sampleEnd = s.frames;
  if (s.sampleEnd <= s.sampleStart + 1U) {
    s.sampleStart = 0;
    s.sampleEnd = s.frames;
  }

  if (s.loopStart < s.sampleStart || s.loopStart >= s.sampleEnd) s.loopStart = s.sampleStart;
  if (s.loopEnd == 0U || s.loopEnd > s.sampleEnd) s.loopEnd = s.sampleEnd;
  if (s.loopEnd <= s.loopStart + 1U) {
    s.loopStart = s.sampleStart;
    s.loopEnd = s.sampleEnd;
  }

  if (s.loopMode > LOOP_ALTERNATE) s.loopMode = LOOP_FORWARD;
  if (s.loopEnd <= s.loopStart + 1U) s.loopMode = LOOP_OFF;
  s.loopEnabled = (s.loopMode != LOOP_OFF);
}

void PhoenixAudioManager::bumpSlotMarkerRevision(uint8_t slot) {
  slot = safeSlot(slot);
  // Publish the marker payload before the revision token. The audio core reads
  // the token first and then reloads the coherent slot marker set.
  __sync_synchronize();
  uint32_t next = _slotMarkerRevision[slot] + 1U;
  if (next == 0U) next = 1U;
  _slotMarkerRevision[slot] = next;
  __sync_synchronize();
}

void PhoenixAudioManager::syncVoiceMarkersFromSlot(InstrumentVoice &v, bool restartAtSampleStart) {
  if (!v.active || v.keygroup != 255U) return;
  const uint8_t slot = safeSlot(v.slot);
  SampleSlot &s = _slots[slot];
  const uint32_t revision = _slotMarkerRevision[slot];
  __sync_synchronize();

  if (!s.recorded || !s.buffer || s.frames == 0U || s.sampleEnd <= s.sampleStart + 1U) {
    v.active = false;
    v.markerRevision = revision;
    return;
  }

  uint32_t rangeStart = 0U, rangeEnd = 0U, liveLoopStart = 0U, liveLoopEnd = 0U;
  uint8_t liveLoopMode = LOOP_OFF;
  resolveSlotPlaybackMarkers(slot, rangeStart, rangeEnd, liveLoopStart, liveLoopEnd, liveLoopMode);

  v.rangeStart = rangeStart;
  v.rangeEnd = rangeEnd;
  v.loopStartFrame = liveLoopStart;
  v.loopEndFrame = liveLoopEnd;
  v.loopMode = v.reverse ? LOOP_OFF : liveLoopMode;
  {
    uint32_t xf = (uint32_t)s.loopXfadeMs * 32U;
    const uint32_t loopLen = (liveLoopEnd > liveLoopStart) ? (liveLoopEnd - liveLoopStart) : 0U;
    if (xf > loopLen / 2U) xf = loopLen / 2U;
    v.loopXfadeFrames = (uint16_t)min((uint32_t)65535U, xf);
  }
  if (v.loopMode != LOOP_ALTERNATE) v.loopForward = true;

  if (v.reverse) {
    const uint32_t pos = (uint32_t)(v.posQ16 >> 16);
    if (pos < rangeStart || pos >= rangeEnd)
      v.posQ16 = ((uint64_t)(rangeEnd - 1U)) << 16;
    v.markerRevision = revision;
    return;
  }

  if (restartAtSampleStart) {
    // Sample Start is an audible start marker, not merely a clamp. Moving it
    // while a note is held restarts the sample position without retriggering
    // the ADSR envelope.
    v.posQ16 = ((uint64_t)rangeStart) << 16;
    v.loopForward = true;
    v.markerRevision = revision;
    return;
  }

  uint32_t pos = (uint32_t)(v.posQ16 >> 16);
  if (pos < rangeStart) {
    v.posQ16 = ((uint64_t)rangeStart) << 16;
    pos = rangeStart;
  }

  const bool gateActive = !v.releasing && (v.keyHeld || v.sustained);
  if (pos >= rangeEnd) {
    if (gateActive && liveLoopMode != LOOP_OFF) {
      v.posQ16 = ((uint64_t)liveLoopStart) << 16;
      v.loopForward = true;
    } else {
      v.posQ16 = ((uint64_t)(rangeEnd - 1U)) << 16;
      beginVoiceRelease(v);
    }
  } else if (gateActive && liveLoopMode != LOOP_OFF && pos >= liveLoopEnd) {
    v.posQ16 = ((uint64_t)liveLoopStart) << 16;
    v.loopForward = true;
  } else if (liveLoopMode == LOOP_ALTERNATE && !v.loopForward && pos <= liveLoopStart) {
    v.posQ16 = ((uint64_t)liveLoopStart) << 16;
    v.loopForward = true;
  }

  v.markerRevision = revision;
}

void PhoenixAudioManager::applySlotMarkersToActivePlayback(uint8_t slot, bool restartAtSampleStart) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.recorded || !s.buffer || s.frames == 0U) return;

  uint32_t rangeStart = 0U, rangeEnd = 0U, liveLoopStart = 0U, liveLoopEnd = 0U;
  uint8_t liveLoopMode = LOOP_OFF;
  resolveSlotPlaybackMarkers(slot, rangeStart, rangeEnd, liveLoopStart, liveLoopEnd, liveLoopMode);

  // C007: Standard slot voices no longer keep stale marker snapshots. Every
  // editor change is copied into already active voices immediately. Keygroup
  // voices are intentionally excluded because their ranges belong to the
  // multisample mapping, not to the four-marker single-sample editor.
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    InstrumentVoice &v = _voices[i];
    if (!v.active || v.slot != slot || v.keygroup != 255U) continue;
    syncVoiceMarkersFromSlot(v, restartAtSampleStart);
  }

  // The F7 audition transport is also live. Explicit playback ranges are used
  // by the Loop Points editor; normal replay follows Sample Start/End directly.
  if ((_state == AUDIO_PLAY || _state == AUDIO_PLAY_REVERSE) && _playSlot == slot) {
    const uint32_t transportStart = _playRangeActive ? _playRangeStart : rangeStart;
    const uint32_t transportEnd = _playRangeActive ? _playRangeEnd : rangeEnd;
    if (transportEnd <= transportStart + 1U) {
      _state = AUDIO_THRU;
      _playRangeActive = false;
      return;
    }

    if (restartAtSampleStart && !_playRangeActive && _state == AUDIO_PLAY) {
      _playPos = rangeStart;
      _playPosQ16 = ((uint64_t)rangeStart) << 16;
      _transportLoopForward = true;
      return;
    }

    if (liveLoopMode != LOOP_ALTERNATE) _transportLoopForward = true;
    uint32_t pos = (uint32_t)(_playPosQ16 >> 16);
    if (pos < transportStart) {
      const uint32_t restart = (_state == AUDIO_PLAY_REVERSE) ? (transportEnd - 1U) : transportStart;
      _playPos = restart;
      _playPosQ16 = ((uint64_t)restart) << 16;
      _transportLoopForward = true;
    } else if (pos >= transportEnd) {
      if (_state == AUDIO_PLAY && liveLoopMode != LOOP_OFF) {
        _playPos = liveLoopStart;
        _playPosQ16 = ((uint64_t)liveLoopStart) << 16;
        _transportLoopForward = true;
      } else {
        _state = AUDIO_THRU;
        _playRangeActive = false;
        _transportLoopForward = true;
      }
    } else if (_state == AUDIO_PLAY && liveLoopMode != LOOP_OFF && pos >= liveLoopEnd) {
      _playPos = liveLoopStart;
      _playPosQ16 = ((uint64_t)liveLoopStart) << 16;
      _transportLoopForward = true;
    } else if (_state == AUDIO_PLAY && liveLoopMode == LOOP_ALTERNATE &&
               !_transportLoopForward && pos <= liveLoopStart) {
      _playPos = liveLoopStart;
      _playPosQ16 = ((uint64_t)liveLoopStart) << 16;
      _transportLoopForward = true;
    }
  }
}

void PhoenixAudioManager::setLoopEnabled(uint8_t slot, bool enabled) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  s.loopMode = enabled ? ((s.loopMode == LOOP_ALTERNATE) ? LOOP_ALTERNATE : LOOP_FORWARD) : LOOP_OFF;
  normalizeSlotMarkers(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, false);
}

void PhoenixAudioManager::setLoopMode(uint8_t slot, uint8_t mode) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  s.loopMode = (mode > LOOP_ALTERNATE) ? LOOP_ALTERNATE : mode;
  normalizeSlotMarkers(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, false);
}

uint8_t PhoenixAudioManager::loopMode(uint8_t slot) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.recorded || s.frames == 0U || !s.loopEnabled) return LOOP_OFF;
  return (s.loopMode <= LOOP_ALTERNATE) ? s.loopMode : LOOP_FORWARD;
}

void PhoenixAudioManager::setLoopCrossfadeMs(uint8_t slot, uint8_t ms) {
  slot = safeSlot(slot);
  static const uint8_t allowed[] = {0,2,4,8,16,32};
  uint8_t best = allowed[0];
  uint8_t bestDiff = 255;
  for (uint8_t i=0;i<sizeof(allowed);++i) { uint8_t d=(allowed[i]>ms)?(allowed[i]-ms):(ms-allowed[i]); if(d<bestDiff){bestDiff=d;best=allowed[i];} }
  _slots[slot].loopXfadeMs = best;
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, false);
}

uint8_t PhoenixAudioManager::loopCrossfadeMs(uint8_t slot) const {
  return _slots[safeSlot(slot)].loopXfadeMs;
}

bool PhoenixAudioManager::loopEnabled(uint8_t slot) const {
  return loopMode(slot) != LOOP_OFF;
}

uint32_t PhoenixAudioManager::loopStart(uint8_t slot) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.recorded || s.frames == 0U) return 0;
  uint32_t start = s.loopStart;
  const uint32_t sampleStartFrame = sampleStart(slot);
  const uint32_t sampleEndFrame = sampleEnd(slot);
  if (start < sampleStartFrame || start >= sampleEndFrame) start = sampleStartFrame;
  return start;
}

uint32_t PhoenixAudioManager::loopEnd(uint8_t slot) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.recorded || s.frames == 0U) return 0;
  uint32_t end = s.loopEnd;
  const uint32_t sampleEndFrame = sampleEnd(slot);
  if (end == 0U || end > sampleEndFrame) end = sampleEndFrame;
  if (end <= loopStart(slot) + 1U) end = sampleEndFrame;
  return end;
}

void PhoenixAudioManager::setLoopRange(uint8_t slot, uint32_t start, uint32_t end) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (s.frames == 0U) {
    normalizeSlotMarkers(s);
    return;
  }

  normalizeSlotMarkers(s);
  if (start < s.sampleStart) start = s.sampleStart;
  if (start >= s.sampleEnd) start = s.sampleEnd - 1U;
  if (end == 0U || end > s.sampleEnd) end = s.sampleEnd;
  if (end <= start + 1U) {
    if (start + 2U <= s.sampleEnd) end = start + 2U;
    else {
      start = (s.sampleEnd > s.sampleStart + 2U) ? (s.sampleEnd - 2U) : s.sampleStart;
      end = s.sampleEnd;
    }
  }
  s.loopStart = start;
  s.loopEnd = end;
  normalizeSlotMarkers(s);
  bumpSlotMarkerRevision(slot);

  // startPlaybackRange() is the Loop Points F7 audition path. Keep that
  // explicit audition window synchronized with edited markers.
  if ((_state == AUDIO_PLAY || _state == AUDIO_PLAY_REVERSE) &&
      _playSlot == slot && _playRangeActive) {
    _playRangeStart = s.loopStart;
    _playRangeEnd = s.loopEnd;
  }
  applySlotMarkersToActivePlayback(slot, false);
}

uint32_t PhoenixAudioManager::sampleStart(uint8_t slot) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.recorded || s.frames == 0U) return 0;
  return (s.sampleStart < s.frames) ? s.sampleStart : 0;
}

uint32_t PhoenixAudioManager::sampleEnd(uint8_t slot) const {
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.recorded || s.frames == 0U) return 0;
  uint32_t end = s.sampleEnd;
  if (end == 0U || end > s.frames) end = s.frames;
  if (end <= sampleStart(slot) + 1U) end = s.frames;
  return end;
}

void PhoenixAudioManager::setSampleRange(uint8_t slot, uint32_t start, uint32_t end) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  const uint32_t oldStart = s.sampleStart;
  if (s.frames == 0U) {
    normalizeSlotMarkers(s);
    return;
  }
  if (start >= s.frames) start = s.frames - 1U;
  if (end == 0U || end > s.frames) end = s.frames;
  if (end <= start + 1U) {
    if (start + 2U <= s.frames) end = start + 2U;
    else {
      start = (s.frames > 2U) ? s.frames - 2U : 0U;
      end = s.frames;
    }
  }
  s.sampleStart = start;
  s.sampleEnd = end;
  normalizeSlotMarkers(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, s.sampleStart != oldStart);
}

void PhoenixAudioManager::setSampleMarkers(uint8_t slot, uint32_t newSampleStart,
                                           uint32_t newLoopStart, uint32_t newLoopEnd,
                                           uint32_t newSampleEnd) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (s.frames == 0U) {
    normalizeSlotMarkers(s);
    return;
  }

  const uint32_t oldSampleStart = s.sampleStart;
  const uint32_t frames = s.frames;

  if (newSampleStart >= frames) newSampleStart = frames - 1U;
  if (newSampleEnd == 0U || newSampleEnd > frames) newSampleEnd = frames;
  if (newSampleEnd <= newSampleStart + 1U) {
    if (newSampleStart + 2U <= frames) newSampleEnd = newSampleStart + 2U;
    else {
      newSampleStart = (frames > 2U) ? frames - 2U : 0U;
      newSampleEnd = frames;
    }
  }

  if (newLoopStart < newSampleStart) newLoopStart = newSampleStart;
  if (newLoopStart >= newSampleEnd) newLoopStart = newSampleEnd - 1U;
  if (newLoopEnd == 0U || newLoopEnd > newSampleEnd) newLoopEnd = newSampleEnd;
  if (newLoopEnd <= newLoopStart + 1U) {
    if (newLoopStart + 2U <= newSampleEnd) newLoopEnd = newLoopStart + 2U;
    else {
      newLoopStart = (newSampleEnd > newSampleStart + 2U)
          ? (newSampleEnd - 2U) : newSampleStart;
      newLoopEnd = newSampleEnd;
    }
  }

  s.sampleStart = newSampleStart;
  s.loopStart = newLoopStart;
  s.loopEnd = newLoopEnd;
  s.sampleEnd = newSampleEnd;
  normalizeSlotMarkers(s);
  bumpSlotMarkerRevision(slot);
  applySlotMarkersToActivePlayback(slot, s.sampleStart != oldSampleStart);
}

void PhoenixAudioManager::setSampleGain(uint8_t slot, uint8_t gainPct) {
  _slotSampleGainPct[safeSlot(slot)] = (uint8_t)constrain((int)gainPct, 0, 200);
}

void PhoenixAudioManager::setVelocityAmount(uint8_t slot, uint8_t amountPct) {
  _slotVelocityAmount[safeSlot(slot)] = (uint8_t)constrain((int)amountPct, 0, 100);
}

bool PhoenixAudioManager::normalizeSlot(uint8_t slot) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || s.frames == 0) return false;
  int32_t peak = 0;
  for (uint32_t i = 0; i < s.frames; ++i) {
    int32_t a = s.buffer[i]; if (a < 0) a = -a; if (a > peak) peak = a;
  }
  if (peak <= 0 || peak >= 32760) return true;
  for (uint32_t i = 0; i < s.frames; ++i) {
    int32_t v = ((int32_t)s.buffer[i] * 32760L) / peak;
    s.buffer[i] = (int16_t)constrain(v, -32768L, 32767L);
    if ((i & 0x7FFU) == 0) delay(0);
  }
  return true;
}

bool PhoenixAudioManager::smartProcessSlot(uint8_t slot, uint32_t trimStart, uint32_t trimEnd,
                                                bool applyTrim, bool removeDc, bool normalize95,
                                                SmartProcessResult &result) {
  result = SmartProcessResult();
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || !s.recorded || s.frames < 2U) return false;
  if (_state == AUDIO_RECORD || (_state == AUDIO_ARMED && _recordSlot == slot)) return false;

  // No voice may read the slot while its PCM data is moved or scaled.
  allNotesOff(slot, true);

  result.originalFrames = s.frames;
  uint32_t start = trimStart;
  uint32_t end = trimEnd;
  if (start >= s.frames) start = 0;
  if (end == 0U || end > s.frames) end = s.frames;
  if (end <= start + 1U) { start = 0; end = s.frames; applyTrim = false; }

  if (applyTrim && (start > 0U || end < s.frames)) {
    const uint32_t keptFrames = end - start;
    result.removedLeadingFrames = start;
    result.removedTrailingFrames = s.frames - end;
    if (start > 0U) {
      memmove(s.buffer, s.buffer + start, (size_t)keptFrames * sizeof(int16_t));
    }
    s.frames = keptFrames;
    s.sampleStart = 0;
    s.sampleEnd = keptFrames;
    s.loopStart = 0;
    s.loopEnd = keptFrames;
    s.loopEnabled = false;
    s.loopMode = LOOP_OFF;
    result.trimApplied = true;
    result.dataChanged = true;
  } else {
    s.sampleStart = 0;
    s.sampleEnd = s.frames;
  }

  if (removeDc && s.frames > 0U) {
    int64_t sum = 0;
    for (uint32_t i = 0; i < s.frames; ++i) {
      sum += s.buffer[i];
      if ((i & 0x1FFFU) == 0U) delay(0);
    }
    int32_t dc = (int32_t)(sum / (int64_t)s.frames);
    if (dc > 8 || dc < -8) {
      for (uint32_t i = 0; i < s.frames; ++i) {
        int32_t v = (int32_t)s.buffer[i] - dc;
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        s.buffer[i] = (int16_t)v;
        if ((i & 0x1FFFU) == 0U) delay(0);
      }
      result.dcRemoved = true;
      result.removedDcOffset = (int16_t)constrain(dc, -32768L, 32767L);
      result.dataChanged = true;
    }
  }

  uint32_t peak = 0;
  for (uint32_t i = 0; i < s.frames; ++i) {
    int32_t v = s.buffer[i];
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    if (a > peak) peak = a;
    if ((i & 0x1FFFU) == 0U) delay(0);
  }

  static const uint32_t TARGET_PEAK = 31128U; // 95 percent of full scale.
  if (normalize95 && peak > 0U && peak < TARGET_PEAK) {
    result.normalizeGainX1000 = (uint32_t)(((uint64_t)TARGET_PEAK * 1000ULL + peak / 2U) / peak);
    for (uint32_t i = 0; i < s.frames; ++i) {
      int64_t scaled = ((int64_t)s.buffer[i] * (int64_t)TARGET_PEAK) / (int64_t)peak;
      if (scaled > 32767) scaled = 32767;
      else if (scaled < -32768) scaled = -32768;
      s.buffer[i] = (int16_t)scaled;
      if ((i & 0x1FFFU) == 0U) delay(0);
    }
    peak = TARGET_PEAK;
    result.normalized = true;
    result.dataChanged = true;
  }

  // Avoid Arduino/GCC min() template type conflicts (uint32_t vs unsigned long).
  normalizeSlotMarkers(s);
  result.finalPeak = (uint16_t)((peak > 32767U) ? 32767U : peak);
  result.finalFrames = s.frames;
  result.success = true;
  return true;
}

bool PhoenixAudioManager::startPlaybackRange(uint8_t slot, uint32_t start, uint32_t end, bool reverse) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || s.frames == 0) return false;
  if (end == 0 || end > s.frames) end = s.frames;
  if (start >= end) start = 0;
  if (end <= start + 1) end = s.frames;
  uint32_t frame = reverse ? (end - 1) : start;
  if (!startPlaybackInternal(slot, frame, reverse, true)) return false;
  _playRangeActive = true;
  _playRangeStart = start;
  _playRangeEnd = end;
  _transportLoopForward = true;
  return true;
}


void PhoenixAudioManager::setTriggerSettings(bool automatic, uint8_t level) {
  if (level < 1) level = 1;
  if (level > 8) level = 8;
  _triggerAuto = automatic;
  _triggerLevel = level;
}

uint32_t PhoenixAudioManager::triggerThresholdRaw() const {
  static const uint32_t table[8] = {
    0x006000UL, 0x010600UL, 0x020C00UL, 0x040000UL,
    0x080000UL, 0x100000UL, 0x400000UL, 0x5A0000UL
  };
  uint8_t idx = _triggerLevel;
  if (idx < 1) idx = 1;
  if (idx > 8) idx = 8;
  return table[idx - 1];
}

bool PhoenixAudioManager::triggerThresholdReached() const {
  // C021: compare the live ADC peak against exactly the same raw threshold
  // used by AUTO recording. This query is read-only and cannot alter slots,
  // transport state or the 32 ms pre-trigger ring.
  return _lastPeakRaw >= triggerThresholdRaw();
}

bool PhoenixAudioManager::samplePlayheadActive(uint8_t slot) const {
  slot = safeSlot(slot);
  if (_slotVoicePlayheadActive[slot]) return true;
  const uint8_t state = _state;
  return (state == AUDIO_PLAY || state == AUDIO_PLAY_REVERSE) && safeSlot(_playSlot) == slot;
}

uint32_t PhoenixAudioManager::samplePlayheadFrame(uint8_t slot) const {
  slot = safeSlot(slot);
  // MIDI voices are rendered before the F7 transport and therefore own the
  // visible playhead whenever both happen to be active at the same time.
  if (_slotVoicePlayheadActive[slot]) return _slotVoicePlayheadFrame[slot];
  const uint8_t state = _state;
  if ((state == AUDIO_PLAY || state == AUDIO_PLAY_REVERSE) && safeSlot(_playSlot) == slot) return _playPos;
  return 0U;
}

void PhoenixAudioManager::setPitchSemitone(int8_t semitone) {
  uint8_t slot = safeSlot(_selectedSlot);
  setInstrumentParams(slot, semitone, _slotFine[slot], _slotRoot[slot]);
}

void PhoenixAudioManager::selectSlot(uint8_t slot) {
  _selectedSlot = safeSlot(slot);
}

bool PhoenixAudioManager::slotRecorded(uint8_t slot) const {
  slot = safeSlot(slot);
  return _slots[slot].recorded && _slots[slot].frames > 0;
}

bool PhoenixAudioManager::slotRecording(uint8_t slot) const {
  slot = safeSlot(slot);
  return _state == AUDIO_RECORD && _recordSlot == slot;
}

bool PhoenixAudioManager::slotPlaying(uint8_t slot) const {
  slot = safeSlot(slot);
  if ((_state == AUDIO_PLAY || _state == AUDIO_PLAY_REVERSE) && _playSlot == slot) return true;
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) if (_voices[i].active && _voices[i].slot == slot) return true;
  return false;
}

uint32_t PhoenixAudioManager::slotFrames(uint8_t slot) const {
  slot = safeSlot(slot);
  return _slots[slot].frames;
}

const int16_t *PhoenixAudioManager::slotSampleBuffer(uint8_t slot) const {
  slot = safeSlot(slot);
  return _slots[slot].buffer;
}

uint32_t PhoenixAudioManager::slotCapacityFrames(uint8_t slot) const {
  slot = safeSlot(slot);
  return _slots[slot].capacityFrames;
}

void PhoenixAudioManager::setTestTone(uint8_t tone) {
  if (tone > TONE_NOISE) tone = TONE_THRU;
  _testTone = tone;
  _testFrequencyHz = 440;
  _testLevelDb = -12;
  _testAmpQ15 = PHX_TONE_AMP_Q15;
  _phase = 0;
  if (tone != TONE_THRU) _state = AUDIO_THRU;
}

void PhoenixAudioManager::setTestGenerator(uint8_t waveform, uint16_t frequencyHz, int8_t levelDb, bool outputOn) {
  if (!outputOn) {
    _testTone = TONE_THRU;
    return;
  }
  if (waveform > 4) waveform = 0;
  uint8_t tone = TONE_SINE;
  switch (waveform) {
    case 0: tone = TONE_SINE; break;
    case 1: tone = TONE_TRIANGLE; break;
    case 2: tone = TONE_SAW; break;
    case 3: tone = TONE_SQUARE; break;
    case 4: tone = TONE_NOISE; break;
  }
  if (frequencyHz < 20) frequencyHz = 20;
  if (frequencyHz > 20000) frequencyHz = 20000;
  if (levelDb > -3) levelDb = -3;
  if (levelDb < -60) levelDb = -60;
  _testFrequencyHz = frequencyHz;
  _testLevelDb = levelDb;
  _testAmpQ15 = phxDbToAmpQ15(levelDb);
  if (_testTone != tone) _phase = 0;
  _testTone = tone;
  _state = AUDIO_THRU;
}

void PhoenixAudioManager::clearClip() {
  _clipActive = false;
  _highActive = false;
  _clipHoldUntilMs = 0;
  _highHoldUntilMs = 0;
  _clipCount = 0;
}

void PhoenixAudioManager::resetSlotForNewRecording(uint8_t slot) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];

  // C022: this is the irreversible overwrite boundary. AUTO mode calls it
  // only after the threshold has actually fired; MANUAL mode calls it when
  // recording starts immediately. Merely arming AUTO never touches the slot.
  allNotesOff(slot, true);
  s.frames = 0;
  s.recorded = false;
  s.sampleStart = 0;
  s.sampleEnd = 0;
  s.loopStart = 0;
  s.loopEnd = 0;
  s.loopMode = LOOP_OFF;
  s.loopXfadeMs = 0;
  s.loopEnabled = false;
  s.trimEnabled = false;
  s.trimUndoValid = false;
  s.trimUndoSampleStart = 0;
  s.trimUndoLoopStart = 0;
  s.trimUndoLoopEnd = 0;
  s.trimUndoSampleEnd = 0;
  s.dcCorrectionEnabled = false;
  s.normalizeEnabled = false;
  s.dcOffset = 0;
  s.normalizeGainQ16 = 65536UL;
  _recordPos = 0;
  bumpSlotMarkerRevision(slot);
}

void PhoenixAudioManager::finalizeRecordedSlot(uint8_t slot) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  uint32_t frames = _recordPos;
  if (frames > s.capacityFrames) frames = s.capacityFrames;
  s.frames = frames;
  s.recorded = (frames > 0U);
  s.sampleStart = 0;
  s.sampleEnd = frames;
  s.loopStart = 0;
  s.loopEnd = frames;
  s.loopMode = LOOP_OFF;
  s.loopXfadeMs = 0;
  s.loopEnabled = false;
  s.trimEnabled = false;
  s.trimUndoValid = false;
  s.trimUndoSampleStart = 0;
  s.trimUndoLoopStart = 0;
  s.trimUndoLoopEnd = 0;
  s.trimUndoSampleEnd = 0;
  s.dcCorrectionEnabled = false;
  s.normalizeEnabled = false;
  s.dcOffset = 0;
  s.normalizeGainQ16 = 65536UL;
  bumpSlotMarkerRevision(slot);
}

bool PhoenixAudioManager::startRecording() { return startRecording(_selectedSlot); }

bool PhoenixAudioManager::startRecording(uint8_t slot) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || s.capacityFrames == 0) return false;
  _testTone = TONE_THRU;
  _selectedSlot = slot;
  _recordSlot = slot;
  _playSlot = slot;
  _recordPos = 0;
  _playPos = 0;
  _previewFrames = 0;
  _previewPos = 0;
  _playPosQ16 = 0;
  _playRangeActive = false;
  _playRangeStart = 0;
  _playRangeEnd = 0;
  _transportLoopForward = true;
  clearEchoBuffer();
  _preTriggerWrite = 0;
  _preTriggerCount = 0;
  clearClip();

  if (_triggerAuto) {
    // C022: preserve an existing sample, all markers and processing settings
    // while waiting. Cancelling AUDIO_ARMED is therefore completely safe.
    _state = AUDIO_ARMED;
  } else {
    // MANUAL starts now, so this is the deliberate overwrite boundary.
    resetSlotForNewRecording(slot);
    _state = AUDIO_RECORD;
  }
  return true;
}

bool PhoenixAudioManager::startPlaybackInternal(uint8_t slot, uint32_t frame, bool reverse, bool clearDelay) {
  slot = safeSlot(slot);
  SampleSlot &s = _slots[slot];
  if (!s.buffer || s.frames == 0) return false;
  if (frame >= s.frames) frame = s.frames - 1;

  _testTone = TONE_THRU;
  _selectedSlot = slot;
  _playSlot = slot;
  _playPos = frame;
  _playPosQ16 = ((uint64_t)frame) << 16;
  updatePlaybackIncrement(slot);
  _playRangeActive = false;
  _playRangeStart = effectiveSampleStart(s);
  _playRangeEnd = effectiveSampleEnd(s);
  _transportLoopForward = true;
  if (clearDelay) clearEchoBuffer();
  _transportVintagePhase = 0; _transportVintageHold = 0; _transportVintageFilterState = 0; _transportVintageNoise = 0x13579BDFUL;
  _lastReplayReverse = reverse;
  _state = reverse ? AUDIO_PLAY_REVERSE : AUDIO_PLAY;
  return true;
}

bool PhoenixAudioManager::startPlayback() { return startPlayback(_selectedSlot); }

bool PhoenixAudioManager::startPlayback(uint8_t slot) {
  slot = safeSlot(slot);
  return startPlaybackInternal(slot, effectiveSampleStart(_slots[slot]), false, true);
}

bool PhoenixAudioManager::startPlaybackFrom(uint32_t frame) { return startPlaybackFrom(_selectedSlot, frame); }

bool PhoenixAudioManager::startPlaybackFrom(uint8_t slot, uint32_t frame) {
  slot = safeSlot(slot);
  const uint32_t end = sampleEnd(slot);
  const uint32_t start = sampleStart(slot);
  if (end == 0U) return false;
  if (frame < start) frame = start;
  if (frame >= end) frame = end - 1U;
  return startPlaybackInternal(slot, frame, false, true);
}

bool PhoenixAudioManager::startPlaybackReverse() { return startPlaybackReverse(_selectedSlot); }

bool PhoenixAudioManager::startPlaybackReverse(uint8_t slot) {
  slot = safeSlot(slot);
  const uint32_t end = sampleEnd(slot);
  if (end == 0U) return false;
  return startPlaybackInternal(slot, end - 1U, true, true);
}

bool PhoenixAudioManager::startPlaybackReverseFrom(uint32_t frame) { return startPlaybackReverseFrom(_selectedSlot, frame); }

bool PhoenixAudioManager::startPlaybackReverseFrom(uint8_t slot, uint32_t frame) {
  slot = safeSlot(slot);
  const uint32_t start = sampleStart(slot);
  const uint32_t end = sampleEnd(slot);
  if (end == 0U) return false;
  if (frame < start) frame = start;
  if (frame >= end) frame = end - 1U;
  return startPlaybackInternal(slot, frame, true, true);
}

void PhoenixAudioManager::stopTransport() {
  allNotesOff();
  const uint8_t oldState = _state;
  if (oldState == AUDIO_RECORD) {
    finalizeRecordedSlot(_recordSlot);
    stopEchoTail();
  } else if (oldState == AUDIO_ARMED) {
    // C022: AUTO was cancelled before the threshold. The old slot remains
    // byte-for-byte and metadata-for-metadata unchanged.
    _recordPos = 0;
    _preTriggerCount = 0;
  } else if (oldState == AUDIO_PLAY || oldState == AUDIO_PLAY_REVERSE) {
    // v0.6.5 Echo Tail: stop the source immediately, but let the delay buffer decay naturally.
    startEchoTail();
  }
  _state = AUDIO_THRU;
  _playRangeActive = false;
  _transportLoopForward = true;
  _testTone = TONE_THRU;
}

bool PhoenixAudioManager::buildWaveform(uint8_t *dst, uint8_t count) const {
  return buildWaveform(_selectedSlot, dst, count);
}

bool PhoenixAudioManager::buildWaveform(uint8_t slot, uint8_t *dst, uint8_t count) const {
  if (!dst || count == 0) return false;
  for (uint8_t i = 0; i < count; ++i) dst[i] = 0;
  slot = safeSlot(slot);
  const SampleSlot &s = _slots[slot];
  if (!s.buffer || s.frames == 0) return false;

  // v0.5.6: Auto-normalized waveform display.
  // First collect one peak value per OLED bin and the global peak.
  // Then scale the bins so even quiet recordings use the available height.
  // Audio data is not changed; this affects drawing only.
  uint16_t tmp[96];
  const uint8_t n = (count > 96) ? 96 : count;
  for (uint8_t i = 0; i < n; ++i) tmp[i] = 0;

  const uint32_t frames = s.frames;
  uint16_t globalPeak = 0;

  for (uint8_t i = 0; i < n; ++i) {
    uint32_t start = ((uint32_t)i * frames) / n;
    uint32_t end   = ((uint32_t)(i + 1) * frames) / n;
    if (end <= start) end = start + 1;
    if (end > frames) end = frames;

    uint16_t peak = 0;
    for (uint32_t p = start; p < end; ++p) {
      int v = (int)s.buffer[p];
      uint16_t a = (uint16_t)(v < 0 ? -v : v);
      if (a > peak) peak = a;
    }
    tmp[i] = peak;
    if (peak > globalPeak) globalPeak = peak;
  }

  if (globalPeak < 64) {
    // Near silence: keep a flat line rather than amplifying noise.
    return true;
  }

  for (uint8_t i = 0; i < n; ++i) {
    uint32_t v = ((uint32_t)tmp[i] * 15UL) / (uint32_t)globalPeak;
    if (v > 15UL) v = 15UL;
    dst[i] = (uint8_t)v;
  }
  return true;
}


static uint32_t phxReadLE32(const uint8_t *p);
static uint16_t phxReadLE16(const uint8_t *p);

static void phxTrimLine(char *s) {
  if (!s) return;
  char *p=s; while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p;
  if (p!=s) memmove(s,p,strlen(p)+1);
  size_t n=strlen(s); while(n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n]=0;
}

struct PhxWavLoopMetadata {
  bool valid;
  uint8_t mode;          // Phoenix: 0 OFF, 1 FORWARD, 2 ALTERNATE
  uint32_t start;        // source-frame index, inclusive
  uint32_t endExclusive; // source-frame index, exclusive
  uint8_t rootNote;
  PhxWavLoopMetadata() : valid(false), mode(0), start(0), endExclusive(0), rootNote(60) {}
};

static bool phxReadWavHeaderGeneric(File &f, uint16_t &channels, uint32_t &sampleRate, uint16_t &bits, uint32_t &dataOffset, uint32_t &dataLength, PhxWavLoopMetadata *loopMeta = nullptr) {
  channels = 0; sampleRate = 0; bits = 0; dataOffset = 0; dataLength = 0;
  if (loopMeta) *loopMeta = PhxWavLoopMetadata();
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;
  bool gotFmt = false;
  bool gotData = false;
  uint16_t audioFormat = 0;
  while (f.available()) {
    uint8_t ch[8];
    if (f.read(ch, 8) != 8) break;
    uint32_t chunkSize = phxReadLE32(ch + 4);
    uint32_t dataPos = f.position();
    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fmt[40];
      uint32_t toRead = chunkSize;
      if (toRead > sizeof(fmt)) toRead = sizeof(fmt);
      if (f.read(fmt, toRead) != (int)toRead) return false;
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
      // Continue scanning: a standard WAV smpl chunk may follow the PCM data.
      f.seek(dataPos + chunkSize + (chunkSize & 1U));
    } else if (memcmp(ch, "smpl", 4) == 0) {
      // RIFF smpl: 36-byte header followed by 24 bytes per sample loop.
      uint8_t smpl[60];
      const uint32_t toRead = (chunkSize < sizeof(smpl)) ? chunkSize : (uint32_t)sizeof(smpl);
      if (toRead && f.read(smpl, toRead) != (int)toRead) return false;
      if (chunkSize > toRead) f.seek(dataPos + chunkSize + (chunkSize & 1U));
      else if (chunkSize & 1U) f.seek(dataPos + chunkSize + 1U);
      if (loopMeta && chunkSize >= 60U) {
        const uint32_t root = phxReadLE32(smpl + 12);
        const uint32_t loopCount = phxReadLE32(smpl + 28);
        const uint32_t type = phxReadLE32(smpl + 40);
        const uint32_t start = phxReadLE32(smpl + 44);
        const uint32_t endInclusive = phxReadLE32(smpl + 48);
        if (loopCount > 0U && endInclusive >= start && (type == 0U || type == 1U)) {
          loopMeta->valid = true;
          loopMeta->mode = (type == 1U) ? 2U : 1U;
          loopMeta->start = start;
          loopMeta->endExclusive = (endInclusive == 0xFFFFFFFFUL) ? endInclusive : endInclusive + 1U;
          loopMeta->rootNote = (uint8_t)constrain((int)root, 0, 127);
        }
      }
    } else {
      f.seek(dataPos + chunkSize + (chunkSize & 1));
    }
  }
  if (!gotFmt || !gotData) return false;
  if (audioFormat != 1) return false;
  if (channels < 1 || channels > 2) return false;
  if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
  if (sampleRate < 8000 || sampleRate > 48000) return false;
  return true;
}

static bool phxReadPcmFrame(File &f, uint16_t channels, uint16_t bits, int16_t &out) {
  int32_t sum = 0;
  for (uint16_t c = 0; c < channels; ++c) {
    int32_t v = 0;
    if (bits == 8) {
      int b = f.read();
      if (b < 0) return false;
      v = ((int32_t)b - 128) << 8;
    } else if (bits == 16) {
      uint8_t b[2];
      if (f.read(b, 2) != 2) return false;
      v = (int16_t)phxReadLE16(b);
    } else if (bits == 24) {
      uint8_t b[3];
      if (f.read(b, 3) != 3) return false;
      int32_t x = (int32_t)b[0] | ((int32_t)b[1] << 8) | ((int32_t)b[2] << 16);
      if (x & 0x00800000L) x |= 0xFF000000L;
      v = x >> 8;
    } else { // 32-bit PCM, keep upper 16 bits
      uint8_t b[4];
      if (f.read(b, 4) != 4) return false;
      int32_t x = (int32_t)b[0] | ((int32_t)b[1] << 8) | ((int32_t)b[2] << 16) | ((int32_t)b[3] << 24);
      v = x >> 16;
    }
    sum += v;
  }
  if (channels > 1) sum /= (int32_t)channels;
  out = phxClamp16(sum);
  return true;
}


static bool phxLoadWavToMono32kBuffer(const char *path, int16_t *buffer, uint32_t capacityFrames, uint32_t &outFrames, char *status, size_t statusLen) {
  outFrames = 0;
  if (!path || !path[0]) { if (status) snprintf(status, statusLen, "NO PATH"); return false; }
  if (!buffer || capacityFrames == 0) { if (status) snprintf(status, statusLen, "NO BUFFER"); return false; }

  File f = SD.open(path, FILE_READ);
  if (!f) { if (status) snprintf(status, statusLen, "OPEN FAILED"); return false; }

  uint16_t channels = 0, bits = 0;
  uint32_t sr = 0, dataOffset = 0, dataLength = 0;
  if (!phxReadWavHeaderGeneric(f, channels, sr, bits, dataOffset, dataLength)) {
    f.close();
    if (status) snprintf(status, statusLen, "BAD WAV");
    return false;
  }

  const uint32_t bytesPerSample = (uint32_t)bits / 8UL;
  const uint32_t bytesPerFrame = bytesPerSample * (uint32_t)channels;
  const uint32_t inFrames = bytesPerFrame ? (dataLength / bytesPerFrame) : 0;
  if (inFrames == 0) { f.close(); if (status) snprintf(status, statusLen, "EMPTY WAV"); return false; }

  uint32_t frames = (uint32_t)(((uint64_t)inFrames * 32000ULL) / (uint64_t)sr);
  if (frames == 0) frames = 1;
  if (frames > capacityFrames) frames = capacityFrames;

  // Read only the source section needed for the short preview. A single bulk SD
  // transfer is dramatically faster than thousands of 1/2/3/4-byte File reads.
  uint32_t neededSourceFrames = (uint32_t)((((uint64_t)(frames - 1UL) * (uint64_t)sr) / 32000ULL) + 1ULL);
  if (neededSourceFrames > inFrames) neededSourceFrames = inFrames;
  const size_t sourceBytes = (size_t)neededSourceFrames * (size_t)bytesPerFrame;

  uint8_t *source = psramFound() ? (uint8_t*)ps_malloc(sourceBytes) : (uint8_t*)malloc(sourceBytes);
  if (!source) { f.close(); if (status) snprintf(status, statusLen, "NO PREV RAM"); return false; }
  if (!f.seek(dataOffset)) { free(source); f.close(); if (status) snprintf(status, statusLen, "SEEK FAILED"); return false; }

  size_t got = f.read(source, sourceBytes);
  f.close();
  if (got < bytesPerFrame) { free(source); if (status) snprintf(status, statusLen, "READ FAILED"); return false; }
  uint32_t loadedSourceFrames = (uint32_t)(got / bytesPerFrame);

  for (uint32_t out = 0; out < frames; ++out) {
    uint32_t srcFrame = (uint32_t)(((uint64_t)out * (uint64_t)sr) / 32000ULL);
    if (srcFrame >= loadedSourceFrames) srcFrame = loadedSourceFrames - 1UL;
    const uint8_t *p = source + (size_t)srcFrame * bytesPerFrame;
    int32_t sum = 0;

    for (uint16_t c = 0; c < channels; ++c) {
      const uint8_t *q = p + (size_t)c * bytesPerSample;
      int32_t v = 0;
      if (bits == 8) {
        v = ((int32_t)q[0] - 128) << 8;
      } else if (bits == 16) {
        v = (int16_t)((uint16_t)q[0] | ((uint16_t)q[1] << 8));
      } else if (bits == 24) {
        int32_t x = (int32_t)q[0] | ((int32_t)q[1] << 8) | ((int32_t)q[2] << 16);
        if (x & 0x00800000L) x |= 0xFF000000L;
        v = x >> 8;
      } else {
        int32_t x = (int32_t)q[0] | ((int32_t)q[1] << 8) | ((int32_t)q[2] << 16) | ((int32_t)q[3] << 24);
        v = x >> 16;
      }
      sum += v;
    }
    if (channels > 1) sum /= (int32_t)channels;
    buffer[out] = (int16_t)(phxClamp16(sum) >> 1); // preview deliberately quieter
  }

  free(source);
  outFrames = frames;
  if (status) snprintf(status, statusLen, "PREVIEW READY");
  return true;
}

bool PhoenixAudioManager::importWavToSlot(const char *path, uint8_t slot, char *status, size_t statusLen) {
  if (status && statusLen) { status[0] = 0; }
  if (!path || !path[0]) { if (status) snprintf(status, statusLen, "NO PATH"); return false; }
  slot = safeSlot(slot);
  SampleSlot &dst = _slots[slot];
  if (!dst.buffer || dst.capacityFrames == 0) { if (status) snprintf(status, statusLen, "NO PSRAM"); return false; }

  stopTransport();
  reportProgress(0);
  File f = SD.open(path, FILE_READ);
  if (!f) { if (status) snprintf(status, statusLen, "OPEN FAILED"); return false; }

  uint16_t channels = 0, bits = 0;
  uint32_t sr = 0, dataOffset = 0, dataLength = 0;
  PhxWavLoopMetadata wavLoop;
  if (!phxReadWavHeaderGeneric(f, channels, sr, bits, dataOffset, dataLength, &wavLoop)) {
    f.close();
    if (status) snprintf(status, statusLen, "BAD WAV");
    return false;
  }

  const uint32_t bytesPerFrame = ((uint32_t)bits / 8UL) * (uint32_t)channels;
  const uint32_t inFrames = bytesPerFrame ? (dataLength / bytesPerFrame) : 0;
  if (inFrames == 0) { f.close(); if (status) snprintf(status, statusLen, "EMPTY WAV"); return false; }

  uint32_t outFrames = (uint32_t)(((uint64_t)inFrames * 32000ULL) / (uint64_t)sr);
  if (outFrames == 0) outFrames = 1;
  if (outFrames > dst.capacityFrames) outFrames = dst.capacityFrames;

  if (!f.seek(dataOffset)) { f.close(); if (status) snprintf(status, statusLen, "SEEK FAILED"); return false; }

  uint32_t srcIndex = 0;
  int16_t current = 0;
  if (!phxReadPcmFrame(f, channels, bits, current)) { f.close(); if (status) snprintf(status, statusLen, "READ FAILED"); return false; }

  for (uint32_t out = 0; out < outFrames; ++out) {
    uint32_t targetSrc = (uint32_t)(((uint64_t)out * (uint64_t)sr) / 32000ULL);
    if (targetSrc >= inFrames) targetSrc = inFrames - 1;
    while (srcIndex < targetSrc) {
      if (!phxReadPcmFrame(f, channels, bits, current)) break;
      srcIndex++;
    }
    dst.buffer[out] = current;
    if ((out & 0x3FFUL) == 0) { reportProgress((uint8_t)(((uint64_t)(out + 1U) * 100ULL) / outFrames)); delay(0); }
  }
  f.close();

  dst.frames = outFrames;
  dst.recorded = true;
  dst.sampleStart = 0;
  dst.sampleEnd = outFrames;
  dst.loopStart = 0;
  dst.loopEnd = outFrames;
  dst.loopMode = LOOP_OFF;
  dst.loopEnabled = false;
  if (wavLoop.valid) {
    uint32_t importedStart = (uint32_t)(((uint64_t)wavLoop.start * 32000ULL) / (uint64_t)sr);
    uint32_t importedEnd = (uint32_t)(((uint64_t)wavLoop.endExclusive * 32000ULL) / (uint64_t)sr);
    if (importedEnd > outFrames) importedEnd = outFrames;
    if (importedStart < importedEnd && importedEnd >= importedStart + 2U) {
      dst.loopStart = importedStart;
      dst.loopEnd = importedEnd;
      dst.loopMode = wavLoop.mode;
      dst.loopEnabled = true;
    }
  }
  dst.trimEnabled = false;
  dst.trimUndoValid = false;
  dst.trimUndoSampleStart = 0; dst.trimUndoLoopStart = 0; dst.trimUndoLoopEnd = 0; dst.trimUndoSampleEnd = 0;
  dst.dcCorrectionEnabled = false; dst.normalizeEnabled = false;
  dst.dcOffset = 0; dst.normalizeGainQ16 = 65536UL;
  _selectedSlot = slot;
  _recordSlot = slot;
  _playSlot = slot;
  _playPos = 0;
  _playPosQ16 = 0;
  _playRangeActive = false;
  _lastReplayReverse = false;
  _slotCoarse[slot] = 0;
  _slotFine[slot] = 0;
  _slotRoot[slot] = wavLoop.valid ? wavLoop.rootNote : 60;
  _slotOctave[slot] = 0;
  _slotPitchTrack[slot] = true;
  _slotVintagePreset[slot] = 0; _slotVintageSampleRate[slot] = 0; _slotVintageBitDepth[slot] = 0; _slotVintageFilter[slot] = 0; _slotVintageJitter[slot] = 0;
  updatePlaybackIncrement(slot);
  clearEchoBuffer();
  reportProgress(100);
  if (status) snprintf(status, statusLen, "IMPORTED S%u", (unsigned)(slot + 1));
  return true;
}




static bool phxAutoMapIsDelimiter(char c) {
  return c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == '[';
}


static int8_t phxAutoMapVelocityLayerFromName(const char *name) {
  if (!name) return -1;
  char base[96];
  strncpy(base, name, sizeof(base)); base[sizeof(base)-1] = 0;
  char *dot = strrchr(base, '.'); if (dot) *dot = 0;
  for (char *p = base; *p; ++p) *p = (char)tolower((unsigned char)*p);

  const char *tokens[3][6] = {
    {"_soft", "-soft", " soft", "_v1", "-v1", "_pp"},
    {"_medium", "-medium", " medium", "_med", "-med", "_v2"},
    {"_hard", "-hard", " hard", "_v3", "-v3", "_ff"}
  };
  for (uint8_t layer = 0; layer < 3; ++layer) {
    for (uint8_t i = 0; i < 6; ++i) {
      if (strstr(base, tokens[layer][i])) return (int8_t)layer;
    }
  }
  return -1;
}
static int8_t phxAutoMapRoundRobinVariantFromName(const char *name) {
  if (!name) return -1;
  char base[96];
  strncpy(base, name, sizeof(base)); base[sizeof(base)-1] = 0;
  char *dot = strrchr(base, '.'); if (dot) *dot = 0;
  for (char *p = base; *p; ++p) *p = (char)tolower((unsigned char)*p);

  // Supported suffixes/tokens: _rr1.._rr4, -rr1..-rr4, " rr1"..,
  // and compact _r1.._r4 / -r1..-r4. The delimiter requirement avoids
  // accidental matches inside instrument names.
  for (int v = 1; v <= 4; ++v) {
    char token[8];
    snprintf(token, sizeof(token), "_rr%d", v); if (strstr(base, token)) return (int8_t)(v - 1);
    snprintf(token, sizeof(token), "-rr%d", v); if (strstr(base, token)) return (int8_t)(v - 1);
    snprintf(token, sizeof(token), " rr%d", v); if (strstr(base, token)) return (int8_t)(v - 1);
    snprintf(token, sizeof(token), "_r%d", v);  if (strstr(base, token)) return (int8_t)(v - 1);
    snprintf(token, sizeof(token), "-r%d", v);  if (strstr(base, token)) return (int8_t)(v - 1);
  }
  return -1;
}

static bool phxAutoMapRootFromName(const char *name, uint8_t &root) {
  if (!name) return false;
  char base[96];
  strncpy(base, name, sizeof(base)); base[sizeof(base)-1] = 0;
  char *dot = strrchr(base, '.'); if (dot) *dot = 0;
  const int len = (int)strlen(base);

  for (int i = len - 1; i >= 0; --i) {
    if (!isdigit((unsigned char)base[i])) continue;
    int end = i;
    while (i >= 0 && isdigit((unsigned char)base[i])) --i;
    int start = i + 1;
    if (start > 0 && !phxAutoMapIsDelimiter(base[start-1])) continue;
    int digits = end - start + 1;
    if (digits < 2 || digits > 3) continue;
    int v = atoi(base + start);
    if (v >= 0 && v <= 127) { root = (uint8_t)v; return true; }
  }

  for (int i = len - 1; i >= 0; --i) {
    char c = (char)toupper((unsigned char)base[i]);
    if (c < 'A' || c > 'G') continue;
    if (i > 0 && !phxAutoMapIsDelimiter(base[i-1])) continue;
    int pc = 0;
    switch(c){case 'C':pc=0;break;case 'D':pc=2;break;case 'E':pc=4;break;case 'F':pc=5;break;case 'G':pc=7;break;case 'A':pc=9;break;default:pc=11;break;}
    int p=i+1;
    if (base[p]=='#') { pc=(pc+1)%12; ++p; }
    else if (base[p]=='b' || base[p]=='B') { pc=(pc+11)%12; ++p; }
    bool neg=false; if(base[p]=='-'){neg=true;++p;}
    if(!isdigit((unsigned char)base[p])) continue;
    int octave=0; while(isdigit((unsigned char)base[p])){octave=octave*10+(base[p]-'0');++p;}
    if(neg) octave=-octave;
    if(base[p] && !phxAutoMapIsDelimiter(base[p])) continue;
    int midi=(octave+2)*12+pc;
    if(midi>=0 && midi<=127){root=(uint8_t)midi;return true;}
  }
  return false;
}

bool PhoenixAudioManager::autoMapMultisampleFolder(const char *folder, uint8_t slot, char *status, size_t statusLen) {
  slot=safeSlot(slot);
  if(status&&statusLen) status[0]=0;
  File dir=SD.open(folder);
  if(!dir || !dir.isDirectory()){if(status)snprintf(status,statusLen,"FOLDER ERROR");if(dir)dir.close();return false;}
  struct Candidate { char path[160]; uint8_t root; } c[MAX_KEYGROUPS];
  uint8_t count=0, skipped=0;
  while(count<MAX_KEYGROUPS){
    File e=dir.openNextFile(); if(!e)break;
    if(!e.isDirectory()){
      const char *nm=e.name(); const char *bn=strrchr(nm,'/'); bn=bn?bn+1:nm;
      const char *dot=strrchr(bn,'.');
      if(dot && !strcasecmp(dot,".WAV")){
        uint8_t r=0;
        if(phxAutoMapRootFromName(bn,r)){
          snprintf(c[count].path,sizeof(c[count].path),"%s%s%s",folder,(folder[strlen(folder)-1]=='/')?"":"/",bn);
          c[count].root=r; ++count;
        } else ++skipped;
      }
    }
    e.close();
  }
  dir.close();
  if(count==0){if(status)snprintf(status,statusLen,"NO ROOT NAMES");return false;}
  for(uint8_t i=0;i<count;++i)for(uint8_t j=i+1;j<count;++j)if(c[j].root<c[i].root){Candidate t=c[i];c[i]=c[j];c[j]=t;}
  for(uint8_t i=1;i<count;++i)if(c[i].root==c[i-1].root){if(status)snprintf(status,statusLen,"DUP ROOT %u",c[i].root);return false;}

  stopTransport();
  clearMultisample(slot);
  for(uint8_t i=0;i<count;++i){
    char st[24];
    if(!importWavToKeygroup(c[i].path,slot,i,st,sizeof(st))){
      clearMultisample(slot);
      if(status)snprintf(status,statusLen,"LOAD FAIL G%u",i+1);
      return false;
    }
    uint8_t low=(i==0)?0:(uint8_t)(((uint16_t)c[i-1].root+(uint16_t)c[i].root)/2U+1U);
    uint8_t high=(i==count-1)?127:(uint8_t)(((uint16_t)c[i].root+(uint16_t)c[i+1].root)/2U);
    setKeygroupMapping(slot,i,true,low,high,c[i].root,100,0);
  }
  if(status)snprintf(status,statusLen,"MAPPED %u SKIP %u",count,skipped);
  return true;
}

bool PhoenixAudioManager::importWavToKeygroup(const char *path,uint8_t slot,uint8_t group,char *status,size_t statusLen){return importWavToKeygroupVariant(path,slot,group,0,0,status,statusLen);}
bool PhoenixAudioManager::importWavToKeygroupLayer(const char *path,uint8_t slot,uint8_t group,uint8_t layer,char *status,size_t statusLen){return importWavToKeygroupVariant(path,slot,group,layer,0,status,statusLen);}
bool PhoenixAudioManager::importWavToKeygroupVariant(const char *path,uint8_t slot,uint8_t group,uint8_t layer,uint8_t variant,char *status,size_t statusLen){
  slot=safeSlot(slot);group&=15;layer%=MAX_VELOCITY_LAYERS;variant%=MAX_RR_VARIANTS;PHX_INFO_PRINTF("V1.0.3 MS LOAD begin S%u G%u L%u R%u path=%s\n",(unsigned)(slot+1),(unsigned)(group+1),(unsigned)(layer+1),(unsigned)(variant+1),path?path:"?");vTaskDelay(pdMS_TO_TICKS(2));File f=SD.open(path,FILE_READ);if(!f){if(status)snprintf(status,statusLen,"OPEN ERROR");return false;}uint16_t ch=0,bits=0;uint32_t sr=0,dataOff=0,dataLen=0;if(!phxReadWavHeaderGeneric(f,ch,sr,bits,dataOff,dataLen)){f.close();if(status)snprintf(status,statusLen,"BAD WAV");return false;}const uint32_t bpf=(bits/8U)*ch;const uint32_t inFrames=bpf?dataLen/bpf:0;const uint32_t outFrames=(uint32_t)(((uint64_t)inFrames*32000ULL)/sr);if(!outFrames){f.close();if(status)snprintf(status,statusLen,"EMPTY WAV");return false;}int16_t*buf=(int16_t*)ps_malloc(outFrames*sizeof(int16_t));if(!buf){f.close();if(status)snprintf(status,statusLen,"NO PSRAM");return false;}f.seek(dataOff);uint32_t src=0;int16_t cur=0;reportProgress(0);for(uint32_t o=0;o<outFrames;++o){uint32_t target=(uint32_t)(((uint64_t)o*sr)/32000ULL);while(src<=target){if(!phxReadPcmFrame(f,ch,bits,cur))break;++src;}buf[o]=cur;if((o&0x3ffU)==0U){reportProgress((uint8_t)(((uint64_t)(o+1U)*100ULL)/outFrames));vTaskDelay(pdMS_TO_TICKS(2));}}reportProgress(100);f.close();MultisampleKeygroup &kg=_keygroups[slot][group];VelocityLayer &vl=kg.layer[layer];RoundRobinVariant &rv=vl.rr[variant];if(rv.buffer)free(rv.buffer);rv.buffer=buf;rv.frames=outFrames;strncpy(rv.path,path,sizeof(rv.path));rv.path[sizeof(rv.path)-1]=0;vl.enabled=true;if(layer==0&&!kg.enabled){kg.lowNote=0;kg.highNote=127;kg.rootNote=60;}kg.enabled=true;if(vl.velocityHigh<vl.velocityLow){vl.velocityLow=(layer==0)?1:(uint8_t)(layer*43+1);vl.velocityHigh=(layer==2)?127:(uint8_t)((layer+1)*43);}if(variant>0&&vl.rrMode==0)vl.rrMode=(uint8_t)(variant+1);updateVelocityLayerPan(vl);if(status)snprintf(status,statusLen,"S%u G%02u L%u R%u",slot+1,group+1,layer+1,variant+1);PHX_INFO_PRINTF("V1.0.3 MS LOAD ok S%u G%u L%u R%u frames=%lu\n",(unsigned)(slot+1),(unsigned)(group+1),(unsigned)(layer+1),(unsigned)(variant+1),(unsigned long)outFrames);vTaskDelay(pdMS_TO_TICKS(2));return true;
}

bool PhoenixAudioManager::autoMapFolder(const char *folder, uint8_t slot, char *status, size_t statusLen) {
  if (status && statusLen) status[0] = 0;
  if (!folder || !folder[0]) { if (status) snprintf(status, statusLen, "NO FOLDER"); return false; }
  slot = safeSlot(slot);

  // C014: Auto-map workspaces are temporary and can be large. Keep them out
  // of both static internal DRAM and the loop-task stack.
  struct Candidate { char path[128]; uint8_t root; int8_t layer; int8_t variant; };
  const size_t candidateCapacity = (size_t)MAX_KEYGROUPS * MAX_VELOCITY_LAYERS * MAX_RR_VARIANTS;
  Candidate *c = psramFound()
                   ? (Candidate*)ps_malloc(candidateCapacity * sizeof(Candidate))
                   : (Candidate*)malloc(candidateCapacity * sizeof(Candidate));
  if (!c) { if (status) snprintf(status, statusLen, "NO MAP WORK RAM"); return false; }

  uint16_t count = 0;
  bool tooMany = false;

  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    free(c);
    if (status) snprintf(status, statusLen, "DIR ERROR");
    return false;
  }
  while (true) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      const char *nm = e.name();
      const char *bn = strrchr(nm, '/'); bn = bn ? bn + 1 : nm;
      const char *dot = strrchr(bn, '.');
      if (dot && !strcasecmp(dot, ".WAV")) {
        uint8_t root = 0;
        if (phxAutoMapRootFromName(bn, root)) {
          if (count >= candidateCapacity) tooMany = true;
          else {
            snprintf(c[count].path, sizeof(c[count].path), "%s%s%s", folder, (folder[strlen(folder)-1] == '/') ? "" : "/", bn);
            c[count].root = root;
            c[count].layer = phxAutoMapVelocityLayerFromName(bn);
            c[count].variant = phxAutoMapRoundRobinVariantFromName(bn);
            ++count;
          }
        }
      }
    }
    e.close();
  }
  dir.close();
  if (tooMany) { free(c); if (status) snprintf(status, statusLen, "MORE THAN 192"); return false; }
  if (!count) { free(c); if (status) snprintf(status, statusLen, "NO ROOT WAVS"); return false; }

  // Sort by root, velocity layer and RR variant. Unmarked items map to L1/RR1.
  for (uint16_t i = 0; i < count; ++i) for (uint16_t j = i + 1; j < count; ++j) {
    int li = c[i].layer < 0 ? 0 : c[i].layer + 1;
    int lj = c[j].layer < 0 ? 0 : c[j].layer + 1;
    int ri = c[i].variant < 0 ? 0 : c[i].variant + 1;
    int rj = c[j].variant < 0 ? 0 : c[j].variant + 1;
    if (c[j].root < c[i].root ||
        (c[j].root == c[i].root && (lj < li || (lj == li && rj < ri)))) {
      Candidate t = c[i]; c[i] = c[j]; c[j] = t;
    }
  }

  uint8_t roots[MAX_KEYGROUPS];
  uint8_t rootCount = 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (i == 0 || c[i].root != c[i-1].root) {
      if (rootCount >= MAX_KEYGROUPS) {
        free(c);
        if (status) snprintf(status, statusLen, "MORE THAN 16 ROOTS");
        return false;
      }
      roots[rootCount++] = c[i].root;
    }
  }

  const size_t tempBytes = (size_t)MAX_KEYGROUPS * sizeof(MultisampleKeygroup);
  MultisampleKeygroup *temp = psramFound()
                                ? (MultisampleKeygroup*)ps_malloc(tempBytes)
                                : (MultisampleKeygroup*)malloc(tempBytes);
  if (!temp) {
    free(c);
    if (status) snprintf(status, statusLen, "NO MAP TEMP RAM");
    return false;
  }
  for (uint8_t g = 0; g < MAX_KEYGROUPS; ++g) new (&temp[g]) MultisampleKeygroup();

  bool used[MAX_KEYGROUPS][MAX_VELOCITY_LAYERS][MAX_RR_VARIANTS] = {};
  uint8_t rrCount[MAX_KEYGROUPS][MAX_VELOCITY_LAYERS] = {};
  uint16_t loadedFiles = 0;

  for (uint16_t i = 0; i < count; ++i) {
    uint8_t g = 0;
    while (g < rootCount && roots[g] != c[i].root) ++g;
    if (g >= rootCount) continue;

    int8_t layer = c[i].layer < 0 ? 0 : c[i].layer;
    int8_t variant = c[i].variant < 0 ? 0 : c[i].variant;
    if (used[g][layer][variant]) {
      if (status) snprintf(status, statusLen, "DUP R%u L%u R%u", c[i].root, layer + 1, variant + 1);
      goto fail;
    }

    File f = SD.open(c[i].path, FILE_READ);
    if (!f) { if (status) snprintf(status, statusLen, "OPEN G%u L%u", g + 1, layer + 1); goto fail; }
    uint16_t ch = 0, bits = 0; uint32_t sr = 0, off = 0, len = 0;
    if (!phxReadWavHeaderGeneric(f, ch, sr, bits, off, len)) { f.close(); if (status) snprintf(status, statusLen, "BAD G%u L%u", g + 1, layer + 1); goto fail; }
    uint32_t bpf = ((uint32_t)bits / 8UL) * ch;
    uint32_t inFrames = bpf ? len / bpf : 0;
    uint32_t outFrames = sr ? (uint32_t)(((uint64_t)inFrames * 32000ULL) / sr) : 0;
    if (!outFrames) { f.close(); if (status) snprintf(status, statusLen, "EMPTY G%u L%u", g + 1, layer + 1); goto fail; }
    int16_t *buf = psramFound() ? (int16_t*)ps_malloc((size_t)outFrames * 2U) : (int16_t*)malloc((size_t)outFrames * 2U);
    if (!buf) { f.close(); if (status) snprintf(status, statusLen, "NO PSRAM G%u", g + 1); goto fail; }
    if (!f.seek(off)) { free(buf); f.close(); if (status) snprintf(status, statusLen, "SEEK G%u", g + 1); goto fail; }
    uint32_t src = 0; int16_t cur = 0;
    if (!phxReadPcmFrame(f, ch, bits, cur)) { free(buf); f.close(); if (status) snprintf(status, statusLen, "READ G%u", g + 1); goto fail; }
    for (uint32_t o = 0; o < outFrames; ++o) {
      uint32_t target = (uint32_t)(((uint64_t)o * sr) / 32000ULL);
      while (src < target) { if (!phxReadPcmFrame(f, ch, bits, cur)) break; ++src; }
      buf[o] = cur;
      if ((o & 0x3ffU) == 0) delay(0);
    }
    f.close();

    MultisampleKeygroup &kg = temp[g];
    kg.enabled = true;
    kg.rootNote = c[i].root;
    VelocityLayer &vl = kg.layer[(uint8_t)layer];
    RoundRobinVariant &rv = vl.rr[(uint8_t)variant];
    rv.buffer = buf; rv.frames = outFrames; vl.enabled = true;
    vl.level = 100; vl.pan = 0;
    if (c[i].layer < 0) { vl.velocityLow = 1; vl.velocityHigh = 127; }
    else if (layer == 0) { vl.velocityLow = 1; vl.velocityHigh = 43; }
    else if (layer == 1) { vl.velocityLow = 44; vl.velocityHigh = 86; }
    else { vl.velocityLow = 87; vl.velocityHigh = 127; }
    strncpy(rv.path, c[i].path, sizeof(rv.path)); rv.path[sizeof(rv.path)-1] = 0;
    updateVelocityLayerPan(vl);
    used[g][layer][variant] = true;
    ++rrCount[g][layer];
    ++loadedFiles;
  }

  // Set RR mode automatically from the number of loaded variants.
  for (uint8_t g = 0; g < rootCount; ++g) {
    for (uint8_t l = 0; l < MAX_VELOCITY_LAYERS; ++l) {
      uint8_t n = rrCount[g][l];
      temp[g].layer[l].rrMode = (n >= 2 && n <= 4) ? n : 0;
      temp[g].layer[l].rrCounter = 0;
    }
    temp[g].lowNote = (g == 0) ? 0 : (uint8_t)(((uint16_t)roots[g-1] + (uint16_t)roots[g]) / 2U + 1U);
    temp[g].highNote = (g + 1 == rootCount) ? 127 : (uint8_t)(((uint16_t)roots[g] + (uint16_t)roots[g+1]) / 2U);
  }

  stopTransport();
  clearMultisample(slot);
  for (uint8_t g = 0; g < rootCount; ++g) {
    _keygroups[slot][g] = temp[g];
    for (uint8_t l = 0; l < MAX_VELOCITY_LAYERS; ++l)
      for (uint8_t r = 0; r < MAX_RR_VARIANTS; ++r)
        temp[g].layer[l].rr[r].buffer = nullptr;
  }
  if (status) snprintf(status, statusLen, "MAPPED %uG %uW", rootCount, (unsigned)loadedFiles);
  free(temp);
  free(c);
  return true;

fail:
  for (uint8_t g = 0; g < MAX_KEYGROUPS; ++g)
    for (uint8_t l = 0; l < MAX_VELOCITY_LAYERS; ++l)
      for (uint8_t r = 0; r < MAX_RR_VARIANTS; ++r)
        if (temp[g].layer[l].rr[r].buffer) {
          free(temp[g].layer[l].rr[r].buffer);
          temp[g].layer[l].rr[r].buffer = nullptr;
        }
  free(temp);
  free(c);
  return false;
}

bool PhoenixAudioManager::writeMultisampleConfigFile(const char *path, bool *anyOut) const {
  if (anyOut) *anyOut = false;
  if (!path || !path[0]) return false;
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  if (f.printf("VERSION=%u\n", (unsigned)PHX_MULTISAMPLE_FORMAT_VERSION) <= 0) {
    f.close(); SD.remove(path); return false;
  }

  bool any = false;
  for (uint8_t s = 0; s < SLOT_COUNT; ++s) {
    for (uint8_t g = 0; g < MAX_KEYGROUPS; ++g) {
      const MultisampleKeygroup &kg = _keygroups[s][g];
      bool groupAny = false;
      for (uint8_t l = 0; l < MAX_VELOCITY_LAYERS && !groupAny; ++l)
        for (uint8_t r = 0; r < MAX_RR_VARIANTS; ++r)
          if (kg.layer[l].rr[r].buffer && kg.layer[l].rr[r].frames) { groupAny = true; break; }
      if (!groupAny) continue;

      any = true;
      if (f.printf("[SLOT%u_GROUP%u]\n", s + 1, g + 1) <= 0 ||
          f.printf("LOW=%u\nHIGH=%u\nROOT=%u\nTRANSPOSE=%d\nFINE_CENT=%d\nKEYTRACK_PCT=%u\nCHOKE=%u\nONE_SHOT=%u\nCHOKE_FADE_MS=%u\nPLAY_MODE=%u\nEXCLUSIVE_GROUP=%u\nRETRIGGER=%u\nSTART_PCT=%u\nEND_PCT=%u\nREVERSE=%u\nENABLED=%u\n\n",
                   kg.lowNote, kg.highNote, kg.rootNote, kg.transposeSemitone, kg.fineCent, kg.keytrackPct,
                   kg.chokeGroup, kg.oneShot ? 1 : 0, kg.chokeFadeMs, kg.playMode, kg.exclusiveGroup,
                   kg.retriggerLegato ? 1 : 0, kg.startPct, kg.endPct, kg.reversePlayback ? 1 : 0,
                   kg.enabled ? 1 : 0) <= 0) {
        f.close(); SD.remove(path); return false;
      }

      for (uint8_t l = 0; l < MAX_VELOCITY_LAYERS; ++l) {
        const VelocityLayer &vl = kg.layer[l];
        bool layerAny = false;
        for (uint8_t r = 0; r < MAX_RR_VARIANTS; ++r)
          if (vl.rr[r].buffer && vl.rr[r].frames) { layerAny = true; break; }
        if (!layerAny) continue;

        if (f.printf("[SLOT%u_GROUP%u_LAYER%u]\n", s + 1, g + 1, l + 1) <= 0 ||
            f.printf("VEL_LOW=%u\nVEL_HIGH=%u\nLEVEL=%u\nPAN=%d\nENABLED=%u\nRR_MODE=%u\nLOOP_MODE=%u\nLOOP_START_PCT=%u\nLOOP_END_PCT=%u\nLOOP_XFADE_MS=%u\nVEL_TO_LEVEL=%u\nVEL_TO_FILTER=%u\n",
                     vl.velocityLow, vl.velocityHigh, vl.level, vl.pan, vl.enabled ? 1 : 0, vl.rrMode,
                     vl.loopMode, vl.loopStartPct, vl.loopEndPct, vl.loopXfadeMs,
                     vl.velocityToLevelPct, vl.velocityToFilterPct) <= 0) {
          f.close(); SD.remove(path); return false;
        }
        for (uint8_t r = 0; r < MAX_RR_VARIANTS; ++r) {
          if (vl.rr[r].buffer && vl.rr[r].frames) {
            if (f.printf("RR%u_FILE=%s\n", r + 1, vl.rr[r].path) <= 0) {
              f.close(); SD.remove(path); return false;
            }
          }
        }
        if (f.println() == 0) { f.close(); SD.remove(path); return false; }
      }
    }
  }

  f.flush();
  const bool ok = (f.size() > 0);
  f.close();
  if (!ok) { SD.remove(path); return false; }
  if (anyOut) *anyOut = any;
  return true;
}

bool PhoenixAudioManager::saveMultisampleConfig(const char *dir) const {
  if (!dir || !dir[0]) return false;
  char target[160], temp[160], backup[160];
  snprintf(target, sizeof(target), "%s/MULTISAMPLE.CFG", dir);
  snprintf(temp, sizeof(temp), "%s/MULTISAMPLE.MS0TMP", dir);
  snprintf(backup, sizeof(backup), "%s/MULTISAMPLE.MS0BAK", dir);
  // Recover a previous interrupted standalone multisample update first.
  if (SD.exists(backup)) {
    if (!SD.exists(target)) SD.rename(backup, target);
    else SD.remove(backup);
  }
  SD.remove(temp);

  bool any = false;
  if (!writeMultisampleConfigFile(temp, &any)) return false;
  if (!any) {
    SD.remove(temp);
    // Desired state is no multisample configuration.  Only remove the old file
    // after the new in-memory state has been validated successfully.
    if (SD.exists(target)) {
      if (!SD.rename(target, backup)) return false;
      SD.remove(backup);
    }
    return true;
  }

  const bool hadOld = SD.exists(target);
  if (hadOld && !SD.rename(target, backup)) { SD.remove(temp); return false; }
  if (!SD.rename(temp, target)) {
    if (hadOld) SD.rename(backup, target);
    SD.remove(temp);
    return false;
  }
  File verify = SD.open(target, FILE_READ);
  const bool ok = verify && verify.size() > 0;
  if (verify) verify.close();
  if (!ok) {
    SD.remove(target);
    if (hadOld) SD.rename(backup, target);
    return false;
  }
  SD.remove(backup);
  return true;
}

bool PhoenixAudioManager::loadMultisampleConfig(const char *dir){
  PHX_INFO_PRINTF("V1.0.3 MS CFG begin dir=%s\n",dir?dir:"?");
  for(uint8_t s=0;s<SLOT_COUNT;++s){ clearMultisample(s); vTaskDelay(pdMS_TO_TICKS(2)); }
  char p[160];snprintf(p,sizeof(p),"%s/MULTISAMPLE.CFG",dir);
  File f=SD.open(p,FILE_READ);if(!f){PHX_INFO_PRINTLN("V1.0.3 MS CFG absent");return false;}
  int cs=-1,cg=-1,cl=-1;char line[196];uint16_t lineCount=0;bool importError=false;
  while(f.available()){
    size_t n=f.readBytesUntil('\n',line,sizeof(line)-1);line[n]=0;phxTrimLine(line);
    if((++lineCount & 0x07U)==0U) vTaskDelay(pdMS_TO_TICKS(2));
    if(!line[0]||line[0]=='#')continue;
    if(line[0]=='['){
      unsigned a=0,b=0,c=0;
      if(sscanf(line,"[SLOT%u_GROUP%u_LAYER%u]",&a,&b,&c)==3&&a>=1&&a<=4&&b>=1&&b<=16&&c>=1&&c<=3){cs=a-1;cg=b-1;cl=c-1;}
      else if(sscanf(line,"[SLOT%u_GROUP%u]",&a,&b)==2&&a>=1&&a<=4&&b>=1&&b<=16){cs=a-1;cg=b-1;cl=-1;}
      continue;
    }
    char*eq=strchr(line,'=');if(!eq||cs<0||cg<0)continue;*eq=0;char*key=line;char*val=eq+1;phxTrimLine(key);phxTrimLine(val);MultisampleKeygroup &kg=_keygroups[cs][cg];
    if(cl<0){
      if(!strcmp(key,"FILE")){char st[32];if(!importWavToKeygroupVariant(val,cs,cg,0,0,st,sizeof(st)))importError=true;kg.layer[0].velocityLow=1;kg.layer[0].velocityHigh=127;}
      else if(!strcmp(key,"LOW"))kg.lowNote=constrain(atoi(val),0,127);
      else if(!strcmp(key,"HIGH"))kg.highNote=constrain(atoi(val),0,127);
      else if(!strcmp(key,"ROOT"))kg.rootNote=constrain(atoi(val),0,127);
      else if(!strcmp(key,"TRANSPOSE"))kg.transposeSemitone=(int8_t)constrain(atoi(val),-24,24);
      else if(!strcmp(key,"FINE_CENT"))kg.fineCent=(int16_t)constrain(atoi(val),-100,100);
      else if(!strcmp(key,"KEYTRACK_PCT"))kg.keytrackPct=(uint8_t)constrain(atoi(val),0,100);
      else if(!strcmp(key,"CHOKE"))kg.chokeGroup=constrain(atoi(val),0,8);
      else if(!strcmp(key,"ONE_SHOT"))kg.oneShot=atoi(val)!=0;
      else if(!strcmp(key,"CHOKE_FADE_MS"))kg.chokeFadeMs=constrain(atoi(val),0,250);
      else if(!strcmp(key,"PLAY_MODE"))kg.playMode=constrain(atoi(val),0,2);
      else if(!strcmp(key,"EXCLUSIVE_GROUP"))kg.exclusiveGroup=constrain(atoi(val),0,8);
      else if(!strcmp(key,"RETRIGGER"))kg.retriggerLegato=atoi(val)!=0;
      else if(!strcmp(key,"START_PCT"))kg.startPct=constrain(atoi(val),0,99);
      else if(!strcmp(key,"END_PCT"))kg.endPct=constrain(atoi(val),1,100);
      else if(!strcmp(key,"REVERSE"))kg.reversePlayback=atoi(val)!=0;
      else if(!strcmp(key,"LEVEL")){kg.layer[0].level=constrain(atoi(val),0,100);updateVelocityLayerPan(kg.layer[0]);}
      else if(!strcmp(key,"PAN")){kg.layer[0].pan=constrain(atoi(val),-100,100);updateVelocityLayerPan(kg.layer[0]);}
      else if(!strcmp(key,"ENABLED"))kg.enabled=atoi(val)!=0;
    }else{
      VelocityLayer &vl=kg.layer[cl];
      if(!strcmp(key,"FILE")){char st[32];if(!importWavToKeygroupVariant(val,cs,cg,cl,0,st,sizeof(st)))importError=true;}
      else if(!strncmp(key,"RR",2)&&strstr(key,"_FILE")){int r=atoi(key+2)-1;if(r>=0&&r<4){char st[32];if(!importWavToKeygroupVariant(val,cs,cg,cl,(uint8_t)r,st,sizeof(st)))importError=true;}}
      else if(!strcmp(key,"RR_MODE"))vl.rrMode=constrain(atoi(val),0,5);
      else if(!strcmp(key,"LOOP_MODE"))vl.loopMode=constrain(atoi(val),0,2);
      else if(!strcmp(key,"LOOP_START_PCT"))vl.loopStartPct=constrain(atoi(val),0,98);
      else if(!strcmp(key,"LOOP_END_PCT"))vl.loopEndPct=constrain(atoi(val),1,100);
      else if(!strcmp(key,"LOOP_XFADE_MS"))vl.loopXfadeMs=constrain(atoi(val),0,50);
      else if(!strcmp(key,"VEL_TO_LEVEL"))vl.velocityToLevelPct=constrain(atoi(val),0,100);
      else if(!strcmp(key,"VEL_TO_FILTER"))vl.velocityToFilterPct=constrain(atoi(val),0,100);
      else if(!strcmp(key,"VEL_LOW"))vl.velocityLow=constrain(atoi(val),1,127);
      else if(!strcmp(key,"VEL_HIGH"))vl.velocityHigh=constrain(atoi(val),0,127);
      else if(!strcmp(key,"LEVEL"))vl.level=constrain(atoi(val),0,100);
      else if(!strcmp(key,"PAN"))vl.pan=constrain(atoi(val),-100,100);
      else if(!strcmp(key,"ENABLED"))vl.enabled=atoi(val)!=0;
      updateVelocityLayerPan(vl);kg.enabled=true;
    }
  }
  f.close();vTaskDelay(pdMS_TO_TICKS(2));
  PHX_INFO_PRINTF("V1.0.3 MS CFG %s lines=%u\n",importError?"IMPORT_ERROR":"OK",(unsigned)lineCount);
  return !importError;
}

bool PhoenixAudioManager::previewWav(const char *path, char *status, size_t statusLen) {
  if (status && statusLen) status[0] = 0;
  if (!_previewBuffer && !allocatePreviewBuffer()) { if (status) snprintf(status, statusLen, "NO PSRAM"); return false; }
  stopTransport();
  uint32_t frames = 0;
  bool ok = phxLoadWavToMono32kBuffer(path, _previewBuffer, _previewCapacityFrames, frames, status, statusLen);
  if (!ok || frames == 0) {
    _previewFrames = 0;
    _previewPos = 0;
    _state = AUDIO_THRU;
    return false;
  }
  _previewFrames = frames;
  _previewPos = 0;
  _playPos = 0;
  _playRangeActive = false;
  clearEchoBuffer();
  _state = AUDIO_PREVIEW;
  if (status) snprintf(status, statusLen, "PREVIEW PLAY");
  return true;
}

void PhoenixAudioManager::stopPreview() {
  if (_state == AUDIO_PREVIEW) {
    _state = AUDIO_THRU;
    _previewPos = 0;
  }
}


static void phxWriteLE16(File &f, uint16_t v) {
  f.write((uint8_t)(v & 0xFF));
  f.write((uint8_t)((v >> 8) & 0xFF));
}

static void phxWriteLE32(File &f, uint32_t v) {
  f.write((uint8_t)(v & 0xFF));
  f.write((uint8_t)((v >> 8) & 0xFF));
  f.write((uint8_t)((v >> 16) & 0xFF));
  f.write((uint8_t)((v >> 24) & 0xFF));
}

static uint32_t phxReadLE32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t phxReadLE16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool phxWriteWavHeader(File &f, uint32_t frames, uint32_t sampleRate, bool writeLoop) {
  const uint32_t dataBytes = frames * 2UL;
  const uint32_t smplChunkBytes = writeLoop ? 68UL : 0UL; // 8-byte chunk header + 60-byte payload
  f.write((const uint8_t*)"RIFF", 4);
  phxWriteLE32(f, 36UL + dataBytes + smplChunkBytes);
  f.write((const uint8_t*)"WAVE", 4);
  f.write((const uint8_t*)"fmt ", 4);
  phxWriteLE32(f, 16UL);
  phxWriteLE16(f, 1);       // PCM
  phxWriteLE16(f, 1);       // mono
  phxWriteLE32(f, sampleRate);
  phxWriteLE32(f, sampleRate * 2UL);
  phxWriteLE16(f, 2);       // block align
  phxWriteLE16(f, 16);      // bits
  f.write((const uint8_t*)"data", 4);
  phxWriteLE32(f, dataBytes);
  return true;
}

static bool phxWriteWavSmplChunk(File &f, const PhoenixAudioManager *a, uint8_t slot, uint32_t sampleRate) {
  const uint32_t start = a->loopStart(slot);
  const uint32_t endExclusive = a->loopEnd(slot);
  const uint8_t mode = a->loopMode(slot);
  if (mode == 0U || endExclusive <= start + 1U) return true;

  f.write((const uint8_t*)"smpl", 4);
  phxWriteLE32(f, 60UL);
  phxWriteLE32(f, 0UL); // manufacturer
  phxWriteLE32(f, 0UL); // product
  phxWriteLE32(f, sampleRate ? (1000000000UL / sampleRate) : 0UL);
  phxWriteLE32(f, a->slotRoot(slot));
  phxWriteLE32(f, 0UL); // MIDI pitch fraction
  phxWriteLE32(f, 0UL); // SMPTE format
  phxWriteLE32(f, 0UL); // SMPTE offset
  phxWriteLE32(f, 1UL); // one sustain loop
  phxWriteLE32(f, 0UL); // sampler data
  phxWriteLE32(f, 0UL); // cue point ID
  phxWriteLE32(f, mode == 2U ? 1UL : 0UL); // 0 forward, 1 alternating
  phxWriteLE32(f, start);
  phxWriteLE32(f, endExclusive - 1U); // WAV smpl end is inclusive
  phxWriteLE32(f, 0UL); // fraction
  phxWriteLE32(f, 0UL); // infinite play count
  return true;
}


static void phxWriteProjectMetadata(File &cfg, const PhoenixAudioManager *a) {
  cfg.println("PROJECT_PHOENIX_BANK=1");
  cfg.printf("BANK_VERSION=%u\n", (unsigned)PHX_BANK_FORMAT_VERSION);
  cfg.println("[GLOBAL]");
  cfg.printf("ECHO_DELAY_MS=%u\n", (unsigned)a->echoDelayMs());
  cfg.printf("ECHO_FEEDBACK=%u\n", (unsigned)a->echoFeedback());
  cfg.printf("ECHO_MIX=%u\n", (unsigned)a->echoMix());
  cfg.printf("TRIGGER_AUTO=%u\n", a->triggerAuto() ? 1 : 0);
  cfg.printf("TRIGGER_LEVEL=%u\n", (unsigned)a->triggerLevel());
  cfg.printf("REPLAY_REVERSE=%u\n", a->lastReplayReverse() ? 1 : 0);
  cfg.printf("QUATTRO_MODE=%u\n", (unsigned)a->quattroMode());
  for (uint8_t i = 0; i < PhoenixAudioManager::SLOT_COUNT; ++i) {
    cfg.printf("[SLOT%u]\n", (unsigned)(i + 1));
    cfg.printf("RECORDED=%u\n", a->slotRecorded(i) ? 1 : 0);
    cfg.printf("SAMPLE_RATE=%lu\n", (unsigned long)a->sampleRate());
    cfg.printf("FRAME_COUNT=%lu\n", (unsigned long)a->slotFrames(i));
    cfg.printf("SAMPLE_START=%lu\n", (unsigned long)a->sampleStart(i));
    cfg.printf("SAMPLE_END=%lu\n", (unsigned long)a->sampleEnd(i));
    cfg.printf("TRIM_ENABLED=%u\n", a->trimEnabled(i) ? 1 : 0);
    uint32_t trimUndoSampleStart = 0, trimUndoLoopStart = 0, trimUndoLoopEnd = 0, trimUndoSampleEnd = 0;
    const bool trimUndoValid = a->getTrimUndoMarkers(i, trimUndoSampleStart, trimUndoLoopStart, trimUndoLoopEnd, trimUndoSampleEnd);
    cfg.printf("TRIM_UNDO_VALID=%u\n", trimUndoValid ? 1 : 0);
    cfg.printf("TRIM_UNDO_SAMPLE_START=%lu\n", (unsigned long)trimUndoSampleStart);
    cfg.printf("TRIM_UNDO_LOOP_START=%lu\n", (unsigned long)trimUndoLoopStart);
    cfg.printf("TRIM_UNDO_LOOP_END=%lu\n", (unsigned long)trimUndoLoopEnd);
    cfg.printf("TRIM_UNDO_SAMPLE_END=%lu\n", (unsigned long)trimUndoSampleEnd);
    cfg.printf("DC_ENABLED=%u\n", a->dcCorrectionEnabled(i) ? 1 : 0);
    cfg.printf("DC_OFFSET=%d\n", (int)a->slotDcOffset(i));
    cfg.printf("NORMALIZE_ENABLED=%u\n", a->normalizeEnabled(i) ? 1 : 0);
    cfg.printf("NORMALIZE_GAIN_Q16=%lu\n", (unsigned long)a->slotNormalizeGainQ16(i));
    cfg.printf("LOOP=%u\n", a->loopEnabled(i) ? 1 : 0);
    cfg.printf("LOOP_MODE=%u\n", (unsigned)a->loopMode(i));
    cfg.printf("LOOP_XFADE_MS=%u\n", (unsigned)a->loopCrossfadeMs(i));
    cfg.printf("LOOP_START=%lu\n", (unsigned long)a->loopStart(i));
    cfg.printf("LOOP_END=%lu\n", (unsigned long)a->loopEnd(i));
    cfg.printf("COARSE=%d\n", (int)a->slotCoarse(i));
    cfg.printf("FINE=%d\n", (int)a->slotFine(i));
    cfg.printf("ROOT=%u\n", (unsigned)a->slotRoot(i));
    cfg.printf("PITCH_BEND_RANGE=%u\n", (unsigned)a->pitchBendRange(i));
    cfg.printf("KEY_LOW=%u\n", (unsigned)a->quattroKeyLow(i));
    cfg.printf("KEY_HIGH=%u\n", (unsigned)a->quattroKeyHigh(i));
    cfg.printf("MIDI_CHANNEL=%u\n", (unsigned)a->quattroMidiChannel(i));
    cfg.printf("OCTAVE=%d\n", (int)a->slotOctave(i));
    cfg.printf("PITCH_TRACK=%u\n", a->slotPitchTracking(i) ? 1 : 0);
    cfg.printf("ATTACK_MS=%u\n", (unsigned)a->slotAttackMs(i));
    cfg.printf("DECAY_MS=%u\n", (unsigned)a->slotDecayMs(i));
    cfg.printf("SUSTAIN_PCT=%u\n", (unsigned)a->slotSustainPct(i));
    cfg.printf("RELEASE_MS=%u\n", (unsigned)a->slotReleaseMs(i));
    cfg.printf("VINTAGE_PRESET=%u\n", (unsigned)a->slotVintagePreset(i));
    cfg.printf("VINTAGE_RATE=%u\n", (unsigned)a->slotVintageSampleRate(i));
    cfg.printf("VINTAGE_BITS=%u\n", (unsigned)a->slotVintageBitDepth(i));
    cfg.printf("VINTAGE_FILTER=%u\n", (unsigned)a->slotVintageFilter(i));
    cfg.printf("VINTAGE_JITTER=%u\n", (unsigned)a->slotVintageJitter(i));
    cfg.printf("ECHO_SEND=%u\n", (unsigned)a->echoSend(i));
    cfg.printf("SAMPLE_GAIN=%u\n", (unsigned)a->sampleGain(i));
    cfg.printf("VELOCITY_AMOUNT=%u\n", (unsigned)a->velocityAmount(i));
    cfg.printf("LEVEL=%u\n", (unsigned)a->slotLevel(i));
    cfg.printf("PAN=%d\n", (int)a->slotPan(i));
    cfg.printf("FILTER_CUTOFF=%u\n", (unsigned)a->slotFilterCutoff(i));
    cfg.printf("FILTER_RESONANCE=%u\n", (unsigned)a->slotFilterResonance(i));
    cfg.printf("FILTER_ENV_AMOUNT=%d\n", (int)a->slotFilterEnvAmount(i));
    cfg.printf("FILTER_VELOCITY=%u\n", (unsigned)a->slotFilterVelocityAmount(i));
    cfg.printf("FILTER_KEYTRACK=%u\n", (unsigned)a->slotFilterKeytrack(i));
    cfg.printf("FILTER_ATTACK_MS=%u\n", (unsigned)a->slotFilterAttackMs(i));
    cfg.printf("FILTER_DECAY_MS=%u\n", (unsigned)a->slotFilterDecayMs(i));
    cfg.printf("FILTER_SUSTAIN=%u\n", (unsigned)a->slotFilterSustainPct(i));
    cfg.printf("FILTER_RELEASE_MS=%u\n", (unsigned)a->slotFilterReleaseMs(i));
    cfg.printf("VOICE_MODE=%u\n", (unsigned)a->slotVoiceMode(i));
    cfg.printf("VOICE_LIMIT=%u\n", (unsigned)a->slotVoiceLimit(i));
    cfg.printf("NOTE_PRIORITY=%u\n", (unsigned)a->slotNotePriority(i));
    cfg.printf("GLIDE_MS=%u\n", (unsigned)a->slotGlideMs(i));
    cfg.printf("REPLAY_REVERSE=%u\n", a->lastReplayReverse() ? 1 : 0);
  }
}

static bool phxParseKvLine(char *line, char *&key, char *&value) {
  key = line;
  while (*key == ' ' || *key == '\t') key++;
  if (*key == 0 || *key == '#' || *key == ';' || *key == '[') return false;
  char *eq = strchr(key, '=');
  if (!eq) return false;
  *eq = 0;
  value = eq + 1;
  char *end = key + strlen(key);
  while (end > key && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
  while (*value == ' ' || *value == '\t') value++;
  end = value + strlen(value);
  while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
  return true;
}

static long phxToLong(const char *v, long def = 0) {
  if (!v || !*v) return def;
  return strtol(v, nullptr, 10);
}

bool PhoenixAudioManager::loadProjectMetadataFromSD(const char *dir) {
  if (!dir || !dir[0]) return false;
  char path[48];
  snprintf(path, sizeof(path), "%s/BANK.CFG", dir);
  File cfg = SD.open(path, FILE_READ);
  if (!cfg) return false;

  // v0.7.18c: older banks do not contain ECHO_SEND. Reset all sends first
  // so missing entries always resolve to the documented 0% default.
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) { _slotEchoSend[i] = 0; _slotSampleGainPct[i]=100; _slotVelocityAmount[i]=100; _slots[i].trimEnabled=false; _slots[i].trimUndoValid=false; _slots[i].trimUndoSampleStart=0; _slots[i].trimUndoLoopStart=0; _slots[i].trimUndoLoopEnd=0; _slots[i].trimUndoSampleEnd=0; _slots[i].dcCorrectionEnabled=false; _slots[i].normalizeEnabled=false; _slots[i].dcOffset=0; _slots[i].normalizeGainQ16=65536UL; setMixerParams(i,100,0); setFilterParams(i,100,0,0,0,0); setFilterEnvelopeParams(i,0,250,0,300); }

  int currentSlot = -1;
  int bankVersion = 0;
  bool voiceLimitSeen[SLOT_COUNT] = { false, false, false, false };
  bool loopModeSeen[SLOT_COUNT] = { false, false, false, false };
  char line[96];
  bool any = false;
  while (cfg.available()) {
    size_t n = cfg.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    char *trim = line;
    while (*trim == ' ' || *trim == '\t') trim++;
    if (strncmp(trim, "[GLOBAL]", 8) == 0) { currentSlot = -1; continue; }
    if (strncmp(trim, "[SLOT", 5) == 0) {
      int slot = atoi(trim + 5) - 1;
      currentSlot = (slot >= 0 && slot < (int)SLOT_COUNT) ? slot : -2;
      continue;
    }
    char *key = nullptr; char *val = nullptr;
    if (!phxParseKvLine(trim, key, val)) continue;

    if (currentSlot < 0) {
      if (strcmp(key, "BANK_VERSION") == 0) bankVersion = (int)phxToLong(val, 0);
      else if (strcmp(key, "ECHO_DELAY_MS") == 0) _echoDelayMs = (uint16_t)constrain((int)phxToLong(val, _echoDelayMs), 50, 1000);
      else if (strcmp(key, "ECHO_FEEDBACK") == 0) _echoFeedback = (uint8_t)constrain((int)phxToLong(val, _echoFeedback), 0, 90);
      else if (strcmp(key, "ECHO_MIX") == 0) _echoMix = (uint8_t)constrain((int)phxToLong(val, _echoMix), 0, 100);
      else if (strcmp(key, "TRIGGER_AUTO") == 0) _triggerAuto = (phxToLong(val, _triggerAuto ? 1 : 0) != 0);
      else if (strcmp(key, "TRIGGER_LEVEL") == 0) _triggerLevel = (uint8_t)constrain((int)phxToLong(val, _triggerLevel), 1, 8);
      else if (strcmp(key, "QUATTRO_MODE") == 0) _quattroMode = (uint8_t)constrain((int)phxToLong(val, _quattroMode), 0, 1);
      else if (strcmp(key, "REPLAY_REVERSE") == 0) _lastReplayReverse = (phxToLong(val, _lastReplayReverse ? 1 : 0) != 0);
      any = true;
      continue;
    }
    if (currentSlot >= (int)SLOT_COUNT) continue;
    SampleSlot &sl = _slots[currentSlot];
    if (strcmp(key, "SAMPLE_RATE") == 0) { /* internal Phoenix playback remains 32 kHz */ }
    else if (strcmp(key, "FRAME_COUNT") == 0) { /* informational; WAV data is authoritative */ }
    else if (strcmp(key, "LOOP") == 0) sl.loopEnabled = (phxToLong(val, sl.loopEnabled ? 1 : 0) != 0);
    else if (strcmp(key, "LOOP_MODE") == 0) { sl.loopMode = (uint8_t)constrain((int)phxToLong(val, sl.loopMode), 0, 2); loopModeSeen[currentSlot] = true; }
    else if (strcmp(key, "LOOP_XFADE_MS") == 0) sl.loopXfadeMs = (uint8_t)constrain((int)phxToLong(val, sl.loopXfadeMs), 0, 32);
    else if (strcmp(key, "SAMPLE_START") == 0) sl.sampleStart = (uint32_t)max(0L, phxToLong(val, sl.sampleStart));
    else if (strcmp(key, "SAMPLE_END") == 0) sl.sampleEnd = (uint32_t)max(0L, phxToLong(val, sl.sampleEnd));
    else if (strcmp(key, "TRIM_ENABLED") == 0) sl.trimEnabled = (phxToLong(val, sl.trimEnabled ? 1 : 0) != 0);
    else if (strcmp(key, "TRIM_UNDO_VALID") == 0) sl.trimUndoValid = (phxToLong(val, sl.trimUndoValid ? 1 : 0) != 0);
    else if (strcmp(key, "TRIM_UNDO_SAMPLE_START") == 0) sl.trimUndoSampleStart = (uint32_t)max(0L, phxToLong(val, sl.trimUndoSampleStart));
    else if (strcmp(key, "TRIM_UNDO_LOOP_START") == 0) sl.trimUndoLoopStart = (uint32_t)max(0L, phxToLong(val, sl.trimUndoLoopStart));
    else if (strcmp(key, "TRIM_UNDO_LOOP_END") == 0) sl.trimUndoLoopEnd = (uint32_t)max(0L, phxToLong(val, sl.trimUndoLoopEnd));
    else if (strcmp(key, "TRIM_UNDO_SAMPLE_END") == 0) sl.trimUndoSampleEnd = (uint32_t)max(0L, phxToLong(val, sl.trimUndoSampleEnd));
    else if (strcmp(key, "DC_ENABLED") == 0) sl.dcCorrectionEnabled = (phxToLong(val, sl.dcCorrectionEnabled ? 1 : 0) != 0);
    else if (strcmp(key, "DC_OFFSET") == 0) sl.dcOffset = (int16_t)constrain((int)phxToLong(val, sl.dcOffset), -32768, 32767);
    else if (strcmp(key, "NORMALIZE_ENABLED") == 0) sl.normalizeEnabled = (phxToLong(val, sl.normalizeEnabled ? 1 : 0) != 0);
    else if (strcmp(key, "NORMALIZE_GAIN_Q16") == 0) sl.normalizeGainQ16 = (uint32_t)max(1L, phxToLong(val, sl.normalizeGainQ16));
    else if (strcmp(key, "LOOP_START") == 0) sl.loopStart = (uint32_t)max(0L, phxToLong(val, sl.loopStart));
    else if (strcmp(key, "LOOP_END") == 0) sl.loopEnd = (uint32_t)max(0L, phxToLong(val, sl.loopEnd));
    else if (strcmp(key, "COARSE") == 0) _slotCoarse[currentSlot] = (int8_t)constrain((int)phxToLong(val, _slotCoarse[currentSlot]), -24, 24);
    else if (strcmp(key, "FINE") == 0) _slotFine[currentSlot] = (int16_t)constrain((int)phxToLong(val, _slotFine[currentSlot]), -100, 100);
    else if (strcmp(key, "KEY_LOW") == 0) _quattroKeyLow[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _quattroKeyLow[currentSlot]), 0, 127);
    else if (strcmp(key, "KEY_HIGH") == 0) _quattroKeyHigh[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _quattroKeyHigh[currentSlot]), 0, 127);
    else if (strcmp(key, "PITCH_BEND_RANGE") == 0) _slotPitchBendRange[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotPitchBendRange[currentSlot]), 1, 24);
    else if (strcmp(key, "MIDI_CHANNEL") == 0) _quattroMidiChannel[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _quattroMidiChannel[currentSlot]), 1, 16);
    else if (strcmp(key, "ROOT") == 0) _slotRoot[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotRoot[currentSlot]), 0, 127);
    else if (strcmp(key, "OCTAVE") == 0) _slotOctave[currentSlot] = (int8_t)constrain((int)phxToLong(val, _slotOctave[currentSlot]), -2, 2);
    else if (strcmp(key, "PITCH_TRACK") == 0) _slotPitchTrack[currentSlot] = (phxToLong(val, _slotPitchTrack[currentSlot] ? 1 : 0) != 0);
    else if (strcmp(key, "ATTACK_MS") == 0) _slotAttackMs[currentSlot] = (uint16_t)constrain((int)phxToLong(val, _slotAttackMs[currentSlot]), 0, 2000);
    else if (strcmp(key, "DECAY_MS") == 0) _slotDecayMs[currentSlot] = (uint16_t)constrain((int)phxToLong(val, _slotDecayMs[currentSlot]), 0, 5000);
    else if (strcmp(key, "SUSTAIN_PCT") == 0) _slotSustainPct[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotSustainPct[currentSlot]), 0, 100);
    else if (strcmp(key, "RELEASE_MS") == 0) _slotReleaseMs[currentSlot] = (uint16_t)constrain((int)phxToLong(val, _slotReleaseMs[currentSlot]), 0, 5000);
    else if (strcmp(key, "VINTAGE_PRESET") == 0) _slotVintagePreset[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVintagePreset[currentSlot]), 0, 7);
    else if (strcmp(key, "VINTAGE_RATE") == 0) _slotVintageSampleRate[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVintageSampleRate[currentSlot]), 0, 8);
    else if (strcmp(key, "VINTAGE_BITS") == 0) _slotVintageBitDepth[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVintageBitDepth[currentSlot]), 0, 6);
    else if (strcmp(key, "VINTAGE_FILTER") == 0) _slotVintageFilter[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVintageFilter[currentSlot]), 0, 3);
    else if (strcmp(key, "VINTAGE_JITTER") == 0) _slotVintageJitter[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVintageJitter[currentSlot]), 0, 100);
    else if (strcmp(key, "SAMPLE_GAIN") == 0) _slotSampleGainPct[currentSlot] = (uint8_t)constrain((int)phxToLong(val,100),0,200);
    else if (strcmp(key, "VELOCITY_AMOUNT") == 0) _slotVelocityAmount[currentSlot] = (uint8_t)constrain((int)phxToLong(val,100),0,100);
    else if (strcmp(key, "ECHO_SEND") == 0) _slotEchoSend[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotEchoSend[currentSlot]), 0, 100);
    else if (strcmp(key, "LEVEL") == 0) setMixerParams(currentSlot, (uint8_t)constrain((int)phxToLong(val, _slotLevel[currentSlot]),0,100), _slotPan[currentSlot]);
    else if (strcmp(key, "PAN") == 0) setMixerParams(currentSlot, _slotLevel[currentSlot], (int8_t)constrain((int)phxToLong(val, _slotPan[currentSlot]),-100,100));
    else if (strcmp(key,"FILTER_CUTOFF")==0) _slotFilterCutoff[currentSlot]=(uint8_t)constrain((int)phxToLong(val,100),0,100);
    else if (strcmp(key,"FILTER_RESONANCE")==0) _slotFilterResonance[currentSlot]=(uint8_t)constrain((int)phxToLong(val,0),0,100);
    else if (strcmp(key,"FILTER_ENV_AMOUNT")==0) _slotFilterEnvAmount[currentSlot]=(int8_t)constrain((int)phxToLong(val,0),-100,100);
    else if (strcmp(key,"FILTER_VELOCITY")==0) _slotFilterVelocityAmount[currentSlot]=(uint8_t)constrain((int)phxToLong(val,0),0,100);
    else if (strcmp(key,"FILTER_KEYTRACK")==0) _slotFilterKeytrack[currentSlot]=(uint8_t)constrain((int)phxToLong(val,0),0,100);
    else if (strcmp(key,"FILTER_ATTACK_MS")==0) _slotFilterAttackMs[currentSlot]=(uint16_t)constrain((int)phxToLong(val,0),0,2000);
    else if (strcmp(key,"FILTER_DECAY_MS")==0) _slotFilterDecayMs[currentSlot]=(uint16_t)constrain((int)phxToLong(val,250),0,5000);
    else if (strcmp(key,"FILTER_SUSTAIN")==0) _slotFilterSustainPct[currentSlot]=(uint8_t)constrain((int)phxToLong(val,0),0,100);
    else if (strcmp(key,"FILTER_RELEASE_MS")==0) _slotFilterReleaseMs[currentSlot]=(uint16_t)constrain((int)phxToLong(val,300),0,5000);
    else if (strcmp(key, "VOICE_MODE") == 0) _slotVoiceMode[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVoiceMode[currentSlot]), 0, 2);
    else if (strcmp(key, "VOICE_LIMIT") == 0) {
      _slotVoiceLimit[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotVoiceLimit[currentSlot]), 1, (int)VOICE_COUNT);
      voiceLimitSeen[currentSlot] = true;
    }
    else if (strcmp(key, "NOTE_PRIORITY") == 0) _slotNotePriority[currentSlot] = (uint8_t)constrain((int)phxToLong(val, _slotNotePriority[currentSlot]), 0, 2);
    else if (strcmp(key, "GLIDE_MS") == 0) _slotGlideMs[currentSlot] = (uint16_t)constrain((int)phxToLong(val, _slotGlideMs[currentSlot]), 0, 2000);
    else if (strcmp(key, "REPLAY_REVERSE") == 0) _lastReplayReverse = (phxToLong(val, _lastReplayReverse ? 1 : 0) != 0);
    any = true;
  }
  cfg.close();

  // C027a: Phoenix 1.0 uses a shared 12-voice maximum. BANK_VERSION 10 was
  // written by C006, whose boot path accidentally forced every slot to four
  // voices before a bank was saved. A value of four from that one version is
  // therefore treated as the regression fingerprint and migrated to 12.
  // Any stored value above 12 is already clamped to VOICE_COUNT while parsing.
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (_slotVoiceMode[i] != 0) {
      _slotVoiceLimit[i] = 1;
    } else if (!voiceLimitSeen[i] ||
               (bankVersion < 9 && (_slotVoiceLimit[i] == 4 || _slotVoiceLimit[i] == 16)) ||
               (bankVersion == 10 && _slotVoiceLimit[i] == 4)) {
      _slotVoiceLimit[i] = PHX_DEFAULT_POLY_VOICE_LIMIT;
    }
  }

  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    SampleSlot &sl = _slots[i];
    // C019 migration: C018 stored TRIM_ENABLED but no undo marker snapshot.
    // Restore the full sample on the first undo while preserving valid loops.
    if (sl.trimEnabled && (bankVersion < 13 || !sl.trimUndoValid)) {
      sl.trimUndoValid = true;
      sl.trimUndoSampleStart = 0U;
      sl.trimUndoSampleEnd = sl.frames;
      sl.trimUndoLoopStart = sl.loopStart;
      sl.trimUndoLoopEnd = sl.loopEnd;
    }
    if (sl.trimUndoValid) {
      if (sl.trimUndoSampleEnd == 0U || sl.trimUndoSampleEnd > sl.frames) sl.trimUndoSampleEnd = sl.frames;
      if (sl.trimUndoSampleStart >= sl.trimUndoSampleEnd) sl.trimUndoSampleStart = 0U;
      if (sl.trimUndoLoopStart < sl.trimUndoSampleStart) sl.trimUndoLoopStart = sl.trimUndoSampleStart;
      if (sl.trimUndoLoopEnd == 0U || sl.trimUndoLoopEnd > sl.trimUndoSampleEnd) sl.trimUndoLoopEnd = sl.trimUndoSampleEnd;
      if (sl.trimUndoLoopEnd <= sl.trimUndoLoopStart + 1U) {
        sl.trimUndoLoopStart = sl.trimUndoSampleStart;
        sl.trimUndoLoopEnd = sl.trimUndoSampleEnd;
      }
    }
    // Banks up to version 9 only stored LOOP=0/1. Preserve them as classic
    // forward loops; version 10 and newer store OFF/FORWARD/ALTERNATE explicitly.
    if (!loopModeSeen[i]) sl.loopMode = sl.loopEnabled ? LOOP_FORWARD : LOOP_OFF;
    normalizeSlotMarkers(sl);
    calculateSlotProcessing(sl);
  }
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    _slotPitchBend[i] = 0;
    setPitchBendRange(i, _slotPitchBendRange[i]);
  }
  updatePlaybackIncrement(_selectedSlot);
  return any;
}


// C030 Transactional Bank Storage -------------------------------------------------
// A bank transaction covers the four base-slot WAV files plus MULTISAMPLE.CFG and
// BANK.CFG.  New files are fully staged first.  A small journal records which old
// targets existed.  Existing targets are then renamed to backups, staged files are
// installed, verified, and only then is the journal removed.  If power is lost while
// the journal exists, the next save/load rolls back to the complete previous bank.
static constexpr uint8_t PHX_C030_TARGET_COUNT = 6;
static const char * const PHX_C030_TARGET_NAMES[PHX_C030_TARGET_COUNT] = {
  "SLOT1.WAV", "SLOT2.WAV", "SLOT3.WAV", "SLOT4.WAV", "MULTISAMPLE.CFG", "BANK.CFG"
};

static void phxC030Path(char *dst, size_t cap, const char *dir, uint8_t idx, const char *suffix) {
  snprintf(dst, cap, "%s/%s%s", dir, PHX_C030_TARGET_NAMES[idx], suffix ? suffix : "");
}

static void phxC030CleanupArtifacts(const char *dir) {
  char p[176];
  for (uint8_t i = 0; i < PHX_C030_TARGET_COUNT; ++i) {
    phxC030Path(p, sizeof(p), dir, i, ".C30TMP"); SD.remove(p);
    phxC030Path(p, sizeof(p), dir, i, ".C30BAK"); SD.remove(p);
  }
  snprintf(p, sizeof(p), "%s/BANK.C30NEW", dir); SD.remove(p);
}

static bool phxC030ReadJournal(const char *dir, uint8_t &oldMask) {
  char path[176]; snprintf(path, sizeof(path), "%s/BANK.C30TXN", dir);
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  bool signature = false, maskSeen = false;
  char line[64];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1); line[n] = 0; phxTrimLine(line);
    if (!strcmp(line, "PHX_C030_TXN=1")) signature = true;
    else if (!strncmp(line, "OLD_MASK=", 9)) { oldMask = (uint8_t)(strtoul(line + 9, nullptr, 0) & 0x3FU); maskSeen = true; }
  }
  f.close();
  return signature && maskSeen;
}

static bool phxC030RecoverBankTransaction(const char *dir) {
  char journal[176]; snprintf(journal, sizeof(journal), "%s/BANK.C30TXN", dir);
  if (!SD.exists(journal)) {
    // No live transaction: any backup/temp files are leftovers from a commit that
    // had already crossed the commit point (journal removal).
    phxC030CleanupArtifacts(dir);
    return true;
  }

  uint8_t oldMask = 0;
  if (!phxC030ReadJournal(dir, oldMask)) {
    Serial.println(F("C030 STORAGE: invalid transaction journal; bank left untouched"));
    return false;
  }

  bool ok = true;
  char target[176], backup[176], temp[176];
  for (uint8_t i = 0; i < PHX_C030_TARGET_COUNT; ++i) {
    phxC030Path(target, sizeof(target), dir, i, "");
    phxC030Path(backup, sizeof(backup), dir, i, ".C30BAK");
    phxC030Path(temp, sizeof(temp), dir, i, ".C30TMP");
    const bool existedBefore = (oldMask & (1U << i)) != 0;
    if (existedBefore) {
      // If a backup exists, this target had already entered the commit phase.
      if (SD.exists(backup)) {
        if (SD.exists(target)) SD.remove(target);
        if (!SD.rename(backup, target)) ok = false;
      }
      // If no backup exists the old target was never moved, so leave it untouched.
    } else {
      // This file did not exist before the transaction; remove a partially installed new copy.
      if (SD.exists(target) && !SD.remove(target)) ok = false;
      if (SD.exists(backup)) SD.remove(backup);
    }
    SD.remove(temp);
  }
  if (ok) SD.remove(journal);
  if (ok) Serial.println(F("C030 STORAGE: interrupted bank transaction rolled back"));
  return ok;
}

static bool phxC030WriteJournal(const char *dir, uint8_t oldMask) {
  char temp[176], journal[176];
  snprintf(temp, sizeof(temp), "%s/BANK.C30NEW", dir);
  snprintf(journal, sizeof(journal), "%s/BANK.C30TXN", dir);
  SD.remove(temp); SD.remove(journal);
  File f = SD.open(temp, FILE_WRITE);
  if (!f) return false;
  bool ok = f.printf("PHX_C030_TXN=1\nOLD_MASK=%u\n", (unsigned)oldMask) > 0;
  f.flush();
  ok = ok && (f.size() >= 20U);
  f.close();
  if (!ok) { SD.remove(temp); return false; }
  if (!SD.rename(temp, journal)) { SD.remove(temp); return false; }
  return SD.exists(journal);
}

static bool phxC030VerifySize(const char *path, uint32_t expected) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  const uint32_t got = (uint32_t)f.size();
  f.close();
  return got == expected && got > 0U;
}

bool PhoenixAudioManager::saveBankToSD(const char *dir) {
  if (!dir || !dir[0]) return false;
  SD.mkdir(dir);
  if (!phxC030RecoverBankTransaction(dir)) return false;

  bool desired[PHX_C030_TARGET_COUNT] = {false, false, false, false, false, false};
  uint32_t stagedSize[PHX_C030_TARGET_COUNT] = {0, 0, 0, 0, 0, 0};
  char target[176], temp[176], backup[176];

  // Phase 1: stage every base-slot WAV without touching the currently valid bank.
  bool wroteAny = false;
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    const SampleSlot &slot = _slots[i];
    if (!slot.buffer || !slot.recorded || slot.frames == 0) continue;
    desired[i] = true;
    wroteAny = true;
    phxC030Path(temp, sizeof(temp), dir, i, ".C30TMP");
    SD.remove(temp);
    File f = SD.open(temp, FILE_WRITE);
    if (!f) { phxC030CleanupArtifacts(dir); return false; }

    const bool writeWavLoop = (slot.loopMode != LOOP_OFF && slot.loopEnd > slot.loopStart + 1U);
    if (!phxWriteWavHeader(f, slot.frames, sampleRate(), writeWavLoop)) {
      f.close(); phxC030CleanupArtifacts(dir); return false;
    }
    const uint8_t *raw = (const uint8_t*)slot.buffer;
    const uint32_t bytes = slot.frames * 2UL;
    uint32_t done = 0;
    _progressBase = (uint8_t)(i * 15U); _progressSpan = 15;
    while (done < bytes) {
      uint32_t chunk = bytes - done; if (chunk > 2048UL) chunk = 2048UL;
      if (f.write(raw + done, chunk) != chunk) {
        f.close(); phxC030CleanupArtifacts(dir); return false;
      }
      done += chunk;
      reportProgress(bytes ? (uint8_t)(((uint64_t)done * 100ULL) / bytes) : 100);
      // C036b: delay(0) only yields to ready tasks of equal/higher priority and can
      // starve IDLE0 while PhoenixControl performs a long SD transaction. Give the
      // scheduler a real tick every 16 KiB so the task watchdog stays serviced.
      if ((done & 0x3FFFUL) == 0UL || done >= bytes) delay(1);
      else delay(0);
    }
    if (writeWavLoop && !phxWriteWavSmplChunk(f, this, i, sampleRate())) {
      f.close(); phxC030CleanupArtifacts(dir); return false;
    }
    f.flush();
    delay(1); // C036b: let IDLE0 run after SD flush
    stagedSize[i] = (uint32_t)f.size();
    f.close();
    const uint32_t expected = 44UL + bytes + (writeWavLoop ? 68UL : 0UL);
    if (stagedSize[i] != expected || !phxC030VerifySize(temp, expected)) {
      phxC030CleanupArtifacts(dir); return false;
    }
  }

  const bool hasKeygroups = keygroupCount(0) || keygroupCount(1) || keygroupCount(2) || keygroupCount(3);
  const bool hasBankData = wroteAny || hasKeygroups;
  if (!hasBankData) {
    // Empty RAM must never erase an existing bank accidentally.
    phxC030CleanupArtifacts(dir);
    return false;
  }

  // Stage multisample metadata, if the new bank needs it.
  if (hasKeygroups) {
    desired[4] = true;
    phxC030Path(temp, sizeof(temp), dir, 4, ".C30TMP");
    bool any = false;
    if (!writeMultisampleConfigFile(temp, &any) || !any) { phxC030CleanupArtifacts(dir); return false; }
    File f = SD.open(temp, FILE_READ); if (!f) { phxC030CleanupArtifacts(dir); return false; }
    stagedSize[4] = (uint32_t)f.size(); f.close();
    if (stagedSize[4] == 0U) { phxC030CleanupArtifacts(dir); return false; }
  }

  // BANK.CFG is the final metadata member of every valid bank.
  desired[5] = true;
  phxC030Path(temp, sizeof(temp), dir, 5, ".C30TMP");
  SD.remove(temp);
  {
    File cfg = SD.open(temp, FILE_WRITE);
    if (!cfg) { phxC030CleanupArtifacts(dir); return false; }
    phxWriteProjectMetadata(cfg, this);
    cfg.flush();
    delay(1); // C036b: cooperative metadata flush
    stagedSize[5] = (uint32_t)cfg.size();
    cfg.close();
  }
  if (stagedSize[5] < 32U || !phxC030VerifySize(temp, stagedSize[5])) {
    phxC030CleanupArtifacts(dir); return false;
  }

  // Snapshot the old generation before moving any live file.
  uint8_t oldMask = 0;
  for (uint8_t i = 0; i < PHX_C030_TARGET_COUNT; ++i) {
    phxC030Path(target, sizeof(target), dir, i, "");
    if (SD.exists(target)) oldMask |= (uint8_t)(1U << i);
  }
  if (!phxC030WriteJournal(dir, oldMask)) { phxC030CleanupArtifacts(dir); return false; }

  // Phase 2: move the complete old generation to backups.
  for (uint8_t i = 0; i < PHX_C030_TARGET_COUNT; ++i) {
    if (!(oldMask & (1U << i))) continue;
    phxC030Path(target, sizeof(target), dir, i, "");
    phxC030Path(backup, sizeof(backup), dir, i, ".C30BAK");
    SD.remove(backup);
    if (!SD.rename(target, backup)) {
      phxC030RecoverBankTransaction(dir);
      return false;
    }
  }

  // Phase 3: install the complete new generation. Files not desired remain absent.
  for (uint8_t i = 0; i < PHX_C030_TARGET_COUNT; ++i) {
    if (!desired[i]) continue;
    phxC030Path(target, sizeof(target), dir, i, "");
    phxC030Path(temp, sizeof(temp), dir, i, ".C30TMP");
    if (!SD.rename(temp, target) || !phxC030VerifySize(target, stagedSize[i])) {
      phxC030RecoverBankTransaction(dir);
      return false;
    }
  }

  // Commit point.  Once the journal is gone the new bank is authoritative.
  char journal[176]; snprintf(journal, sizeof(journal), "%s/BANK.C30TXN", dir);
  if (!SD.remove(journal)) {
    phxC030RecoverBankTransaction(dir);
    return false;
  }

  // Backups are now obsolete. A power loss here is harmless: next recovery sees
  // no journal and removes these stale backup artifacts while keeping new targets.
  phxC030CleanupArtifacts(dir);
  PHX_INFO_PRINTF("C030 STORAGE COMMIT dir=%s oldmask=0x%02X newmask=0x%02X\n", dir,
                (unsigned)oldMask,
                (unsigned)((desired[0]?1:0)|(desired[1]?2:0)|(desired[2]?4:0)|(desired[3]?8:0)|(desired[4]?16:0)|32));
  return true;
}

bool PhoenixAudioManager::loadBankFromSD(const char *dir) {
  if (!dir || !dir[0]) return false;
  if (!phxC030RecoverBankTransaction(dir)) return false;
  stopTransport();
  bool loadedAny = false;
  const uint8_t oldBase = _progressBase, oldSpan = _progressSpan;
  reportProgress(0);

  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    char path[48];
    snprintf(path, sizeof(path), "%s/SLOT%u.WAV", dir, (unsigned)(i + 1));
    File f = SD.open(path, FILE_READ);
    SampleSlot &s = _slots[i];
    s.frames = 0;
    s.recorded = false;
    s.loopEnabled = false;
    s.sampleStart = 0;
    s.sampleEnd = 0;
    s.loopStart = 0;
    s.loopEnd = 0;
    s.loopMode = LOOP_OFF;
    s.trimEnabled = false;
    s.trimUndoValid = false;
    s.trimUndoSampleStart = 0; s.trimUndoLoopStart = 0; s.trimUndoLoopEnd = 0; s.trimUndoSampleEnd = 0;
    s.dcCorrectionEnabled = false; s.normalizeEnabled = false;
    s.dcOffset = 0; s.normalizeGainQ16 = 65536UL;
    if (!f) continue;

    uint16_t channels = 0, bits = 0;
    uint32_t sr = 0, dataOffset = 0, dataBytes = 0;
    PhxWavLoopMetadata wavLoop;
    if (!phxReadWavHeaderGeneric(f, channels, sr, bits, dataOffset, dataBytes, &wavLoop)) { f.close(); continue; }
    if (channels != 1 || bits != 16 || sr != sampleRate()) { f.close(); continue; }
    uint32_t frames = dataBytes / 2UL;
    if (frames > s.capacityFrames) frames = s.capacityFrames;
    if (!s.buffer || frames == 0) { f.close(); continue; }

    if (!f.seek(dataOffset)) { f.close(); continue; }
    uint8_t *raw = (uint8_t*)s.buffer;
    uint32_t bytes = frames * 2UL;
    uint32_t done = 0;
    _progressBase = (uint8_t)(i * 15U); _progressSpan = 15;
    while (done < bytes) {
      uint32_t chunk = bytes - done;
      if (chunk > 2048UL) chunk = 2048UL;
      int r = f.read(raw + done, chunk);
      if (r <= 0) break;
      done += (uint32_t)r;
      reportProgress(bytes ? (uint8_t)(((uint64_t)done * 100ULL) / bytes) : 100);
      // C036b: cooperative bank-load yield, symmetric with the save path.
      if ((done & 0x3FFFUL) == 0UL || done >= bytes) delay(1);
      else delay(0);
    }
    f.close();
    s.frames = done / 2UL;
    s.recorded = (s.frames > 0);
    s.sampleStart = 0;
    s.sampleEnd = s.frames;
    s.loopStart = 0;
    s.loopEnd = s.frames;
    if (wavLoop.valid) {
      uint32_t ls = wavLoop.start;
      uint32_t le = wavLoop.endExclusive;
      if (le > s.frames) le = s.frames;
      if (ls < le && le >= ls + 2U) {
        s.loopStart = ls;
        s.loopEnd = le;
        s.loopMode = wavLoop.mode;
        s.loopEnabled = true;
        _slotRoot[i] = wavLoop.rootNote;
      }
    }
    if (s.recorded) loadedAny = true;
  }
  _progressBase = 60; _progressSpan = 35; reportProgress(0);
  bool loadedMulti = loadMultisampleConfig(dir);
  reportProgress(100);
  if (loadedAny || loadedMulti) loadProjectMetadataFromSD(dir);
  loadedAny = loadedAny || loadedMulti;
  _selectedSlot = 0;
  _playSlot = 0;
  _recordSlot = 0;
  _playPos = 0;
  _playPosQ16 = 0;
  _progressBase = oldBase; _progressSpan = oldSpan;
  if (_progressCallback) _progressCallback(100, _progressContext);
  return loadedAny;
}

bool PhoenixAudioManager::eraseBankFromSD(const char *dir) {
  if (!dir || !dir[0]) return false;
  if (!phxC030RecoverBankTransaction(dir)) return false;
  bool ok = false;
  char cfgPath[48];
  snprintf(cfgPath, sizeof(cfgPath), "%s/BANK.CFG", dir);
  if (SD.exists(cfgPath)) {
    if (SD.remove(cfgPath)) ok = true;
  }
  char msPath[64];
  snprintf(msPath, sizeof(msPath), "%s/MULTISAMPLE.CFG", dir);
  if (SD.exists(msPath) && SD.remove(msPath)) ok = true;
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    char path[48];
    snprintf(path, sizeof(path), "%s/SLOT%u.WAV", dir, (unsigned)(i + 1));
    if (SD.exists(path)) {
      if (SD.remove(path)) ok = true;
    }
  }
  return ok;
}

void PhoenixAudioManager::audioTaskThunk(void *arg) {
  static_cast<PhoenixAudioManager*>(arg)->audioTask();
}

int32_t PhoenixAudioManager::generateToneSample() {
  // v0.7.13b Audio Test Generator:
  // Variable waveform/frequency/level service generator. It is injected directly
  // into the DAC path and bypasses sample playback, pitch, loop and echo.
  const uint16_t freq = _testFrequencyHz ? _testFrequencyHz : 440;
  const uint32_t inc = (uint32_t)(((uint64_t)freq << 32) / 32000ULL);
  const int32_t amp = _testAmpQ15;
  _phase += inc;

  switch (_testTone) {
    case TONE_SINE: {
      const uint16_t idx = (uint16_t)(_phase >> 22) & 0x03FF;
      const uint16_t idx2 = (idx + 1) & 0x03FF;
      const uint32_t frac = (_phase >> 6) & 0xFFFF;
      const int32_t s0 = PHX_SINE_1024_Q15[idx];
      const int32_t s1 = PHX_SINE_1024_Q15[idx2];
      const int32_t s = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
      return phxToneToI2S(s, amp);
    }
    case TONE_TRIANGLE: {
      const uint32_t p = _phase >> 16; // 0..65535
      int32_t s;
      if (p < 16384UL) s = (int32_t)p * 2;                    // 0 .. 32766
      else if (p < 49152UL) s = 32767 - ((int32_t)(p - 16384UL) * 2); // +32767 .. -32767
      else s = -32767 + ((int32_t)(p - 49152UL) * 2);          // -32767 .. 0
      return phxToneToI2S(s, amp);
    }
    case TONE_SAW: {
      const int32_t s = (int32_t)(_phase >> 16) - 32768;
      return phxToneToI2S(s, amp);
    }
    case TONE_SQUARE: {
      const int32_t s = (_phase & 0x80000000UL) ? 32767 : -32767;
      return phxToneToI2S(s, amp);
    }
    case TONE_NOISE: {
      _noise = 1664525UL * _noise + 1013904223UL;
      const int32_t s = (int32_t)((_noise >> 16) & 0xFFFF) - 32768;
      return phxToneToI2S(s, amp);
    }
    default:
      return 0;
  }
}

void PhoenixAudioManager::audioTask() {
  static const int FRAMES = 128;
  static const int WORDS = FRAMES * 2;
  int32_t rx[WORDS];
  int32_t tx[WORDS];

  for (;;) {
    size_t bytesRead = 0;
    esp_err_t r = i2s_read(I2S_NUM_0, rx, sizeof(rx), &bytesRead, portMAX_DELAY);
    if (r != ESP_OK || bytesRead == 0) {
      _underruns++;
      continue;
    }

    // C029/C036c: only AudioTask/Core1 may mutate voice state.
    processMidiCommandQueue();
    const uint32_t seqBlockFrames = (uint32_t)(bytesRead / (sizeof(int32_t) * 2U));
    const uint32_t seqBlockStartUs = micros();
    processSequencerCommandQueue(seqBlockStartUs, seqBlockFrames);

    const uint32_t processStartUs = micros();
    const int wordsRead = bytesRead / sizeof(int32_t);
    uint32_t peak = 0;
    uint8_t tone = _testTone;
    uint8_t state = _state;
    uint8_t recSlot = safeSlot(_recordSlot);
    uint8_t playSlot = safeSlot(_playSlot);
    int32_t sumL = 0;
    int32_t sumR = 0;
    uint32_t nSum = 0;

    // C029d: instrument only one block in N. esp_cpu_get_ccount() is
    // substantially cheaper than micros(), and the sampling ratio keeps the
    // profiler from becoming the performance problem it is measuring.
    const bool profileBlock = PHX_ENABLE_DSP_PROFILER && (_activeVoiceCount > 0) &&
                              (++_dspProfileDivider >= PHX_DSP_PROFILE_EVERY_N_BLOCKS);
    if (profileBlock) _dspProfileDivider = 0;
    uint32_t profPrepCycles = 0, profEnvelopeCycles = 0, profFetchXfadeCycles = 0;
    uint32_t profVintageCycles = 0, profFilterCycles = 0, profGainMixCycles = 0;
    uint32_t profPositionCycles = 0, profEchoOutCycles = 0;

    // C029d BLOCK PARAMETER CACHE
    // Snapshot all values that are constant for the duration of one 128-frame
    // block. MIDI voice mutations are already serialized through the audio-task
    // command queue, so a 4 ms snapshot is deterministic and avoids thousands
    // of repeated slot lookups, safeSlot(), shifts and gain products.
    uint8_t voiceSlotCache[VOICE_COUNT];
    const int16_t *voiceBufferCache[VOICE_COUNT];
    uint32_t voiceFramesCache[VOICE_COUNT];
    int32_t voiceDcOffsetCache[VOICE_COUNT];
    uint32_t voiceNormGainCache[VOICE_COUNT];
    uint16_t voiceGainLCache[VOICE_COUNT];
    uint16_t voiceGainRCache[VOICE_COUNT];
    uint8_t voiceSampleGainCache[VOICE_COUNT];
    uint8_t voiceLevelCache[VOICE_COUNT];
    uint8_t voiceEchoSendCache[VOICE_COUNT];
    bool voiceApplyDcCache[VOICE_COUNT];
    bool voiceApplyNormCache[VOICE_COUNT];
    bool voiceVintageBypassCache[VOICE_COUNT];
    bool voiceFilterBypassCache[VOICE_COUNT];
    uint32_t voiceXfadeStartCache[VOICE_COUNT];
    uint64_t voiceRangeStartQ16Cache[VOICE_COUNT];
    uint64_t voiceLoopStartQ16Cache[VOICE_COUNT];
    uint64_t voiceLoopEndQ16Cache[VOICE_COUNT];
    uint64_t voiceLoopLastQ16Cache[VOICE_COUNT];
    uint64_t voiceLoopSpanQ16Cache[VOICE_COUNT];

    const uint32_t profCacheStart = profileBlock ? esp_cpu_get_ccount() : 0U;
    if (_activeVoiceCount > 0) {
      for (uint8_t vi = 0; vi < VOICE_COUNT; ++vi) {
        InstrumentVoice &v = _voices[vi];
        voiceBufferCache[vi] = nullptr;
        if (!v.active) continue;
        const uint8_t mslot = safeSlot(v.slot);
        voiceSlotCache[vi] = mslot;

        // Marker changes are synchronized once per block instead of once per
        // voice/sample. Worst-case UI-to-audio marker latency is one block (4 ms).
        if (v.keygroup == 255U) {
          const uint32_t revision = _slotMarkerRevision[mslot];
          if (v.markerRevision != revision) {
            __sync_synchronize();
            const bool restartAtSampleStart = (v.rangeStart != _slots[mslot].sampleStart);
            syncVoiceMarkersFromSlot(v, restartAtSampleStart);
            if (!v.active) continue;
          }
        }

        const SampleSlot &vs = _slots[mslot];
        voiceBufferCache[vi] = v.sourceBuffer ? v.sourceBuffer : vs.buffer;
        voiceFramesCache[vi] = v.sourceFrames ? v.sourceFrames : vs.frames;
        voiceApplyDcCache[vi] = (v.keygroup == 255U) && vs.dcCorrectionEnabled;
        voiceApplyNormCache[vi] = (v.keygroup == 255U) && vs.normalizeEnabled;
        voiceDcOffsetCache[vi] = vs.dcOffset;
        voiceNormGainCache[vi] = vs.normalizeGainQ16;
        voiceSampleGainCache[vi] = _slotSampleGainPct[mslot];
        voiceLevelCache[vi] = _slotLevel[mslot];
        voiceEchoSendCache[vi] = _slotEchoSend[mslot];
        voiceGainLCache[vi] = (uint16_t)(((int32_t)_slotGainLQ15[mslot] * (int32_t)v.sourceGainLQ15) >> 15);
        voiceGainRCache[vi] = (uint16_t)(((int32_t)_slotGainRQ15[mslot] * (int32_t)v.sourceGainRQ15) >> 15);
        voiceVintageBypassCache[vi] = (_slotVintageSampleRate[mslot] == 0 && _slotVintageBitDepth[mslot] == 0 &&
                                       _slotVintageFilter[mslot] == 0 && _slotVintageJitter[mslot] == 0);
        voiceFilterBypassCache[vi] = (_slotFilterCutoff[mslot] >= 100 && _slotFilterResonance[mslot] == 0 &&
                                      _slotFilterEnvAmount[mslot] == 0 && _slotFilterVelocityAmount[mslot] == 0 &&
                                      _slotFilterKeytrack[mslot] == 0 && v.velocityFilterAmount == 0);
        voiceXfadeStartCache[vi] = (v.loopXfadeFrames > 0 && v.loopEndFrame >= v.loopXfadeFrames)
                                   ? (v.loopEndFrame - v.loopXfadeFrames) : v.loopEndFrame;
        voiceRangeStartQ16Cache[vi] = ((uint64_t)v.rangeStart) << 16;
        voiceLoopStartQ16Cache[vi] = ((uint64_t)v.loopStartFrame) << 16;
        voiceLoopEndQ16Cache[vi] = ((uint64_t)v.loopEndFrame) << 16;
        voiceLoopLastQ16Cache[vi] = (v.loopEndFrame > 0U) ? (((uint64_t)(v.loopEndFrame - 1U)) << 16) : 0U;
        voiceLoopSpanQ16Cache[vi] = (voiceLoopLastQ16Cache[vi] > voiceLoopStartQ16Cache[vi])
                                    ? (voiceLoopLastQ16Cache[vi] - voiceLoopStartQ16Cache[vi]) : 0U;
      }
    }
    if (profileBlock) profPrepCycles += (esp_cpu_get_ccount() - profCacheStart);

    for (int i = 0; i < wordsRead; i += 2) {
      // C036d: gate releases are evaluated at audio-frame resolution.
      advanceSequencerGates(1U);
      const int32_t inL = rx[i];
      const int32_t inR = (i + 1 < wordsRead) ? rx[i + 1] : rx[i];

      uint32_t aL = phxAbs32(inL >> 8);
      uint32_t aR = phxAbs32(inR >> 8);
      if (aL > peak) peak = aL;
      if (aR > peak) peak = aR;

      int16_t sL = (int16_t)(inL >> 16);
      int16_t sR = (int16_t)(inR >> 16);
      sumL += sL;
      sumR += sR;
      nSum++;

      if (tone != TONE_THRU) {
        int32_t out = generateToneSample();
        tx[i] = out;
        if (i + 1 < wordsRead) tx[i + 1] = out;
      } else if (state == AUDIO_ARMED || state == AUDIO_RECORD) {
        const int16_t mono16 = phxClamp16(((int32_t)sL + (int32_t)sR) >> 1);
        bool doRecord = (state == AUDIO_RECORD);
        bool triggerCopiedCurrent = false;

        if (!doRecord) {
          // C017: continuously retain the latest 1024 mono frames while armed.
          // The triggering frame is written before the threshold test, so the
          // copied history ends exactly at the trigger and no attack sample is lost.
          _preTrigger[_preTriggerWrite] = mono16;
          _preTriggerWrite = (uint16_t)((_preTriggerWrite + 1U) % PRE_TRIGGER_FRAMES);
          if (_preTriggerCount < PRE_TRIGGER_FRAMES) ++_preTriggerCount;

          const uint32_t samplePeak = (aL > aR) ? aL : aR;
          if (samplePeak >= triggerThresholdRaw()) {
            // C022: threshold crossing is the first point at which AUTO is
            // allowed to replace an occupied slot.
            resetSlotForNewRecording(recSlot);
            SampleSlot &s = _slots[recSlot];
            uint32_t copied = 0;
            if (s.buffer && s.capacityFrames) {
              uint16_t idx = (uint16_t)((_preTriggerWrite + PRE_TRIGGER_FRAMES - _preTriggerCount) % PRE_TRIGGER_FRAMES);
              uint16_t count = _preTriggerCount;
              while (count && copied < s.capacityFrames) {
                s.buffer[copied++] = _preTrigger[idx];
                idx = (uint16_t)((idx + 1U) % PRE_TRIGGER_FRAMES);
                --count;
              }
            }
            _recordPos = copied;
            _preTriggerCount = 0;
            _state = AUDIO_RECORD;
            state = AUDIO_RECORD;
            doRecord = true;
            triggerCopiedCurrent = true;
          }
        }

        if (doRecord) {
          SampleSlot &s = _slots[recSlot];
          uint32_t pos = _recordPos;
          // In AUTO mode the current frame is already the final frame copied
          // from the pre-trigger ring. Manual recording writes it normally.
          if (triggerCopiedCurrent) {
            tx[i] = inL;
            if (i + 1 < wordsRead) tx[i + 1] = inR;
          } else if (s.buffer && pos < s.capacityFrames) {
            s.buffer[pos] = mono16;
            _recordPos = pos + 1;
            tx[i] = inL;
            if (i + 1 < wordsRead) tx[i + 1] = inR;
          } else {
            finalizeRecordedSlot(recSlot);
            _state = AUDIO_THRU;
            tx[i] = inL;
            if (i + 1 < wordsRead) tx[i + 1] = inR;
          }
        } else {
          tx[i] = inL;
          if (i + 1 < wordsRead) tx[i + 1] = inR;
        }
      } else if (state == AUDIO_PREVIEW) {
        uint32_t pp = _previewPos;
        if (_previewBuffer && pp < _previewFrames) {
          int16_t dry16 = _previewBuffer[pp];
          int32_t out = ((int32_t)dry16) << 16;
          tx[i] = out;
          if (i + 1 < wordsRead) tx[i + 1] = out;
          pp++;
          _previewPos = pp;
          _playPos = pp;
          if (pp >= _previewFrames) {
            _state = AUDIO_THRU;
            state = AUDIO_THRU;
            _previewPos = 0;
          }
        } else {
          _state = AUDIO_THRU;
          state = AUDIO_THRU;
          _previewPos = 0;
          tx[i] = 0;
          if (i + 1 < wordsRead) tx[i + 1] = 0;
        }
      } else if (_activeVoiceCount > 0) {
        int32_t mixL = 0;
        int32_t mixR = 0;
        int32_t echoSendMix = 0;
        uint8_t live = 0;
        for (uint8_t vi = 0; vi < VOICE_COUNT; ++vi) {
          InstrumentVoice &v = _voices[vi];
          if (!v.active) continue;
          if (v.seqStartDelayFrames > 0U) { --v.seqStartDelayFrames; ++live; continue; }
          const uint32_t profPrepStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          const uint8_t mslot = voiceSlotCache[vi];
          if (v.glideFramesLeft > 0) {
            v.incQ16 = (uint32_t)((int32_t)v.incQ16 + v.glideStepQ16);
            if (--v.glideFramesLeft == 0) v.incQ16 = v.targetIncQ16;
          }
          const int16_t *voiceBuffer = voiceBufferCache[vi];
          const uint32_t voiceFrames = voiceFramesCache[vi];
          const bool sustainLoopActive = !v.reverse && !v.releasing &&
                                         (v.keyHeld || v.sustained) &&
                                         v.loopMode != LOOP_OFF &&
                                         v.loopEndFrame > v.loopStartFrame + 1U;
          if (!sustainLoopActive && v.loopMode == LOOP_ALTERNATE && !v.loopForward) {
            // Note Off while travelling backwards: leave the loop immediately
            // and continue toward Sample End from the current position.
            v.loopForward = true;
          }
          uint32_t pos = (uint32_t)(v.posQ16 >> 16);
          if (!voiceBuffer || pos >= voiceFrames || pos < v.rangeStart || pos >= v.rangeEnd) {
            v.active = false;
            continue;
          }

          const uint32_t profEnvelopeStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profPrepCycles += (profEnvelopeStart - profPrepStart);

          // v0.7.15: per-slot ADSR envelope, calculated once when the voice starts.
          if (v.releasing) v.envStage = 3;
          if (v.envStage == 0) {
            uint32_t e = (uint32_t)v.envQ15 + v.attackStepQ15;
            if (e >= 32767UL) { v.envQ15 = 32767U; v.envStage = 1; }
            else v.envQ15 = (uint16_t)e;
          } else if (v.envStage == 1) {
            if (v.envQ15 <= v.sustainQ15 || v.decayStepQ15 >= v.envQ15 - v.sustainQ15) { v.envQ15 = v.sustainQ15; v.envStage = 2; }
            else v.envQ15 = (uint16_t)(v.envQ15 - v.decayStepQ15);
          } else if (v.envStage == 2) {
            v.envQ15 = v.sustainQ15;
          } else {
            if (v.envQ15 <= v.releaseStepQ15) { v.envQ15 = 0; v.active = false; continue; }
            v.envQ15 = (uint16_t)(v.envQ15 - v.releaseStepQ15);
          }

          const uint32_t profFetchStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profEnvelopeCycles += (profFetchStart - profEnvelopeStart);

          int32_t rawSample = voiceBuffer[pos];
          if (sustainLoopActive && v.loopMode == LOOP_FORWARD && v.loopXfadeFrames > 0 && v.loopForward &&
              pos >= voiceXfadeStartCache[vi] && pos < v.loopEndFrame) {
            const uint32_t off = pos - voiceXfadeStartCache[vi];
            const uint32_t p2 = v.loopStartFrame + off;
            if (p2 < voiceFrames) {
              const int32_t a = rawSample, b = voiceBuffer[p2];
              rawSample = (a * (int32_t)(v.loopXfadeFrames - off) + b * (int32_t)off) / (int32_t)v.loopXfadeFrames;
            }
          }
          // C018/C029d: reversible processing remains non-destructive, but
          // the enabled flags and constants are now snapshotted once per block.
          if (voiceApplyDcCache[vi]) rawSample -= voiceDcOffsetCache[vi];
          if (voiceApplyNormCache[vi]) rawSample = (int32_t)(((int64_t)rawSample * (int64_t)voiceNormGainCache[vi]) >> 16);
          const uint32_t profVintageStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profFetchXfadeCycles += (profVintageStart - profFetchStart);
          int32_t sample = phxClamp16(rawSample);
          if (!voiceVintageBypassCache[vi])
            sample = processVintage(mslot, (int16_t)sample, v.vintagePhase, v.vintageHold, v.vintageFilterState, v.vintageNoise);
          const uint32_t profFilterStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profVintageCycles += (profFilterStart - profVintageStart);
          if (!voiceFilterBypassCache[vi]) sample = processVoiceFilter(v, phxClamp16(sample));
          const uint32_t profGainMixStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profFilterCycles += (profGainMixStart - profFilterStart);
          sample = (sample * (int32_t)v.envQ15) >> 15;
          sample = (sample * (int32_t)v.velocityLevel) / 127;
          sample = (sample * (int32_t)voiceSampleGainCache[vi]) / 100;
          mixL += (sample * (int32_t)voiceGainLCache[vi]) >> 15;
          mixR += (sample * (int32_t)voiceGainRCache[vi]) >> 15;
          const int32_t leveled = (sample * (int32_t)voiceLevelCache[vi]) / 100;
          echoSendMix += (leveled * (int32_t)voiceEchoSendCache[vi]) / 100;
          ++live;

          const uint32_t profPositionStart = profileBlock ? esp_cpu_get_ccount() : 0U;
          if (profileBlock) profGainMixCycles += (profPositionStart - profGainMixStart);

          if (v.reverse) {
            const uint64_t lo = voiceRangeStartQ16Cache[vi];
            if (v.posQ16 <= (uint64_t)v.incQ16 || v.posQ16 <= lo) beginVoiceRelease(v);
            else v.posQ16 -= v.incQ16;
          } else if (sustainLoopActive && v.loopMode == LOOP_ALTERNATE) {
            const uint64_t loopStartQ16 = voiceLoopStartQ16Cache[vi];
            const uint64_t loopLastQ16 = voiceLoopLastQ16Cache[vi];
            if (v.loopForward) {
              uint64_t next = v.posQ16 + (uint64_t)v.incQ16;
              if (next > loopLastQ16) {
                uint64_t over = next - loopLastQ16;
                const uint64_t span = voiceLoopSpanQ16Cache[vi];
                if (span > 0U) over %= (span * 2U);
                if (over <= span) { v.posQ16 = loopLastQ16 - over; v.loopForward = false; }
                else { v.posQ16 = loopStartQ16 + (over - span); v.loopForward = true; }
              } else v.posQ16 = next;
            } else {
              const uint64_t distance = v.posQ16 - loopStartQ16;
              if ((uint64_t)v.incQ16 > distance) {
                uint64_t over = (uint64_t)v.incQ16 - distance;
                const uint64_t span = voiceLoopSpanQ16Cache[vi];
                if (span > 0U) over %= (span * 2U);
                if (over <= span) { v.posQ16 = loopStartQ16 + over; v.loopForward = true; }
                else { v.posQ16 = loopLastQ16 - (over - span); v.loopForward = false; }
              } else v.posQ16 -= v.incQ16;
            }
          } else {
            v.posQ16 += v.incQ16;
            if (sustainLoopActive && v.loopMode == LOOP_FORWARD) {
              const uint64_t loopStartQ16 = voiceLoopStartQ16Cache[vi];
              const uint64_t loopEndQ16 = voiceLoopEndQ16Cache[vi];
              if (v.posQ16 >= loopEndQ16) {
                const uint64_t loopLengthQ16 = loopEndQ16 - loopStartQ16;
                v.posQ16 = loopStartQ16 + ((v.posQ16 - loopEndQ16) % loopLengthQ16);
              }
            } else if ((uint32_t)(v.posQ16 >> 16) >= v.rangeEnd) {
              beginVoiceRelease(v);
            }
          }
          if (profileBlock) profPositionCycles += (esp_cpu_get_ccount() - profPositionStart);
        }
        _activeVoiceCount = live;
        // Constant -6 dB mix gain for the shared 1..12 voice pool.
        // The output no longer becomes abruptly quieter at voice 5 or 9.
        // Only real summing peaks are handled by the soft limiter.
        const uint32_t profEchoStart = profileBlock ? esp_cpu_get_ccount() : 0U;
        int16_t dryL16 = phxSoftLimit16(mixL >> 1);
        int16_t dryR16 = phxSoftLimit16(mixR >> 1);
        int16_t send16 = phxSoftLimit16(echoSendMix >> 1);
        int16_t wet16 = processEcho(0, send16);
        tx[i] = ((int32_t)phxSoftLimit16((int32_t)dryL16 + wet16)) << 16;
        if (i + 1 < wordsRead) tx[i + 1] = ((int32_t)phxSoftLimit16((int32_t)dryR16 + wet16)) << 16;

        // v0.7.19c: short samples may finish before the selected delay time.
        // Keep processing the shared echo buffer after the final polyphonic voice
        // has ended, otherwise the first delayed repeat would never be heard.
        if (live == 0 && _echoMix > 0) startEchoTail();
        if (profileBlock) profEchoOutCycles += (esp_cpu_get_ccount() - profEchoStart);
      } else if (state == AUDIO_PLAY || state == AUDIO_PLAY_REVERSE) {
        SampleSlot &s = _slots[playSlot];
        uint32_t slotSampleStart = 0U, slotSampleEnd = 0U, slotLoopStart = 0U, slotLoopEnd = 0U;
        uint8_t slotLoopMode = LOOP_OFF;
        resolveSlotPlaybackMarkers(playSlot, slotSampleStart, slotSampleEnd, slotLoopStart, slotLoopEnd, slotLoopMode);

        uint32_t rangeStart = _playRangeActive ? _playRangeStart : slotSampleStart;
        uint32_t rangeEnd = _playRangeActive ? _playRangeEnd : slotSampleEnd;
        if (rangeEnd > s.frames) rangeEnd = s.frames;
        if (rangeStart >= rangeEnd) { rangeStart = slotSampleStart; rangeEnd = slotSampleEnd; }

        // Explicit range audition (Loop Points) loops exactly the requested
        // [start,end) range. Normal Sample Editor replay uses the canonical
        // L.START/L.END markers. Both therefore share the same end-exclusive
        // boundary rules as MIDI playback.
        uint32_t loopStartFrame = _playRangeActive ? rangeStart : slotLoopStart;
        uint32_t loopEndFrame = _playRangeActive ? rangeEnd : slotLoopEnd;
        uint8_t transportLoopMode = (state == AUDIO_PLAY) ? slotLoopMode : LOOP_OFF;
        if (_playRangeActive && state == AUDIO_PLAY && slotLoopMode != LOOP_OFF)
          transportLoopMode = slotLoopMode;
        if (loopStartFrame < rangeStart) loopStartFrame = rangeStart;
        if (loopEndFrame == 0U || loopEndFrame > rangeEnd) loopEndFrame = rangeEnd;
        if (loopEndFrame <= loopStartFrame + 1U) transportLoopMode = LOOP_OFF;
        const bool transportLoopActive = transportLoopMode != LOOP_OFF;
        if (transportLoopMode != LOOP_ALTERNATE) _transportLoopForward = true;

        uint64_t posQ16 = _playPosQ16;
        uint32_t pos = (uint32_t)(posQ16 >> 16);
        uint32_t inc = _playIncQ16;
        if (inc == 0U) inc = 65536UL;

        bool transportValid = s.buffer && rangeEnd > rangeStart + 1U;
        if (transportValid && (pos < rangeStart || pos >= rangeEnd)) {
          if (state == AUDIO_PLAY && transportLoopActive) {
            pos = loopStartFrame;
            posQ16 = ((uint64_t)pos) << 16;
            _transportLoopForward = true;
          } else {
            transportValid = false;
          }
        }

        // A marker can be dragged behind the current playback head. Enter the
        // edited loop immediately instead of playing the stale old range once.
        if (transportValid && state == AUDIO_PLAY && transportLoopActive) {
          if (transportLoopMode == LOOP_FORWARD && pos >= loopEndFrame) {
            const uint64_t lsQ16 = ((uint64_t)loopStartFrame) << 16;
            const uint64_t leQ16 = ((uint64_t)loopEndFrame) << 16;
            const uint64_t lenQ16 = leQ16 - lsQ16;
            posQ16 = lsQ16 + ((posQ16 - leQ16) % lenQ16);
            pos = (uint32_t)(posQ16 >> 16);
          } else if (transportLoopMode == LOOP_ALTERNATE) {
            const uint64_t lsQ16 = ((uint64_t)loopStartFrame) << 16;
            const uint64_t lastQ16 = ((uint64_t)(loopEndFrame - 1U)) << 16;
            if (!_transportLoopForward && posQ16 < lsQ16) {
              posQ16 = lsQ16;
              pos = loopStartFrame;
              _transportLoopForward = true;
            } else if (_transportLoopForward && posQ16 > lastQ16) {
              uint64_t span = lastQ16 - lsQ16;
              uint64_t over = posQ16 - lastQ16;
              if (span > 0U) over %= (span * 2U);
              if (over <= span) {
                posQ16 = lastQ16 - over;
                _transportLoopForward = false;
              } else {
                posQ16 = lsQ16 + (over - span);
                _transportLoopForward = true;
              }
              pos = (uint32_t)(posQ16 >> 16);
            }
          }
        }

        if (transportValid && pos < s.frames) {
          _playPosQ16 = posQ16;
          _playPos = pos;

          // v0.6.3 Pitch Converter: fixed-point sample position.
          // Nearest-neighbour is intentionally used for a crunchy SFX-like sound.
          int32_t transportRaw = s.buffer[pos];
          if (transportLoopActive && transportLoopMode == LOOP_FORWARD && s.loopXfadeMs > 0U) {
            uint32_t xf = (uint32_t)s.loopXfadeMs * 32U;
            const uint32_t loopLen = loopEndFrame - loopStartFrame;
            if (xf > loopLen / 2U) xf = loopLen / 2U;
            if (xf > 0U && pos >= loopEndFrame - xf && pos < loopEndFrame) {
              const uint32_t off = pos - (loopEndFrame - xf);
              const uint32_t p2 = loopStartFrame + off;
              if (p2 < s.frames) {
                const int32_t a = s.buffer[pos], b = s.buffer[p2];
                transportRaw = (a * (int32_t)(xf - off) + b * (int32_t)off) / (int32_t)xf;
              }
            }
          }
          if (s.dcCorrectionEnabled) transportRaw -= s.dcOffset;
          if (s.normalizeEnabled) transportRaw = (int32_t)(((int64_t)transportRaw * (int64_t)s.normalizeGainQ16) >> 16);
          int16_t dry16 = processVintage(playSlot, phxClamp16(transportRaw), _transportVintagePhase, _transportVintageHold, _transportVintageFilterState, _transportVintageNoise);
          const uint8_t mslot=safeSlot(playSlot);
          int16_t dryL16=phxSoftLimit16(((int32_t)dry16*_slotGainLQ15[mslot])>>15);
          int16_t dryR16=phxSoftLimit16(((int32_t)dry16*_slotGainRQ15[mslot])>>15);
          int16_t leveled=phxClamp16(((int32_t)dry16*_slotLevel[mslot])/100);
          int16_t send16 = phxClamp16(((int32_t)leveled * (int32_t)_slotEchoSend[mslot]) / 100);
          int16_t wet16 = processEcho(0, send16);
          tx[i] = ((int32_t)phxSoftLimit16((int32_t)dryL16+wet16)) << 16;
          if (i + 1 < wordsRead) tx[i + 1] = ((int32_t)phxSoftLimit16((int32_t)dryR16+wet16)) << 16;

          if (state == AUDIO_PLAY_REVERSE) {
            const uint64_t rangeStartQ16 = ((uint64_t)rangeStart) << 16;
            if (posQ16 <= (uint64_t)inc || posQ16 <= rangeStartQ16) {
              // Preserve the legacy reverse audition loop. Alternate is a
              // forward sustain mode and is therefore not applied here.
              if (s.loopEnabled && rangeEnd > rangeStart + 1U) {
                const uint32_t restart = rangeEnd - 1U;
                _playPos = restart;
                _playPosQ16 = ((uint64_t)restart) << 16;
              } else {
                _playPos = rangeStart;
                _playPosQ16 = ((uint64_t)rangeStart) << 16;
                _state = AUDIO_THRU;
                _playRangeActive = false;
                state = AUDIO_THRU;
                startEchoTail();
              }
            } else {
              posQ16 -= inc;
              _playPosQ16 = posQ16;
              _playPos = (uint32_t)(posQ16 >> 16);
            }
          } else if (transportLoopActive && transportLoopMode == LOOP_ALTERNATE) {
            const uint64_t loopStartQ16 = ((uint64_t)loopStartFrame) << 16;
            const uint64_t loopLastQ16 = ((uint64_t)(loopEndFrame - 1U)) << 16;
            if (_transportLoopForward) {
              uint64_t next = posQ16 + (uint64_t)inc;
              if (next > loopLastQ16) {
                uint64_t over = next - loopLastQ16;
                const uint64_t span = loopLastQ16 - loopStartQ16;
                if (span > 0U) over %= (span * 2U);
                if (over <= span) { _playPosQ16 = loopLastQ16 - over; _transportLoopForward = false; }
                else { _playPosQ16 = loopStartQ16 + (over - span); _transportLoopForward = true; }
              } else _playPosQ16 = next;
            } else {
              const uint64_t distance = posQ16 - loopStartQ16;
              if ((uint64_t)inc > distance) {
                uint64_t over = (uint64_t)inc - distance;
                const uint64_t span = loopLastQ16 - loopStartQ16;
                if (span > 0U) over %= (span * 2U);
                if (over <= span) { _playPosQ16 = loopStartQ16 + over; _transportLoopForward = true; }
                else { _playPosQ16 = loopLastQ16 - (over - span); _transportLoopForward = false; }
              } else _playPosQ16 = posQ16 - inc;
            }
            _playPos = (uint32_t)(_playPosQ16 >> 16);
          } else {
            posQ16 += inc;
            if (transportLoopActive && transportLoopMode == LOOP_FORWARD) {
              const uint64_t loopStartQ16 = ((uint64_t)loopStartFrame) << 16;
              const uint64_t loopEndQ16 = ((uint64_t)loopEndFrame) << 16;
              if (posQ16 >= loopEndQ16) {
                const uint64_t loopLengthQ16 = loopEndQ16 - loopStartQ16;
                posQ16 = loopStartQ16 + ((posQ16 - loopEndQ16) % loopLengthQ16);
              }
              _playPosQ16 = posQ16;
              _playPos = (uint32_t)(posQ16 >> 16);
            } else if ((uint32_t)(posQ16 >> 16) >= rangeEnd) {
              _playPos = rangeEnd;
              _playPosQ16 = ((uint64_t)(rangeEnd - 1U)) << 16;
              _state = AUDIO_THRU;
              _playRangeActive = false;
              state = AUDIO_THRU;
              startEchoTail();
            } else {
              _playPosQ16 = posQ16;
              _playPos = (uint32_t)(posQ16 >> 16);
            }
          }
        } else {
          _state = AUDIO_THRU;
          _playRangeActive = false;
          _transportLoopForward = true;
          state = AUDIO_THRU;
          startEchoTail();
          tx[i] = 0;
          if (i + 1 < wordsRead) tx[i + 1] = 0;
        }
      } else {
        if (_echoTailActive && _echoMix > 0) {
          // v0.6.5 Echo Tail: no new source signal is fed after STOP,
          // but the echo buffer keeps feeding back until it becomes quiet.
          int16_t tail16 = processEcho(0, 0);
          int32_t out = ((int32_t)tail16) << 16;
          tx[i] = out;
          if (i + 1 < wordsRead) tx[i + 1] = out;
          uint16_t a = (tail16 < 0) ? (uint16_t)(-tail16) : (uint16_t)tail16;
          if (a < 8) {
            if (_echoTailQuietFrames < 4000) _echoTailQuietFrames++;
            else stopEchoTail();
          } else {
            _echoTailQuietFrames = 0;
          }
        } else {
          tx[i] = inL;
          if (i + 1 < wordsRead) tx[i + 1] = inR;
        }
      }
    }

    // C008: Publish a stable 32-bit playhead for the newest active MIDI
    // voice of each slot. The GUI reads this once per refresh, so no 64-bit
    // voice-position value is read concurrently across the two ESP32 cores.
    updateSlotVoicePlayheads();

    if (nSum > 0) {
      int16_t avgL = (int16_t)(sumL / (int32_t)nSum);
      int16_t avgR = (int16_t)(sumR / (int32_t)nSum);
      _dcOffsetL = (int16_t)(_dcOffsetL + ((int32_t)avgL - _dcOffsetL) / 16);
      _dcOffsetR = (int16_t)(_dcOffsetR + ((int32_t)avgR - _dcOffsetR) / 16);
    }

    _frameCounter += (wordsRead / 2);
    _lastPeakRaw = peak;
    updatePeakAndClip(peak);

    const uint32_t processUs = (uint32_t)(micros() - processStartUs);
    _audioLoadLastUs = processUs;
    if (processUs > _audioLoadMaxUs) _audioLoadMaxUs = processUs;
    if (processUs > _audioLoadPeakUs) _audioLoadPeakUs = processUs;
    _audioLoadAccumUs += processUs;
    _audioLoadBlocks++;
    // A 128-frame block at 32 kHz has a 4000 us deadline.
    if (processUs >= 3600UL) _audioRiskCount++;
    if (processUs > 4000UL) _audioOverrunCount++;
    // C029d voice-count cost histogram. 32-bit counters are deliberate so
    // Core0 can read them atomically without a mutex. Saturate sums before wrap.
    uint8_t histVoices = _activeVoiceCount; if (histVoices > VOICE_COUNT) histVoices = VOICE_COUNT;
    if (_voiceHistCount[histVoices] != 0xFFFFFFFFUL) ++_voiceHistCount[histVoices];
    if (_voiceHistSumUs[histVoices] <= 0xFFFFFFFFUL - processUs) _voiceHistSumUs[histVoices] += processUs;
    if (processUs > _voiceHistMaxUs[histVoices]) _voiceHistMaxUs[histVoices] = processUs;
    if (profileBlock) {
      // CPU is fixed at 240 MHz in the supported Phoenix target: 240 cycles/us.
      _dspPrepUs = profPrepCycles / 240UL;
      _dspEnvelopeUs = profEnvelopeCycles / 240UL;
      _dspFetchXfadeUs = profFetchXfadeCycles / 240UL;
      _dspSetupEnvUs = _dspPrepUs + _dspEnvelopeUs + _dspFetchXfadeUs;
      _dspVintageUs = profVintageCycles / 240UL;
      _dspFilterUs = profFilterCycles / 240UL;
      _dspGainMixUs = profGainMixCycles / 240UL;
      _dspPositionUs = profPositionCycles / 240UL;
      _dspMixPosUs = _dspGainMixUs + _dspPositionUs;
      _dspEchoOutUs = profEchoOutCycles / 240UL;
      ++_dspProfileSamples;
    }

    const uint32_t loadNowMs = millis();
    if (loadNowMs - _audioLoadWindowStartMs >= 1000UL) {
      _audioLoadAvgUs = (_audioLoadBlocks > 0) ? (uint32_t)(_audioLoadAccumUs / _audioLoadBlocks) : 0;
      _audioLoadAccumUs = 0;
      _audioLoadBlocks = 0;
      _audioLoadWindowStartMs = loadNowMs;
    }

    size_t bytesWritten = 0;
    esp_err_t w = i2s_write(I2S_NUM_0, tx, bytesRead, &bytesWritten, portMAX_DELAY);
    if (w != ESP_OK || bytesWritten != bytesRead) _underruns++;
  }
}

void PhoenixAudioManager::updatePeakAndClip(uint32_t raw) {
  // True zero plus a small dead zone keeps LED 1 dark for ADC idle noise.
  uint8_t lvl = 0;
  if (raw > 0x004000UL) lvl = 1;
  if (raw > 0x010600UL) lvl = 2;
  if (raw > 0x020C00UL) lvl = 3;
  if (raw > 0x040000UL) lvl = 4;
  if (raw > 0x080000UL) lvl = 5;
  if (raw > 0x100000UL) lvl = 6;
  if (raw > 0x400000UL) lvl = 7;
  if (raw > 0x5A0000UL) lvl = 8;

  uint8_t old = _peakLevel;
  if (lvl >= old) _peakLevel = lvl;
  else if (old > 0) _peakLevel = old - 1;

  const uint32_t now = millis();
  if (lvl >= _peakHoldLevel) {
    _peakHoldLevel = lvl;
    _peakHoldUntilMs = now + 1000;
    _peakReleaseMs = now;
  }

  if (raw >= 0x720000UL) {
    _highActive = true;
    _highHoldUntilMs = now + 1200;
  }
  if (raw >= 0x7E0000UL) {
    if (!_clipActive) _clipCount++;
    _clipActive = true;
    _clipHoldUntilMs = now + 3000;
  }
}

void PhoenixAudioManager::update(PhoenixEventBus &events) {
  const uint32_t now = millis();
  if (now - _lastTickMs < 30) return;
  _lastTickMs = now;

  if (_clipActive && _clipHoldUntilMs != 0 && (int32_t)(now - _clipHoldUntilMs) > 0) _clipActive = false;
  if (_highActive && _highHoldUntilMs != 0 && (int32_t)(now - _highHoldUntilMs) > 0) _highActive = false;
  if (_peakHoldLevel > _peakLevel && (int32_t)(now - _peakHoldUntilMs) > 0 && now - _peakReleaseMs > 180) {
    _peakReleaseMs = now;
    _peakHoldLevel--;
  }

  if (_peakLevel != _lastPeakLevel) {
    _lastPeakLevel = _peakLevel;
    events.post(PHX_EVT_AUDIO_PEAK_CHANGED, _peakLevel);
  }

  if (now - _audioStatsPrintMs >= 1000UL) {
    _audioStatsPrintMs = now;
    const float avgPct = ((float)_audioLoadAvgUs * 100.0f) / 4000.0f;
    const float maxPct = ((float)_audioLoadMaxUs * 100.0f) / 4000.0f;
#if PHX_SERIAL_LEVEL >= 2
    Serial.printf("AUDIO load avg=%luus %.1f%% winmax=%luus %.1f%% peak=%luus last=%luus voices=%u/%u vpeak=%u risk=%lu overrun=%lu underruns=%lu\n",
                  (unsigned long)_audioLoadAvgUs, avgPct,
                  (unsigned long)_audioLoadMaxUs, maxPct,
                  (unsigned long)_audioLoadPeakUs,
                  (unsigned long)_audioLoadLastUs,
                  (unsigned)_activeVoiceCount, (unsigned)VOICE_COUNT,
                  (unsigned)_activeVoicePeak,
                  (unsigned long)_audioRiskCount,
                  (unsigned long)_audioOverrunCount,
                  (unsigned long)_underruns);
    if (now - _dspProfilerPrintMs >= 2000UL && _dspProfileSamples > 0) {
      _dspProfilerPrintMs = now;
      Serial.printf("C030 DSP samples=%lu prep=%luus env=%luus fetch_xf=%luus vintage=%luus filter=%luus gain_mix=%luus pos=%luus echo=%luus | V:",
                    (unsigned long)_dspProfileSamples, (unsigned long)_dspPrepUs,
                    (unsigned long)_dspEnvelopeUs, (unsigned long)_dspFetchXfadeUs,
                    (unsigned long)_dspVintageUs, (unsigned long)_dspFilterUs,
                    (unsigned long)_dspGainMixUs, (unsigned long)_dspPositionUs,
                    (unsigned long)_dspEchoOutUs);
      for (uint8_t n = 1; n <= VOICE_COUNT; ++n) {
        if (_voiceHistCount[n]) Serial.printf(" %u=%lu/%lu", (unsigned)n, (unsigned long)(_voiceHistSumUs[n] / _voiceHistCount[n]), (unsigned long)_voiceHistMaxUs[n]);
      }
      Serial.println(F(" us(avg/max)"));
    }
    if (now - _voiceAuditPrintMs >= 2000UL) {
      _voiceAuditPrintMs = now;
      printVoiceAudit();
    }
#endif
    _audioLoadMaxUs = 0;
  }
}

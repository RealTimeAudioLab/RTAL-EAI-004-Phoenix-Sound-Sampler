# Project Phoenix v1.1.0 FINAL — Release Notes

## Release baseline

v1.1.0 FINAL is promoted from the hardware-validated `Phoenix_v1_1_0_DEV3b_REVERB_TIMING_LIGHT` build. Release promotion changes only version strings, serial-log prefixes and packaging/documentation. No new DSP algorithm was introduced after DEV3b validation.

## Main changes since v1.0

### Stereo Reverb

Phoenix now includes a compact integer stereo Schroeder/Moorer Reverb:

- four damped feedback combs
- two decorrelating allpass stages per side
- SIZE / DECAY / DAMP / MIX
- independent Reverb send for sample slots S1..S4
- BANK16 persistence for Reverb state

The render path avoids expensive transcendental functions.

### Reverb FAST-CACHE

The Reverb topology and equations are unchanged, but derived values are cached at 128-frame block rate:

- comb/allpass memory offsets
- effective delay lengths
- feedback Q15 coefficients
- damping shift

This removed repeated invariant work from the per-sample Reverb path.

### Reverb memory in internal SRAM

The complete Reverb delay memory is 6,930 `int16_t` samples = 13,860 bytes. v1.1.0 requests internal 8-bit SRAM for this buffer and keeps a safe fallback allocation path.

Hardware comparison showed that this was a useful latency optimization. The larger 128 kB Echo ring remains in PSRAM; moving that entire ring to SRAM was experimentally rejected and is not included in the final release.

### SD A/B session store

Phoenix session persistence for the active bank, screen and slot is independent from NVS.

Files:

```text
/PHOENIX/SESSION_A.BIN
/PHOENIX/SESSION_B.BIN
```

Each 20-byte record includes:

- magic
- format version
- record size
- 32-bit sequence
- bank
- screen
- slot
- CRC32

Phoenix alternates A/B writes and verifies each write by re-reading it. Boot selects the newest valid record; if one slot is incomplete or corrupt, the other remains valid.

### NEW BANK defaults

NEW BANK supports:

- compiled Factory Defaults
- optional `/PHOENIX/INIT_SOUND.CFG` format v1

The INIT template carries sound/mapping settings only and does not store WAV/sample paths. NEW BANK clears sample and song/pattern state in RAM until the user explicitly saves the bank.

## Performance configuration

```text
ESP32-S3       240 MHz
Audio          32 kHz / 128 frames
Deadline       4000 us
Voice pool     12
Optimization   O3 + fast-math
Reverb timing  compiled out by default
```

Reference hardware stress case:

```text
12 voices
Echo 45
Reverb 49
Size 90
Decay 91
Damp 45
```

Observed DEV3b result:

```text
Peak            3744 us (93.6%)
Headroom         256 us
Typical 12-voice ~2750-2810 us
Overrun          0
Underrun         0
MIDI drops       0
Sequencer drops  0
Scheduling miss  0
```

## Compatibility

```text
BANK format        16
MULTISAMPLE format 10
SD session format   1
```

BANK15 remains load-compatible; fields introduced with Reverb use safe defaults when absent.

DEV2k/DEV3/DEV3a/DEV3b SD session files remain compatible with v1.1.0 FINAL.

## Release decision

Development was intentionally stopped after DEV3b rather than adding another optimization experiment. The goal of v1.1.0 FINAL is a reproducible, hardware-tested Phoenix baseline with measurable real-time headroom and robust persistence.

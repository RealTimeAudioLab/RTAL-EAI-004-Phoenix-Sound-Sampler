# Project Phoenix v1.1.0

Project Phoenix v1.1.0 is the new stable RealTimeAudioLab release for the ESP32-S3 Phoenix sampling platform.

## Highlights

- 12-voice shared polyphonic engine
- new stereo integer Schroeder/Moorer Reverb
- Reverb SIZE / DECAY / DAMP / MIX + per-slot sends
- Reverb FAST-CACHE and internal-SRAM delay memory
- O3 + FASTMATH audio build
- robust SD A/B session persistence with CRC32
- session bank/screen/slot persistence no longer depends on NVS
- Factory Defaults + optional `INIT_SOUND.CFG` template
- BANK16 / MULTISAMPLE10

## Hardware validation

Reference stress case: 12 voices, Echo 45, Reverb 49, Size 90, Decay 91, Damp 45.

- observed peak: **3744 us / 93.6%** of the 4 ms audio deadline
- remaining headroom: **256 us**
- sustained 12-voice load: about **68.7-70.2%**
- **0 audio overruns**
- **0 audio underruns**
- **0 MIDI command/control drops**
- **0 sequencer command drops / scheduling misses**

v1.1.0 FINAL is promoted directly from the hardware-validated DEV3b baseline. No additional DSP experiment was added after validation.

See `RELEASE_NOTES_v1_1_0.md` and `CHANGELOG.md` for details.

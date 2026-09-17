# Project Phoenix Changelog

## v1.1.0 FINAL — 2026-09-16

Release baseline: hardware-validated `DEV3b_REVERB_TIMING_LIGHT`.

### Added

- Stereo integer Schroeder/Moorer Reverb.
- Reverb SIZE / DECAY / DAMP / MIX controls.
- Per-slot Reverb sends for S1..S4.
- BANK format 16 with backward-compatible BANK15 loading.
- Factory Defaults and optional `/PHOENIX/INIT_SOUND.CFG` NEW BANK template.
- SD A/B session journal for active bank/screen/slot.
- CRC32 + sequence-based recovery for session records.

### Performance and stability

- `-O3` + `fast-math` retained for the audio translation unit.
- Reverb FAST-CACHE moves invariant calculations out of the per-sample path.
- Reverb delay memory moved to internal SRAM (13,860 bytes) with fallback.
- Diagnostic loop-XFade hotpath counters removed.
- Per-frame Reverb cycle timing removed from the default build.
- Compact high-water `PHX PEAKTRACE` retained for blocks >= 3500 us.
- 12-voice hardware stress test: observed peak 3744 us at a 4000 us deadline with 0 overruns/underruns/drops.

### Persistence

- Phoenix bank/screen/slot session persistence is SD-only.
- Phoenix session code no longer creates, enumerates, modifies or erases NVS.
- `/PHOENIX/LASTBANK.CFG` remains as a legacy fallback/migration source.

### Memory policy

- Reverb delay memory: internal SRAM.
- Echo delay ring: PSRAM.
- Full Echo-ring-internal-RAM experiment was evaluated after DEV3b and rejected; it is not present in v1.1.0 FINAL.

### Formats

- BANK: 16
- MULTISAMPLE: 10
- SD Session: 1

## v1.0.x

The v1.0 series established the stable Phoenix sampling, editing, bank, multisample, sequencer/song, MIDI, USB storage and 12-voice platform used as the foundation for v1.1.0. Detailed historical engineering notes are preserved under `history/`.

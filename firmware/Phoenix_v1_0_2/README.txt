Project Phoenix v0.8.04a C029
Concurrency & Stability Baseline

Current baseline:
- AudioTask: Core 1 / priority 24
- MIDI task: Core 0 / priority 10
- Control/UI task: Core 0 / priority 3
- Fixed 64-entry MIDI->Audio command ring for Note On/Off, Pitch Bend, Sustain and channel panic
- Fixed 64-entry MIDI->Control queue for CC parameter changes
- Central version definition in src/PhoenixSystem/PhoenixVersion.h
- BANK_VERSION=15, MULTISAMPLE VERSION=10 (unchanged)
- C028e USB MSC eject/remount fix retained
- Bank save now reports metadata/config open failures correctly

See CHANGELOG_v0_8_04a_C029.txt and V0_8_04a_C029_TEST.txt.

Historical notes follow.

Project Phoenix v0.7.10
SFX Classic Reconstruction

Neu in v0.7.10:
- TRIGGER LEVEL im Sound-Sampling-Menue
- MANUAL/AUTO Aufnahme-Start
- einstellbarer Trigger-Level 1..8
- WAIT TRIG Anzeige bei bewaffneter Auto-Aufnahme

Basis: Project Phoenix v0.7.9 LOOP (SFX Classic).


v0.7.13b
- Added Audio Test Generator in Service Mode.
- Variable waveform, frequency and level.


v0.7.13c FIX4: Project Save/Load speichert jetzt neben WAV-Dateien auch BANK.CFG mit Loop-, Pitch-, Trigger-, Echo- und Replay-Status.


v0.7.13c FIX6: Persistent System Settings fuer MIDI Channel, MIDI Control und Trigger Level.

Current test target: v0.7.13c FIX6 - Navigation & Waveform UX.


v0.7.13c FIX8: MIDI Note-On respects Sample Editor START/END range; Note-Off remains gate stop.


C027 Stability Diagnostics
- Main menu F2 opens live diagnostics.
- F1 resets counters, F7 prints a serial snapshot, F8 returns.


C027a 12-Voice Stability Baseline
- Shared physical voice pool and selectable POLY limit are capped at 12.
- Older BANK.CFG values above 12 are clamped safely while loading.
- INITIALIZING was removed from the boot screen.

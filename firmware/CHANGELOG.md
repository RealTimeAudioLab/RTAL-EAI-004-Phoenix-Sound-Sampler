# Changelog

## v1.0.12a — Robust Last Bank Restore

- Added redundant last-bank persistence using NVS plus `/PHOENIX/LASTBANK.CFG`.
- Added NVS write/read-back verification.
- Added SD fallback during boot restore.
- Hardened encoder-button service-mode detection.
- Improved visible boot diagnostics for restore success/failure.
- Hardware restore test confirmed successful.

## v1.0.12 — Active Bank Persistence & Encoder Bank Manager

- Separated the actually loaded bank from the Disk Utilities selection cursor.
- Added encoder-driven LOAD / SAVE / DELETE bank selection.
- A browsed bank number no longer changes the persistent active bank.

## v1.0.11 — Last Session Persistence Fix

- Added active session persistence groundwork and periodic session-state saving.

## v1.0.10 — Realtime Multisample Pitch Editing

- Held multisample voices respond in realtime to transpose, fine tuning, root-note and keytrack changes.
- Active samples continue from their current playback position; no retrigger is required.

## v1.0.9 — Non-Blocking Multisample Editing

- Multisample edits are applied in RAM immediately.
- SD configuration writes are deferred instead of occurring on every encoder step.
- Sequencer timing remains stable during live parameter editing.

## v1.0.8 — Multisample Load UX

- Added progress feedback when assigning/loading multisample WAV files.
- Corrected boot-version text layout on the 128×64 display.

## v1.0.7 — Boot Session Transparency

- Added visible session-restore and bank-load boot messages.

## v1.0.6 — WAV Browser Cache

- Cached WAV directory listing.
- WAV metadata is read lazily for the selected file instead of scanning every WAV header on entry.

## v1.0.5 — Boot UX & Bank Scan Optimization

- Added staged boot feedback.
- Reduced unnecessary fixed splash delay.
- Optimized bank discovery by inspecting opened bank directories.

## v1.0.4 — WAV Import WDT Fix

- Added a real FreeRTOS yield during long WAV import/conversion loops to prevent Core-0 task-watchdog resets.

## v1.0.3 — Non-Blocking Disk Utilities / Bank Cache

- Bank occupancy is cached in RAM.
- Disk Utilities bank browsing no longer performs repeated SD existence checks.

## v1.0.2 — Bank Load Stability

- Robust cooperative scheduling during bank loading.

## v1.0.0 — Final 1.0 baseline

- 12-voice Phoenix sample engine.
- Four-slot Quattro workflow.
- Sampling, sample editing, multisampling, sequencing, MIDI and SD-bank architecture established as the 1.0 baseline.

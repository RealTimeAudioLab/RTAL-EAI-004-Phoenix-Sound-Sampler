# v1.1.0 FINAL Validation Record

Final source baseline: `Phoenix_v1_1_0_DEV3b_REVERB_TIMING_LIGHT`.

Hardware-validated DEV3b reference:

- ESP32-S3 @ 240 MHz
- 32 kHz / 128 frames / 4000 us deadline
- 12 voices
- Echo 45 / Reverb 49 / Size 90 / Decay 91 / Damp 45
- peak observed: 3744 us
- typical 12-voice load: approximately 2750-2810 us
- overrun=0
- underruns=0
- midi_cmd_drop=0
- midi_ctl_drop=0
- seq_cmd_drop=0
- sched_miss=0
- Reverb memory: 13,860 bytes INTERNAL
- Session A/B journal restored existing sequence successfully

Release promotion policy:

- no DSP formula changes
- no voice allocator changes
- no Echo/Reverb topology changes
- no SD session record-format changes
- version/log/documentation cleanup only

A final compile on the target Arduino IDE installation is recommended before tagging the GitHub release, because the packaging environment does not contain the Arduino 1.8.19 / ESP32 Core 2.0.16 compiler toolchain.

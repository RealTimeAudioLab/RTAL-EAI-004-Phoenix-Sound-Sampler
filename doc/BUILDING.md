# Building Project Phoenix v1.0.3

## Reference environment

- Arduino IDE 1.8.19
- Arduino-ESP32 Core 2.0.16
- Board: ESP32-S3 Dev Module
- CPU: 240 MHz
- PSRAM enabled for the target hardware
- U8g2 library installed

## Sketch

Open:

`firmware/Phoenix_v1_0_3_FINAL_CLEAN/Phoenix_v1_0_3_FINAL_CLEAN.ino`

The `src/` directory must remain beside the sketch exactly as provided.

## Runtime architecture

- Audio Task: Core 1, priority 24
- MIDI Task: Core 0, priority 10
- SeqRT Task: Core 0, priority 8
- Control Task: Core 0, priority 3

Audio: 32 kHz, 128 frames, 4 ms block deadline, 12-voice global pool.

## Important

This repository packages the hardware-confirmed v1.0.3 source. No precompiled binary is supplied. Compile and flash with the reference Arduino environment or an equivalent compatible setup.

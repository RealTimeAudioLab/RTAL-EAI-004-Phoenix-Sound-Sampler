# Project Phoenix v1.0.12a

**ESP32-S3 polyphonic hardware sampler, multisample instrument and 4-track sequencer**  
**Release:** v1.0.12a — Robust Last Bank Restore

Project Phoenix is a standalone sample-based instrument built around an ESP32-S3, PCM1808 ADC, PCM5102A DAC, SD card storage, DIN MIDI and a 128×64 OLED. The firmware combines sampling, multisample playback, a shared 12-voice engine, Quattro four-slot operation, sequencing, sample editing and persistent banks in one embedded platform.

This release is the hardware-tested maintenance build based on v1.0.12. Its main change is a robust restore path for the last active bank after reset or power-up.

## Highlights

- Shared **12-voice** sample engine
- Four sample slots with **POLY / MONO / LEGATO** operation
- Multisample keygroups with velocity layers and round-robin variants
- Realtime multisample pitch editing for held notes
- Four-track / four-pattern / 16-step sequencer with song chaining
- DIN MIDI note input and continuous MIDI clock handling
- Internal audio rate **32 kHz**
- PCM1808 ADC + PCM5102A DAC over I2S
- SD-card bank system under `/PHOENIX`
- Sample editor with Start / Loop Start / Loop End / Sample End markers
- Forward and alternate sustain-loop modes
- Echo effect
- Encoder-driven Disk Utilities bank manager
- Robust last-bank restore using NVS plus SD fallback
- Optimized non-blocking WAV import and bank browsing

## What is new in v1.0.12a

The last active bank is now restored reliably after restart.

Phoenix stores the active bank in two places:

1. ESP32 NVS / `Preferences`
2. `/PHOENIX/LASTBANK.CFG` on the SD card

At boot Phoenix first checks NVS. If that entry is missing or invalid, it falls back to the SD-card copy. A successful restore synchronizes both persistence stores again.

The service-mode encoder-button detection was also hardened so that a short or unstable LOW state on GPIO42 no longer unintentionally suppresses the normal bank restore.

## Encoder Bank Manager

Disk Utilities are operated entirely with the rotary encoder:

```text
DISK UTILITIES
  ↓ press encoder
LOAD BANK / SAVE BANK / DELETE BANK
  ↓ press encoder
BANK 01 ... BANK 99
  ↕ turn encoder
  ↓ press encoder
execute selected operation
```

`F8` remains **BACK / CANCEL**.

Browsing bank numbers does **not** change the active bank. Only a successful LOAD or SAVE updates the persistent active-bank state.

## Hardware baseline

| Function | Hardware / Pin |
|---|---|
| MCU | ESP32-S3 Dev Module, 240 MHz |
| PSRAM | 8 MB target configuration |
| Flash | 16 MB target configuration |
| ADC | PCM1808 |
| DAC | PCM5102A |
| OLED | SSD1309 128×64 SPI |
| OLED SCK / MOSI / CS / DC / RST | 12 / 11 / 10 / 6 / 7 |
| SD SCK / MOSI / MISO / CS | 12 / 11 / 13 / 9 |
| I2S BCLK / LRCK / DOUT / DIN | 18 / 16 / 17 / 5 |
| MIDI DIN RX / TX | 40 / 39 |
| Encoder A / B / SW | 1 / 2 / 42 |
| F1..F8 | 21, 47, 45, 38, 4, 15, 3, 14 |

See [docs/HARDWARE.md](docs/HARDWARE.md) for the complete development baseline.

## Toolchain

The release baseline is:

- Arduino IDE **1.8.19**
- Arduino-ESP32 Core **2.0.16**
- Board: **ESP32-S3 Dev Module**
- U8g2 display library
- USB MSC setting: **Tools → USB Mode → USB-OTG (TinyUSB)**

## Build

Open:

```text
firmware/Phoenix_v1_0_12a_ROBUST_LAST_BANK_RESTORE/
Phoenix_v1_0_12a_ROBUST_LAST_BANK_RESTORE.ino
```

The sketch directory and `.ino` filename intentionally match, so the project can be opened directly in the Arduino IDE.

## SD card layout

Phoenix uses `/PHOENIX` as its root directory. Typical structure:

```text
/PHOENIX/
├── BANKS/
│   ├── BANK01/
│   ├── BANK02/
│   └── ...
├── WAV/
├── EXPORT/
├── CONFIG.TXT
└── LASTBANK.CFG
```

Bank format compatibility for this release:

```text
BANK_VERSION        = 15
MULTISAMPLE_VERSION = 10
```

## Audio architecture

Phoenix runs its playback engine internally at **32 kHz**. Imported WAV files are converted to the internal playback rate during import. The shared physical voice pool is limited to **12 voices** and is used globally across the four slots.

Realtime audio work is isolated from SD-card maintenance as far as possible. Recent v1.0.x maintenance releases moved expensive bank browsing, WAV metadata parsing and multisample configuration writes out of timing-critical interaction paths.

## v1.0.x maintenance milestones

- **v1.0.2** — robust cooperative bank loading / watchdog stability
- **v1.0.3** — RAM-based bank cache for Disk Utilities
- **v1.0.4** — WAV import watchdog fix
- **v1.0.5** — boot UX and bank-scan optimization
- **v1.0.6** — cached WAV browser with lazy metadata reads
- **v1.0.7** — visible boot/session restore status
- **v1.0.8** — multisample sample-load progress and boot-layout fix
- **v1.0.9** — non-blocking multisample parameter editing
- **v1.0.10** — realtime pitch update of active multisample voices
- **v1.0.11** — session persistence groundwork
- **v1.0.12** — active-bank separation and encoder bank manager
- **v1.0.12a** — robust last-bank restore with NVS + SD fallback

See [CHANGELOG.md](CHANGELOG.md) and [RELEASE_NOTES.md](RELEASE_NOTES.md).

## License

Project Phoenix is released under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

---

**RealTimeAudioLab / RTAL**  
Embedded realtime audio development on ESP32-S3.

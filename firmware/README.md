# Project Phoenix v1.1.0

**ESP32-S3 polyphonic hardware sampler, multisample instrument and 4-track sequencer**  
**Release:** v1.1.0 FINAL — Stereo Reverb, SD A/B Session Store & Performance Baseline

Project Phoenix is a standalone sample-based instrument built around an ESP32-S3, PCM1808 ADC, PCM5102A DAC, SD-card storage, DIN MIDI and a 128×64 OLED. The firmware combines sampling, multisample playback, a shared 12-voice engine, Quattro four-slot operation, sequencing, sample editing, stereo effects and persistent banks in one embedded platform.

v1.1.0 FINAL is the current frozen stable firmware release. It builds on the proven v1.0.x maintenance line and adds the new stereo Reverb, BANK16 persistence, a robust SD A/B session journal and further realtime-performance optimization.

## Highlights

- Shared **12-voice** sample engine
- Four sample slots with **POLY / MONO / LEGATO** operation
- Multisample keygroups with velocity layers and round-robin variants
- Realtime multisample pitch editing for held notes
- Four-track / four-pattern / 16-step sequencer with song chaining
- DIN MIDI note input and continuous MIDI clock handling
- Internal audio rate **32 kHz**
- Audio block size **128 frames / 4.0 ms deadline**
- PCM1808 ADC + PCM5102A DAC over I2S
- SD-card bank system under `/PHOENIX`
- Sample editor with Start / Loop Start / Loop End / Sample End markers
- Forward and alternate sustain-loop modes
- Smoothed Echo effect
- **Stereo Schroeder/Moorer Reverb**
- **Reverb FAST-CACHE**
- **13,860-byte Reverb delay memory in internal SRAM**
- Encoder-driven Disk Utilities bank manager
- **SD A/B session persistence with CRC32 and sequence recovery**
- `/PHOENIX/LASTBANK.CFG` retained as legacy bank fallback
- Optimized non-blocking WAV import and bank browsing
- Targeted **-O3 + fast-math** audio optimization
- Hardware-validated **12-voice** realtime baseline

## What is new in v1.1.0

### Stereo Reverb

Phoenix v1.1.0 adds a compact integer stereo Reverb designed specifically for the ESP32-S3 realtime budget.

The Reverb includes:

- four damped feedback comb filters
- two decorrelating allpass stages per side
- **SIZE**
- **DECAY**
- **DAMP**
- **MIX**
- independent Reverb send for sample slots **S1–S4**
- BANK16 persistence

A block-rate **Reverb FAST-CACHE** precomputes invariant delay lengths, feedback values and damping parameters once per 128-frame block instead of rebuilding them for every sample.

The complete Reverb delay memory uses only:

```text
6,930 × int16_t = 13,860 bytes
```

For v1.1.0 FINAL this memory is allocated in **internal SRAM**, with a safe fallback path if internal allocation is not possible.

This placement produced a measurable reduction in high-load audio latency compared with the earlier PSRAM placement.

### Echo memory strategy

The complete Echo delay ring remains in **PSRAM**.

The Echo buffer is:

```text
64,000 × int16_t = 128,000 bytes
```

During v1.1.0 development, moving the complete Echo ring into internal SRAM was tested. The experiment consumed a large part of the internal heap but did not provide a useful realtime-performance advantage, so the final v1.1.0 architecture keeps:

```text
Reverb delay memory  → internal SRAM
Echo delay memory    → PSRAM
```

### SD A/B session persistence

The v1.0.x session implementation used NVS plus `/PHOENIX/LASTBANK.CFG`.

During v1.1.0 development the target ESP32-S3 was found to contain a full NVS partition belonging to another namespace. Instead of erasing or modifying unrelated NVS data, Phoenix session persistence was redesigned around the SD card.

Phoenix v1.1.0 stores the active:

- bank
- screen
- slot

in two alternating session files:

```text
/PHOENIX/SESSION_A.BIN
/PHOENIX/SESSION_B.BIN
```

Each fixed-size 20-byte record contains:

- magic / format signature
- format version
- record size
- 32-bit sequence number
- active bank
- active screen
- active slot
- CRC32

Writes alternate between A and B. Every write is flushed, closed and re-read for verification.

At boot Phoenix validates both records and selects the newest valid sequence. If one record is incomplete or corrupt, the other remains available as a rollback source.

`/PHOENIX/LASTBANK.CFG` is retained as a legacy bank-only fallback and migration source.

> Phoenix **session persistence** no longer depends on NVS. Optional independent subsystems such as MIDI preferences may still use their own Preferences/NVS namespaces.

### BANK16

The current persistent bank format is:

```text
BANK_VERSION        = 16
MULTISAMPLE_VERSION = 10
```

BANK16 adds the v1.1.0 Reverb-related persistence while retaining the established Phoenix bank architecture.

### NEW BANK defaults

NEW BANK can start from:

- compiled **Factory Defaults**
- optional `/PHOENIX/INIT_SOUND.CFG`

The INIT template is intended for sound/mapping defaults only. Sample paths and current pattern/song content are not treated as part of the reusable init template.

## Encoder Bank Manager

Disk Utilities continue to use the rotary encoder:

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

Browsing bank numbers does **not** change the active bank. Only a successful LOAD or SAVE updates the active-bank state.

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
| PCM1808 MCLK | 0 |
| MIDI DIN RX / TX | 40 / 39 |
| Encoder A / B / SW | 1 / 2 / 42 |
| F1..F8 | 21, 47, 45, 38, 4, 15, 3, 14 |

See [docs/HARDWARE.md](docs/HARDWARE.md) for the complete development baseline.

## Toolchain

The v1.1.0 FINAL release baseline is:

- Arduino IDE **1.8.19**
- Arduino-ESP32 Core **2.0.16**
- Board: **ESP32-S3 Dev Module**
- CPU: **240 MHz**
- PSRAM: **8 MB**
- Flash: **16 MB**
- U8g2 display library
- USB MSC setting: **Tools → USB Mode → USB-OTG (TinyUSB)**

The realtime audio translation unit uses targeted **-O3 + fast-math** optimization.

Temporary per-frame Reverb cycle profiling used during development is compiled out by default in the FINAL audio path.

## Build

Open:

```text
firmware/Phoenix_v1_1_0_FINAL/
Phoenix_v1_1_0_FINAL.ino
```

The sketch directory and `.ino` filename intentionally match, so the project can be opened directly in Arduino IDE 1.8.19.

## SD card layout

Phoenix uses `/PHOENIX` as its root directory.

Typical v1.1.0 structure:

```text
/PHOENIX/
├── SESSION_A.BIN
├── SESSION_B.BIN
├── LASTBANK.CFG
├── INIT_SOUND.CFG        (optional)
├── CONFIG.TXT
├── BANKS/
│   ├── BANK01/
│   ├── BANK02/
│   └── ...
├── WAV/
└── EXPORT/
```

Typical bank contents remain:

```text
BANK.CFG
BANK.INFO
SLOT1.WAV
SLOT2.WAV
SLOT3.WAV
SLOT4.WAV
MULTISAMPLE.CFG
PATTERNS.CFG
SONG.CFG
```

Bank format compatibility for v1.1.0:

```text
BANK_VERSION        = 16
MULTISAMPLE_VERSION = 10
```

## Audio architecture

Phoenix runs its playback engine internally at **32 kHz**. Imported WAV files are converted to the internal playback rate during import.

The shared physical voice pool is limited to **12 voices** and is used globally across the four sample slots and multisample regions.

Realtime audio is isolated from SD-card, MIDI, sequencing and UI maintenance through the ESP32-S3 task architecture:

```text
CORE 1
└── AudioTask      Priority 24
    ├── I2S input
    ├── voice rendering
    ├── scheduled events
    ├── filter / Echo / Reverb
    └── I2S output

CORE 0
├── MidiTask       Priority 10
├── SeqRT          Priority 8
└── ControlTask    Priority 3
    ├── UI
    ├── encoder / buttons
    ├── SD / bank management
    └── system control
```

Audio block:

```text
Sample rate : 32,000 Hz
Block size  : 128 frames
Deadline    : 4,000 us
Voices      : 12
```

## v1.1.0 performance reference

The v1.1.0 FINAL source is promoted directly from the hardware-validated:

```text
Phoenix_v1_1_0_DEV3b_REVERB_TIMING_LIGHT
```

The FINAL promotion changes release/version strings and documentation only; the validated DSP architecture is retained.

Reference stress configuration:

```text
ESP32-S3       240 MHz
Audio          32 kHz / 128 frames
Deadline       4000 us
Voices         12
Echo           45
Reverb         49
Size           90
Decay          91
Damp           45
```

Observed on the target hardware:

```text
Peak block time        3744 us
Deadline utilization   93.6 %
Remaining headroom      256 us

Typical 12-voice load  ~2750–2810 us

Audio overruns          0
Audio underruns         0
MIDI command drops      0
MIDI control drops      0
Sequencer command drops 0
Scheduling misses       0
```

These are measured reference values from the validated test configuration and should not be interpreted as a guaranteed worst-case limit for every possible bank, SD card or workload.

## Session validation

The new SD A/B session store was tested across repeated writes and power cycles.

Observed sequence:

```text
A seq=1
B seq=2
A seq=3
B seq=4
A seq=5

POWER CYCLE

A=OK
B=OK
selected=A seq=5

B seq=6
A seq=7
B seq=8
A seq=9
```

All tested writes completed with verification enabled.

This confirmed:

- alternating A/B journal writes
- persistent sequence numbering
- CRC validation
- newest-valid-record selection
- session recovery after restart
- bank/screen/slot persistence
- compatibility between the validated DEV builds and v1.1.0 FINAL

## Realtime diagnostics

Runtime diagnostics expose audio, MIDI, sequencer, multisample and storage health counters.

The FINAL build retains the high-water:

```text
PHX PEAKTRACE
```

diagnostic for unusually expensive audio blocks.

This makes realtime problems measurable instead of relying only on subjective listening tests.

## v1.0.x maintenance milestones

The v1.1.0 release includes all previously established v1.0.x maintenance work:

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

## v1.1.0 development milestones

The v1.1.0 development branch concentrated on performance, Reverb and robust session persistence.

Key milestones:

- **Reverb FAST-CACHE** — block-rate cached Reverb invariants
- **O3 + fast-math** — validated audio translation-unit optimization
- **SD A/B Session Store** — removed PhoenixOS session dependency on a full NVS partition
- **DEV3 stable baseline** — consolidated diagnostics and release architecture
- **DEV3a** — moved the 13,860-byte Reverb delay memory to internal SRAM
- **DEV3b** — removed temporary per-frame Reverb timing overhead from the default build
- **DEV3c experiment** — tested full 128 kB Echo ring in internal SRAM; rejected because it consumed too much internal memory without a useful performance gain
- **v1.1.0 FINAL** — promoted from the hardware-validated DEV3b architecture

See [CHANGELOG.md](CHANGELOG.md) and [RELEASE_NOTES.md](RELEASE_NOTES.md).

## Compatibility notes

v1.1.0 keeps the established Phoenix hardware and toolchain baseline.

Important persistent-format changes:

```text
v1.0.x  BANK_VERSION = 15
v1.1.0  BANK_VERSION = 16

MULTISAMPLE_VERSION   = 10
```

The v1.1.0 session store uses SD A/B records rather than the earlier PhoenixOS NVS session path.

`LASTBANK.CFG` is intentionally retained for fallback/migration behavior.

## Release status

**Phoenix v1.1.0 FINAL** is the current frozen firmware baseline.

The release is based on the hardware-tested DEV3b source architecture:

```text
12 voices
32 kHz / 128 frames
Reverb FAST-CACHE
Reverb delay memory in internal SRAM
Echo delay memory in PSRAM
SD A/B session journal
BANK16
-O3 + fast-math
Reverb timing profiling OFF
```

Future Phoenix development should branch from the archived v1.1.0 FINAL release rather than modifying the release source in place.

## License

Project Phoenix is released under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

---

**RealTimeAudioLab / RTAL**  
Embedded realtime audio development on ESP32-S3.

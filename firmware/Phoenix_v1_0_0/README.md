<div align="center">

# 🔥 Project Phoenix Firmware

### ESP32-S3 Realtime Sampler Firmware  
**12 Voices · 32 kHz Audio · Multisampling · MIDI · Realtime Sequencer · SD Storage · USB MSC**

**Firmware Version 1.0.0 FINAL**

![ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3-000000?style=for-the-badge&logo=espressif)
![Audio](https://img.shields.io/badge/Audio-32_kHz_%7C_128_Frames-6f42c1?style=for-the-badge)
![Voices](https://img.shields.io/badge/Polyphony-12_Voices-success?style=for-the-badge)
![Release](https://img.shields.io/badge/Release-v1.0.0_FINAL-brightgreen?style=for-the-badge)

**RealTimeAudioLab / RTAL**

</div>

---

# Firmware overview

This repository contains the embedded firmware for **Project Phoenix**, a polyphonic hardware sampler built around the **ESP32-S3**.

Phoenix v1.0.0 FINAL is based on the hardware-validated v0.9.0 RC1 release candidate. Audio, MIDI, sequencer, storage, multisample, USB, filter and echo behavior were frozen for the final release.

The firmware is designed around one central principle:

> **Realtime audio must remain deterministic even while MIDI, sequencing, display updates and SD-card operations are active.**

Phoenix therefore uses a deliberately separated multicore task architecture with a dedicated audio task on Core 1 and MIDI, realtime sequencing and system control on Core 0.

---

# Core specifications

| Parameter | Phoenix v1.0.0 FINAL |
|---|---|
| MCU | ESP32-S3 @ 240 MHz |
| PSRAM | 8 MB |
| Flash | 16 MB |
| Audio sample rate | **32,000 Hz** |
| Audio block size | **128 frames** |
| Audio block deadline | **4.0 ms** |
| Global polyphony | **12 voices** |
| ADC | PCM1808 |
| DAC | PCM5102A |
| Basic sample slots | 4 |
| Sequencer | 4 patterns × 4 tracks × up to 16 steps |
| External clock | MIDI Clock, 24 PPQN |
| Transport | FA Start / FC Stop |
| Storage | SD card |
| PC access | USB Mass Storage |
| Bank format | **15** |
| Multisample format | **10** |
| Arduino IDE | **1.8.19** |
| Arduino-ESP32 Core | **2.0.16** |

---

# Firmware architecture

Phoenix separates time-critical and non-time-critical work.

```text
ESP32-S3 @ 240 MHz

CORE 1
└── AudioTask             Priority 24
    ├── I2S input
    ├── voice rendering
    ├── sample playback
    ├── envelopes
    ├── pitch
    ├── filter
    ├── echo / DSP
    ├── scheduled sequencer events
    └── I2S output

CORE 0
├── MidiTask              Priority 10
│   ├── DIN MIDI parser
│   ├── Note On / Off
│   ├── CC
│   ├── Pitch Bend
│   ├── Sustain
│   └── F8 / FA / FB / FC
│
├── SeqRT                 Priority 8
│   ├── timestamped external MIDI clock
│   ├── 24 PPQN phase tracking
│   ├── predicted step timing
│   └── sample-accurate event scheduling
│
└── ControlTask           Priority 3
    ├── UI
    ├── encoder
    ├── buttons
    ├── bank management
    ├── SD-card operations
    └── system control
```

The Arduino `loop()` is intentionally not used as the central realtime scheduler.

---

# Audio engine

Phoenix runs at:

```text
Sample rate : 32000 Hz
Block size  : 128 frames
Block time  : 4.000 ms
Voices      : 12
```

The audio engine uses one global 12-voice pool shared by all four sample slots and multisample regions.

Each voice maintains its own:

- sample source
- playback position
- pitch increment
- ADSR state
- sample start/end boundaries
- loop start/end boundaries
- loop mode
- loop direction
- key state
- sustain state
- ownership token
- filter state
- realtime sequencer start offset

Voice state is mutated from the audio side rather than from unrelated control tasks.

---

# Voice ownership and MIDI robustness

A central stability improvement in Phoenix is the separation between incoming MIDI/control events and the voice structures rendered by the audio task.

The firmware uses queued event transfer rather than allowing the MIDI task to manipulate active voice data directly.

The voice system includes:

- fixed 12-voice pool
- duplicate Note-On handling
- one matching Note-Off per held note
- Sustain Pedal handling
- protected voice stealing
- ownership tokens
- panic handling
- orphan Note-Off diagnostics
- duplicate-note diagnostics

This architecture prevents stale Note-Off or sequencer gate events from accidentally terminating a voice that has already been reassigned.

---

# Realtime sequencer architecture

External-clock sequencing is handled separately from normal UI/control processing.

```text
MIDI DIN
   │
   │ F8 / FA / FC
   ▼
MidiTask P10
   │
   │ timestamped clock events
   ▼
SeqRT P8
   │
   │ future scheduled events
   ▼
Audio command queue
   │
   ▼
AudioTask P24
   │
   └── exact event position inside 128-frame audio block
```

## MIDI clock

Phoenix expects standard MIDI Clock:

```text
24 F8 clocks = 1 quarter note
6 F8 clocks  = 1 sixteenth note
```

Transport:

```text
FA = START
FB = CONTINUE
FC = STOP
F8 = CLOCK
```

Phoenix explicitly supports a setup where **F8 continues running even while transport is stopped**.

Stopped clocks are not played back later as a backlog.

## Sample-accurate scheduling

The dedicated SeqRT task predicts the relevant upcoming clock boundary and schedules note events into the audio engine.

The AudioTask can then place the note onset at an offset inside the current 128-frame block instead of waiting for the next complete block.

This keeps sequencer timing independent from slow display refreshes or SD-card/control work.

---

# Pattern sequencer

Phoenix provides:

- 4 patterns
- 4 tracks per pattern
- 1–16 steps per track
- note per step
- velocity per step
- gate length per step
- independent track lengths
- internal clock
- external MIDI clock
- pattern management
- song chaining

Conceptually a step contains:

```text
ACTIVE, NOTE, VELOCITY, GATE
```

Example:

```text
T1_STEP1=1,60,110,50
```

Meaning:

```text
Track      = 1
Step       = 1
Active     = yes
MIDI note  = 60
Velocity   = 110
Gate       = 50 %
```

---

# Sample playback and looping

Phoenix uses four marker positions:

```text
S.START ---- L.START ================= L.END ---- S.END
               <---- LOOP AREA ---->
```

Playback semantics:

### Loop OFF

```text
S.START -----------------------------> S.END
```

### FORWARD

```text
S.START ----> L.START ---> L.END
                 ^           |
                 |___________|
```

### ALTERNATE

```text
S.START ----> L.START <----> L.END
```

`S.END` and `L.END` are handled as exclusive boundaries in the final playback model.

When a held loop is released, the voice can leave the sustain loop and continue toward `S.END` according to the configured envelope/playback behavior.

---

# Multisampling

Phoenix supports a multisample system beyond the four main sample slots.

Features include:

- keygroups
- key ranges
- velocity layers
- round robin
- root-note mapping
- one-shot playback
- forward loops
- alternate loops
- per-region WAV assignment

The v1.0 multisample format version is:

```text
MULTISAMPLE_VERSION = 10
```

The loader validates mappings and rejects unsupported future format versions.

---

# Quattro modes

The four primary slots can be used in two performance concepts:

## KEYZONE

Each slot responds to a defined keyboard range.

```text
Low                                        High
│---- SLOT 1 ----│
        │---- SLOT 2 ----│
                 │---- SLOT 3 ----│
                          │---- SLOT 4 ----│
```

## MULTI

Several slots can respond simultaneously with their own:

- MIDI assignment
- level
- panorama
- key range
- processing
- sample parameters

---

# DSP

Phoenix includes lightweight DSP designed around the ESP32-S3 realtime budget.

The signal path includes:

```text
Sample Voice
    │
    ▼
Envelope / Pitch
    │
    ▼
Filter
    │
    ▼
Voice Mix
    │
    ├── Echo Send
    │
    ▼
Master Processing
    │
    ▼
PCM5102A
```

The final firmware includes:

- realtime filter
- echo/delay
- smoothed delay parameters
- delay-time crossfade
- output limiting
- vintage sampler processing
- per-voice / per-part processing support

The audio render code avoids unnecessary expensive operations in the hot path.

---

# Transactional bank storage

Phoenix bank saving was designed to avoid replacing a working bank with a partially written one.

The storage subsystem uses temporary and backup files during commit.

Conceptually:

```text
Existing Bank
     │
     ├── create temporary data
     │
     ├── flush / validate
     │
     ├── preserve previous state
     │
     └── commit new bank
```

The system also contains recovery logic for interrupted transactions.

Bank I/O cooperatively yields while processing larger data blocks so long SD operations do not starve the ESP32-S3 system watchdog.

---

# Bank format

Typical bank structure:

```text
/PHOENIX/BANKS/BANK01/
│
├── BANK.CFG
├── BANK.INFO
├── SLOT1.WAV
├── SLOT2.WAV
├── SLOT3.WAV
├── SLOT4.WAV
├── MULTISAMPLE.CFG
├── PATTERNS.CFG
└── SONG.CFG
```

Current firmware formats:

```text
BANK_VERSION        15
MULTISAMPLE_VERSION 10
```

A bank does not have to contain all four basic slot WAV files.

---

# WAV handling

Phoenix works internally with sample data prepared for its 32 kHz engine.

The import system supports PCM WAV input and converts supported source material into the Phoenix target format.

The typical internal target is:

```text
PCM
16 bit
Mono
32000 Hz
```

Imported samples can then be edited, looped, mapped and stored as part of a Phoenix bank.

---

# USB Mass Storage

Phoenix can expose its storage to a PC through native ESP32-S3 USB.

This allows the same bank files to be edited by **Phoenix Librarian** or by normal PC file tools.

Safe workflow:

```text
1. Stop playback / sequencer
2. Enter USB Mass Storage mode
3. Wait for the Phoenix drive in Windows
4. Read or modify files
5. Finish all writes
6. Safely eject the Phoenix volume in Windows
7. Leave USB storage mode on Phoenix
8. Reload the bank
```

Do not disconnect the storage device during an active write operation.

---

# Phoenix Librarian compatibility

Phoenix Firmware v1.0.0 FINAL is designed to work with **Phoenix Librarian v1.0.0**.

The Librarian operates on the same bank files used by the firmware:

```text
BANK.CFG
BANK.INFO
SLOT*.WAV
MULTISAMPLE.CFG
PATTERNS.CFG
SONG.CFG
```

This keeps the PC editor and hardware firmware based on one common data model.

---

# Hardware pinout

## Audio

| Signal | ESP32-S3 GPIO | Destination |
|---|---:|---|
| PCM1808 MCLK / SCKI | **0** | PCM1808 |
| I2S BCLK | **18** | PCM1808 + PCM5102A |
| I2S LRCK / WS | **16** | PCM1808 + PCM5102A |
| I2S DOUT | **17** | PCM5102A DIN |
| I2S DIN | **5** | PCM1808 DOUT |

```text
ESP32-S3                       PCM1808
GPIO0  MCLK  ----------------> MCLK/SCKI
GPIO18 BCLK  ----------------> BCK
GPIO16 LRCK  ----------------> LRCK
GPIO5  DIN   <---------------- DOUT

ESP32-S3                       PCM5102A
GPIO18 BCLK  ----------------> BCK
GPIO16 LRCK  ----------------> LRCK
GPIO17 DOUT  ----------------> DIN
```

## OLED

```text
SCK   GPIO12
MOSI  GPIO11
CS    GPIO10
DC    GPIO6
RST   GPIO7
```

## SD card

```text
SCK   GPIO12
MOSI  GPIO11
MISO  GPIO13
CS    GPIO9
```

OLED and SD share SCK and MOSI.

## MIDI

```text
RX    GPIO40
TX    GPIO39
Baud  31250
```

## Buttons

```text
F1    GPIO21
F2    GPIO47
F3    GPIO45
F4    GPIO38
F5    GPIO4
F6    GPIO15
F7    GPIO3
F8    GPIO14
```

## Encoder

```text
A     GPIO1
B     GPIO2
SW    GPIO42
```

## Native USB

```text
USB D-   GPIO19
USB D+   GPIO20
```

All GPIO signals use **3.3 V logic**.

---

# Build environment

The release firmware targets:

```text
Arduino IDE        1.8.19
ESP32 Arduino Core 2.0.16
ESP32-S3           240 MHz
PSRAM              8 MB
Flash              16 MB
```

Recommended approach:

1. Install Arduino IDE 1.8.19.
2. Install ESP32 Arduino Core 2.0.16.
3. Select the appropriate ESP32-S3 board.
4. Enable the correct PSRAM / flash configuration for the board.
5. Open the final Phoenix sketch.
6. Compile.
7. Upload.
8. Open Serial Monitor for boot diagnostics.

Do not assume later Arduino-ESP32 core releases are source-compatible without regression testing. Phoenix v1.0.0 FINAL was validated against the release environment above.

---

# Source structure

The firmware is organized into functional modules rather than one monolithic sketch.

A typical source tree looks conceptually like:

```text
Phoenix/
│
├── Phoenix.ino
│
└── src/
    ├── PhoenixAudio/
    │   └── audio engine / voices / I2S
    │
    ├── PhoenixOS/
    │   └── system control / banks / sequencer
    │
    ├── PhoenixSystem/
    │   └── version / diagnostics / common state
    │
    └── ...
```

The exact folder set may vary with the packaged release, but the architectural separation between Audio, OS/Control and System code is intentional.

---

# Diagnostics

Phoenix contains runtime diagnostics for the major realtime paths.

Important counters include:

```text
risk
overrun
underruns

midi_cmd_drop
midi_ctl_drop

tick_drop
clk_qdrop
seq_cmd_drop
note_on_fail
owner_fail

load_fail
save_fail
```

For a healthy system under normal validated operation, the critical error/drop counters should remain at zero.

Some counters are informational and may legitimately increase depending on operation, for example stopped external clocks or naturally expired sequencer-owned voices.

---

# Service mode

Hold the encoder switch while powering on Phoenix to enter the service/self-test section.

Available functions include:

- Audio Output Test
- Audio Input Test
- Button Test
- System Diagnostics
- Start Sampler

This provides a quick way to verify the hardware before debugging higher-level firmware behavior.

---

# Release lineage

Phoenix v1.0.0 FINAL contains the accumulated stability work from the v0.8.x development series.

Important milestones included:

```text
C029   Cross-core MIDI / audio ownership separation
C029d  Audio hot-path block caching
C029e  Stable 12-voice performance baseline

C030   Transactional bank storage
C031   Voice / MIDI ownership robustness
C032   Sample and loop boundary accuracy
C033   Pattern / song / transport robustness

C034   Effects and DSP hardening
C034a  Smooth delay-time crossfade
C034b  MIDI clock backlog semantics

C035   Multisample robustness

C036b  Cooperative bank-save watchdog fix
C036c  Sequencer ownership / timestamp architecture
C036d  Dedicated realtime sequencer and intra-block scheduling

v0.9.0 RC1
        Release candidate / regression validation

v1.0.0 FINAL
        Frozen first stable release
```

---

# Release policy

**Phoenix v1.0.0 FINAL should be treated as a frozen reference release.**

Bug fixes or new features should be developed on a later branch/version rather than modifying the archived v1.0.0 source directly.

This makes the final release useful as:

- a known-good hardware reference
- a regression baseline
- a recovery build
- a documentation baseline
- a starting point for future branches

---

# Related project

Phoenix is accompanied by:

### Phoenix Librarian v1.0.0

Windows bank manager and PC editor for:

- sample editing
- loop editing
- bank management
- multisample preparation
- pattern editing
- song editing
- metadata
- preview
- backup / restore
- validation

---

# Documentation

Recommended firmware repository documentation:

```text
docs/
├── Phoenix_v1_0_0_Bedienungshandbuch_DE.pdf
├── HARDWARE_PINOUT.md
├── BANK_FORMAT.md
├── MULTISAMPLE_FORMAT.md
├── REALTIME_ARCHITECTURE.md
└── RELEASE_NOTES_v1.0.0.md
```

---

# License

Add the license selected for your Phoenix source code here.

For example:

```text
Copyright (c) RealTimeAudioLab / RTAL
See LICENSE for details.
```

---

<div align="center">

# PROJECT PHOENIX FIRMWARE

### 12 Voices. One ESP32-S3. Deterministic realtime audio.

**Sample · Sculpt · Map · Sequence · Perform**

Phoenix Firmware v1.0.0 FINAL  
RealTimeAudioLab / RTAL

</div>

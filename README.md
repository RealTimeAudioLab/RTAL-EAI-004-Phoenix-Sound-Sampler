# Project Phoenix Sound Sampler

# 🎹 The Return of the Hardware Sampler

### *An Open Engineering Project by Realtime Audio Lab (RTAL)*

*Inspired by the legendary Commodore 64 SFX Sound Sampler --- redesigned
for the modern embedded world.*

------------------------------------------------------------------------

> **Project Phoenix is not simply another sampler.**
>
> It is the complete documentation of designing, engineering and
> building a professional embedded musical instrument from scratch.

------------------------------------------------------------------------

# 📸 Hero Image

</p>

<p align="center">
  
<img src="images/SFX_Sound_Sampler.jpg" width="900">

*A vision of the final instrument.*

------------------------------------------------------------------------

# 📸 Current Development Prototype

</p>

<p align="center">
  
<img src="images/Phoenix_Development.jpg" width="900">

*The current ESP32-S3 based hardware platform used for firmware, DSP and
hardware development.*

------------------------------------------------------------------------

# Why Project Phoenix?

Modern hardware samplers are often closed systems.

Phoenix follows another philosophy:

-   Open Hardware
-   Open Firmware
-   Open Engineering
-   Open Documentation

Every important engineering decision will be documented.

Visitors are invited to follow the complete journey from the very first
prototype to Version 1.0.

------------------------------------------------------------------------

# Engineering Philosophy

Project Phoenix is built around five principles:

-   Engineering before marketing
-   Simplicity where possible
-   Professional audio quality
-   Educational value
-   Long-term maintainability

------------------------------------------------------------------------

# Current Development Status

| Module                  | Status |
|-------------------------|:------:|
| Audio Engine            | ✅ |
| Smart Sample Analysis   | ✅ |
| Vintage Sampler Engine  | ✅ |
| SD Card Browser         | ✅ |
| Multisamples            | ✅ |
| Velocity Layers         | ✅ |
| Round Robin             | ✅ |
| Smart Loop Engine       | 🚧 |
| Touch User Interface    | 🚧 |
| Complete Hardware       | 🚧 |


------------------------------------------------------------------------

# Planned Hardware

-   ESP32-S3
-   PCM1808 Audio ADC
-   PCM5102 DAC
-   SD Card
-   MIDI In / Out
-   OLED Display
-   Future Touch Display
-   Dedicated User Interface

------------------------------------------------------------------------

# Software Architecture

``` text
             MIDI
               │
               ▼
        User Interface
               │
               ▼
        Sample Manager
               │
     ┌─────────┴─────────┐
     ▼                   ▼
 Smart Analysis     Audio Engine
     ▼                   ▼
 Vintage DSP       Effects Engine
     └─────────┬─────────┘
               ▼
           PCM5102 DAC
```

------------------------------------------------------------------------

# Major Features

## Sampling

-   Smart Sample Analysis
-   Automatic Trim
-   Automatic Normalize
-   Zero Crossing Detection
-   Non-destructive Editing

## Playback

-   Polyphonic Playback
-   Keygroups
-   Velocity Layers
-   Round Robin
-   ADSR
-   Filters
-   Pitch Bend
-   Vintage DAC Simulation

## Storage

-   WAV Import
-   SD Card Browser
-   Sample Banks
-   Configuration Files

------------------------------------------------------------------------

# Development Roadmap

``` text
v0.1   Prototype
v0.3   Audio Engine
v0.5   Sample Browser
v0.7   Smart Sampling
        ▲
        │ YOU ARE HERE
        ▼
v0.8   Smart Loop Engine
v0.9   Touch Interface
v1.0   First Public Release
```

------------------------------------------------------------------------

# Open Engineering

Unlike commercial products, Phoenix documents:

-   firmware evolution
-   hardware revisions
-   DSP algorithms
-   performance measurements
-   design decisions
-   engineering notes
-   experiments
-   failures and improvements

Everything is part of the engineering story.

------------------------------------------------------------------------

# Repository Structure

``` text
Project_Phoenix/

firmware/
hardware/
mechanics/
docs/
images/
audio_examples/

README.md
CHANGELOG.md
LICENSE
```

------------------------------------------------------------------------

# Follow the Journey

Project Phoenix is intended to become one of the most comprehensively
documented open embedded sampler projects available.

Whether you are interested in:

-   Embedded Systems
-   Audio DSP
-   MIDI
-   Firmware Architecture
-   Hardware Design
-   Digital Audio

...you are welcome to follow the development.

------------------------------------------------------------------------

# About RTAL

## Realtime Audio Lab

**Engineering Heritage Archive**

Sharing over four decades of experience in:

-   Embedded Audio Systems
-   Digital Musical Instruments
-   Hi-Fi Engineering
-   Vintage Hardware Restoration
-   Open Engineering Documentation

------------------------------------------------------------------------

⭐ **If you enjoy following long-term engineering projects, consider
starring this repository.**


------------------------------------------------------------------------

<p align="center">
  
<img src="images/C64_SFX.jpg" width="900">
  
*C64 SFX Menu*
</p>

<p align="center">
  
<img src="images/C64_SFX_ESP32_Version.jpg" width="900">
  
*C64 SFX ESP32 Version Menu*


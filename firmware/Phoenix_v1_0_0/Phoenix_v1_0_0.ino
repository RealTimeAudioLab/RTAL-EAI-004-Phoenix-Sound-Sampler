// Project Phoenix v1.0.0 FINAL - First Official Release
// Functional baseline: v0.9.0 RC1 / C036d architecture (hardware-confirmed)
/*
  Project Phoenix / SFX-S3
  v0.8.06b C034 - Effects & DSP Robustness

  Phoenix 1.0 loop architecture:
  - Sample Start -> Loop Start -> Loop End -> Sample End
  - OFF, FORWARD and ALTERNATE sustain-loop modes
  - Note Off exits the loop and continues forward toward Sample End
  - Four-marker SAMPLE EDITOR: Sample Start, Loop Start, Loop End, Sample End
  - Every marker follows the encoder live and updates active notes immediately
  - SAMPLE EDITOR playhead follows F7 transport and normal MIDI-triggered voices
  - C010 tracks the exact newest base-sample MIDI voice with an age token
  - C011 transfers all four editor markers atomically with a captured slot ID
  - C012 uses cascading four-marker editing: the selected marker pushes crossed neighbours
  - C013 shows the active slot in the SAMPLE EDITOR title: S1..S4 SAMPLE EDITOR
  - C014 moves multisample path and keygroup metadata from static internal DRAM to PSRAM
  - C015 improves SAMPLE EDITOR precision, marker focus and zoom alignment
  - C020 uses a continuous compact SAMPLE EDITOR footer with full labels
  - C021 turns 7TEST into a safe input-threshold test without recording or slot changes
  - C022 protects occupied slots until AUTO really triggers and adds explicit overwrite confirmation
  - C023 stores the complete SAMPLE EDITOR state in BANK.CFG (BANK_VERSION 14)
  - C023 writes standard WAV smpl metadata for FORWARD and ALTERNATE loops
  - C023 imports WAV smpl loop points and MIDI unity note into a slot
  - C023 scans RIFF chunks independent of their order and restores loops even without BANK.CFG
  - C024 opens a direct post-record screen with EDIT, PLAY, SAVE and BACK
  - C027 adds live system diagnostics for audio deadline, voices, heap and PSRAM
  - C027 provides resettable session peaks and explicit serial snapshots
  - C027a fixes the shared physical voice pool and all POLY limits at 12
  - C027a removes the INITIALIZING text from the boot screen
  - Active voices verify a marker revision every audio block as a live-sync fallback
  - Shared 12-voice stability pool; RTAL AutoLoop remains deferred post-1.0

  Arduino IDE 1.8.19
  ESP32 core 2.0.16
  Board: ESP32-S3 Dev Module

  Display: SSD1309 128x64 SPI via U8g2 full buffer
  OLED: SCK=12, MOSI=11, CS=10, DC=6, RST=7
  Buttons F1..F8: 21, 47, 45, 38, 4, 15, 3, 14
  Encoder: A=1, B=2, SW=42
  Audio: PCM1808 ADC + PCM5102A DAC on shared I2S
  I2S: BCLK=18, LRCK=16, DOUT=17, DIN=5
  MIDI: Serial2 RX=40, TX=39, own task on Core 0

  Task policy:
  - AudioTask: Core 1, high priority 24
  - Gui/Input: Core 0
  - MIDI Task: Core 0, priority 10

  Polyphony:
  - Shared global pool of 12 physical voices
  - Per-slot POLY / MONO / LEGATO allocation
  - Root-note pitch tracking and velocity response
  - Click-safe attack and release envelope
  - Release-aware oldest-voice stealing
*/

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

#include "src/PhoenixGUI/PhoenixGUI.h"
#include "src/PhoenixApp/PhoenixScreens.h"
#include "src/PhoenixOS/PhoenixInputManager.h"
#include "src/PhoenixAudio/PhoenixAudioManager.h"
#include "src/PhoenixOS/PhoenixOS.h"
#include "src/PhoenixMidi/PhoenixMidiParameters.h"
#include "src/PhoenixMidi/PhoenixMidiMonitor.h"
#include "src/PhoenixSystem/PhoenixDiagnostics.h"
#include "src/PhoenixSystem/PhoenixVersion.h"

#define OLED_SCK   12
#define OLED_MOSI  11
#define OLED_CS    10
#define OLED_DC     6
#define OLED_RST    7

#define SD_CS_PIN    9
#define SD_MISO_PIN 13

static const uint8_t BTN_PINS[8] = {21, 47, 45, 38, 4, 15, 3, 14};
#define ENC_A_PIN   1
#define ENC_B_PIN   2
#define ENC_SW_PIN 42

#define I2S_BCLK_PIN 18
#define I2S_LRCK_PIN 16
#define I2S_DOUT_PIN 17
#define I2S_DIN_PIN   5

#define MIDI_RX_PIN 40
#define MIDI_TX_PIN 39

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
PhoenixGUI gui(u8g2);
PhoenixScreenManager screens;
PhoenixInputManager input(BTN_PINS, ENC_A_PIN, ENC_B_PIN, ENC_SW_PIN);
PhoenixAudioManager audio(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);
PhoenixOS phoenix(gui, screens, input, audio);

static volatile uint32_t gMidiBytes = 0;

static void midiTask(void *param) {
  (void)param;
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  // v0.5.5: very small DIN-MIDI parser.
  // Note On with velocity > 0 retriggers the currently selected Quattro slot.
  // Running status is supported for Note On / Note Off.
  uint8_t status = 0;
  uint8_t data1 = 0;
  bool haveData1 = false;

  for (;;) {
    while (Serial2.available()) {
      uint8_t b = (uint8_t)Serial2.read();
      gMidiBytes++;

      if (!screens.midiEnabled()) {
        haveData1 = false;
        continue;
      }

      if (b >= 0xF8) {
        // Realtime bytes may occur anywhere in the MIDI stream and do not
        // disturb running status. F8 is evaluated continuously, also at STOP.
        if (b == 0xF8) phxMidiMonitorRecord(PHX_MMON_CLOCK,0,0,0);
        else if (b == 0xFA) phxMidiMonitorRecord(PHX_MMON_START,0,0,0);
        else if (b == 0xFB) phxMidiMonitorRecord(PHX_MMON_CONTINUE,0,0,0);
        else if (b == 0xFC) phxMidiMonitorRecord(PHX_MMON_STOP,0,0,0);
        if (!screens.midiClockEnabled()) continue;
        if (b == 0xF8) phoenix.notifyMidiClock();
        else if (b == 0xFA) phoenix.notifyMidiStart();
        else if (b == 0xFB) phoenix.notifyMidiContinue();
        else if (b == 0xFC) phoenix.notifyMidiStop();
        continue;
      }

      if (b & 0x80) {
        status = b;
        haveData1 = false;
        continue;
      }

      if (status == 0) continue;
      const uint8_t msg = status & 0xF0;

      if (msg == 0x90 || msg == 0x80) {
        if (!haveData1) {
          data1 = b;
          haveData1 = true;
        } else {
          uint8_t velocity = b;
          haveData1 = false;
          const uint8_t ch = (status & 0x0F) + 1;
          phxMidiMonitorRecord((msg == 0x90 && velocity > 0) ? PHX_MMON_NOTE_ON : PHX_MMON_NOTE_OFF, ch, data1, velocity);
          if (screens.midiEnabled() && screens.midiNoteTriggerEnabled()) {
            if (screens.quattroMode() == 0) {
              // KEYZONE: base MIDI channel; every matching zone triggers. Overlaps create layers.
              if (screens.midiOmni() || ch == screens.midiChannel()) {
                for (uint8_t slot = 0; slot < 4; ++slot) {
                  if (data1 >= screens.quattroKeyLow(slot) && data1 <= screens.quattroKeyHigh(slot)) {
                    if (msg == 0x90 && velocity > 0) audio.queueMidiNoteOn(slot, data1, velocity, screens.playbackReverseMode());
                    else audio.queueMidiNoteOff(slot, data1);
                  }
                }
              }
            } else {
              // MULTI: individual channel per slot, full keyboard; base channel and keyzones ignored.
              for (uint8_t slot = 0; slot < 4; ++slot) {
                if (ch == screens.quattroMidiChannel(slot)) {
                  if (msg == 0x90 && velocity > 0) audio.queueMidiNoteOn(slot, data1, velocity, screens.playbackReverseMode());
                  else audio.queueMidiNoteOff(slot, data1);
                }
              }
            }
          }
        }
      } else if (msg == 0xE0) {
        // Pitch Bend: MIDI sends LSB then MSB; center is 8192.
        if (!haveData1) { data1 = b; haveData1 = true; }
        else {
          const uint8_t ch = (status & 0x0F) + 1;
          const uint16_t raw14 = ((uint16_t)b << 7) | data1;
          const int16_t bend = (int16_t)raw14 - 8192;
          haveData1 = false;
          phxMidiMonitorRecord(PHX_MMON_PITCH_BEND,ch,0,bend);
          if (screens.quattroMode() == 0) {
            if (screens.midiOmni() || ch == screens.midiChannel()) {
              for (uint8_t slot = 0; slot < 4; ++slot) audio.queueMidiPitchBend(slot, bend);
            }
          } else {
            for (uint8_t slot = 0; slot < 4; ++slot) {
              if (ch == screens.quattroMidiChannel(slot)) audio.queueMidiPitchBend(slot, bend);
            }
          }
        }
      } else if (msg == 0xB0) {
        // Control Change: sustain and channel-mode messages are always accepted
        // while MIDI input is enabled. Other CC parameter control remains governed
        // by the existing MIDI CC setting.
        if (!haveData1) {
          data1 = b; // controller number
          haveData1 = true;
        } else {
          const uint8_t value = b;
          const uint8_t ch = (status & 0x0F) + 1;
          haveData1 = false;
          phxMidiMonitorRecord(PHX_MMON_CC,ch,data1,value);

          const bool systemCc = (data1 == 64 || data1 == 120 || data1 == 121 || data1 == 123);
          if (!systemCc && !screens.midiCcControlEnabled()) continue;

          if (screens.quattroMode() == 0) {
            // KEYZONE: channel-mode controllers on the base channel affect all
            // four slots, including overlapping layers.
            if (screens.midiOmni() || ch == screens.midiChannel()) {
              for (uint8_t slot = 0; slot < 4; ++slot) {
                if (data1 == 64) {
                  audio.queueMidiSustain(slot, value >= 64);
                } else if (data1 == 123) {
                  audio.queueMidiAllNotesOff(slot, false);
                } else if (data1 == 120) {
                  audio.queueMidiAllNotesOff(slot, true);
                } else if (data1 == 121) {
                  audio.queueMidiSustain(slot, false);
                  audio.queueMidiPitchBend(slot, 0);
                }
              }
              if (!systemCc) phoenix.queueMidiControlChange(audio.selectedSlot(), data1, value);
            }
          } else {
            // MULTI: each slot reacts only to controllers on its own channel.
            for (uint8_t slot = 0; slot < 4; ++slot) {
              if (ch != screens.quattroMidiChannel(slot)) continue;
              if (data1 == 64) {
                audio.queueMidiSustain(slot, value >= 64);
              } else if (data1 == 123) {
                audio.queueMidiAllNotesOff(slot, false);
              } else if (data1 == 120) {
                audio.queueMidiAllNotesOff(slot, true);
              } else if (data1 == 121) {
                audio.queueMidiSustain(slot, false);
                audio.queueMidiPitchBend(slot, 0);
              } else {
                phoenix.queueMidiControlChange(slot, data1, value);
              }
            }
          }
        }
      } else if (msg == 0xC0) {
        // Program Change. One data byte.
        const uint8_t ch=(status & 0x0F)+1;
        phxMidiMonitorRecord(PHX_MMON_PROGRAM,ch,b,0);
        if (!screens.midiProgramChangeEnabled()) { haveData1 = false; continue; }
        haveData1 = false;
      } else if (msg == 0xD0) {
        // Channel pressure. One data byte.
        const uint8_t ch=(status & 0x0F)+1;
        phxMidiMonitorRecord(PHX_MMON_CHANNEL_PRESSURE,ch,0,b);
        haveData1=false;
      } else {
        // Ignore other messages for now, but keep running safely.
        haveData1 = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void controlTask(void *param) {
  (void)param;
  Serial.printf("V1.0 CONTROL task core=%d priority=%u\n", xPortGetCoreID(), (unsigned)uxTaskPriorityGet(nullptr));
  for (;;) {
    phoenix.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.printf("%s - %s\n", PHX_FULL_VERSION, PHX_BUILD_NAME);
  Serial.printf("CPU LOAD MEASUREMENT: configured voices=%u, sample rate=32000 Hz, block=128 frames, deadline=4000 us\n",
                (unsigned)PhoenixAudioManager::VOICE_COUNT);
  Serial.println("Open Serial Monitor at 115200 baud.");

  SPI.begin(OLED_SCK, SD_MISO_PIN, OLED_MOSI, OLED_CS);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  delay(20);
  const bool serviceMode = (digitalRead(ENC_SW_PIN) == LOW);
  PhoenixDiagnostics::printBootSnapshot("BEFORE INIT", 0U);
  phoenix.begin(serviceMode);
  PhoenixDiagnostics::printBootSnapshot("READY", screens.smartProcessingMode());
  phoenix.setMidiCounterSource(&gMidiBytes);
  xTaskCreatePinnedToCore(midiTask, "MIDI", 4096, nullptr, 10, nullptr, 0);
  xTaskCreatePinnedToCore(controlTask, "PhoenixControl", 16384, nullptr, 3, nullptr, 0);
  Serial.printf("V1.0 SETUP core=%d; Audio=Core1/P24, MIDI=Core0/P10, SeqRT=Core0/P8, Control=Core0/P3\n", xPortGetCoreID());
}

void loop() {
  // C029: the Arduino loop task is intentionally idle. GUI/control runs pinned on Core 0.
  vTaskDelay(pdMS_TO_TICKS(1000));
}

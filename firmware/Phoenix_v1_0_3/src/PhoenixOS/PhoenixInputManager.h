/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <Arduino.h>
#include "PhoenixEventBus.h"
#include "../PhoenixApp/PhoenixInput.h"

class PhoenixInputManager {
public:
  PhoenixInputManager(const uint8_t *buttonPins, uint8_t encA, uint8_t encB, uint8_t encSw);

  void begin();
  void update(PhoenixInputState &state, PhoenixEventBus &events);

private:
  struct ButtonState {
    uint8_t pin;
    bool lastRaw;
    bool stable;
    bool press;
    bool longPress;
    bool longFired;
    uint32_t lastChangeMs;
    uint32_t downMs;
  };

  const uint8_t *_buttonPins;
  uint8_t _encA;
  uint8_t _encB;
  uint8_t _encSw;
  ButtonState _buttons[8];
  ButtonState _encButton;
  volatile int32_t _encRawDelta;
  int32_t _encAccum;
  uint8_t _encLast;
  uint32_t _encLastEmitMs;

  static PhoenixInputManager *_instance;
  static void IRAM_ATTR encoderISR();
  void IRAM_ATTR scanEncoderISR();

  void initButton(ButtonState &b, uint8_t pin);
  void scanButton(ButtonState &b);
  void scanEncoder();
};

#include "PhoenixInputManager.h"

PhoenixInputManager *PhoenixInputManager::_instance = nullptr;

PhoenixInputManager::PhoenixInputManager(const uint8_t *buttonPins, uint8_t encA, uint8_t encB, uint8_t encSw)
: _buttonPins(buttonPins), _encA(encA), _encB(encB), _encSw(encSw), _encRawDelta(0), _encAccum(0), _encLast(0), _encLastEmitMs(0) {}

void PhoenixInputManager::initButton(ButtonState &b, uint8_t pin) {
  b.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  b.lastRaw = digitalRead(pin);
  b.stable = b.lastRaw;
  b.press = false;
  b.longPress = false;
  b.longFired = false;
  b.lastChangeMs = millis();
  b.downMs = 0;
}

void PhoenixInputManager::begin() {
  for (uint8_t i = 0; i < 8; ++i) initButton(_buttons[i], _buttonPins[i]);
  initButton(_encButton, _encSw);
  pinMode(_encA, INPUT_PULLUP);
  pinMode(_encB, INPUT_PULLUP);
  _encLast = (digitalRead(_encA) ? 1 : 0) | (digitalRead(_encB) ? 2 : 0);
  _encRawDelta = 0;
  _encAccum = 0;
  _instance = this;
  attachInterrupt(digitalPinToInterrupt(_encA), PhoenixInputManager::encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(_encB), PhoenixInputManager::encoderISR, CHANGE);
}

void IRAM_ATTR PhoenixInputManager::encoderISR() {
  if (_instance) _instance->scanEncoderISR();
}

void IRAM_ATTR PhoenixInputManager::scanEncoderISR() {
  uint8_t s = (digitalRead(_encA) ? 1 : 0) | (digitalRead(_encB) ? 2 : 0);
  if (s != _encLast) {
    static const int8_t tbl[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    _encRawDelta += tbl[(_encLast << 2) | s];
    _encLast = s;
  }
}

void PhoenixInputManager::scanButton(ButtonState &b) {
  b.press = false;
  b.longPress = false;
  const bool raw = digitalRead(b.pin);
  const uint32_t now = millis();

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChangeMs = now;
  }

  if ((now - b.lastChangeMs) > 18 && raw != b.stable) {
    b.stable = raw;
    if (b.stable == LOW) {
      b.downMs = now;
      b.longFired = false;
    } else {
      if (!b.longFired) {
        const uint32_t dur = now - b.downMs;
        if (dur < 650) b.press = true;
      }
    }
  }

  if (b.stable == LOW && !b.longFired && b.downMs != 0 && (now - b.downMs) >= 650) {
    b.longPress = true;
    b.longFired = true;
  }
}

void PhoenixInputManager::scanEncoder() {
  // v0.7.0: encoder is collected in interrupt context.
  // This function remains for compatibility with the update flow.
}

void PhoenixInputManager::update(PhoenixInputState &state, PhoenixEventBus &events) {
  state.clear();

  for (uint8_t i = 0; i < 8; ++i) {
    scanButton(_buttons[i]);
    state.f[i + 1] = _buttons[i].press;
    if (_buttons[i].press) events.post((PhoenixEventType)(PHX_EVT_F1 + i));
  }

  scanButton(_encButton);
  scanEncoder();

  // v0.7.0: robust encoder handling.
  // The ISR collects every quadrature edge in _encRawDelta. The GUI consumes
  // accumulated detents once per update cycle, applies bounded acceleration,
  // and preserves the sub-detent remainder. This avoids lost steps during fast
  // turns and avoids unpredictable large jumps.
  int32_t raw = 0;
  noInterrupts();
  raw = _encRawDelta;
  _encRawDelta = 0;
  interrupts();

  if (raw != 0) _encAccum += raw;

  if (_encAccum >= 4 || _encAccum <= -4) {
    int16_t detents = (int16_t)(_encAccum / 4);
    _encAccum -= (int32_t)detents * 4;

    const int8_t dir = (detents > 0) ? 1 : -1;
    int16_t amount = detents > 0 ? detents : -detents;
    if (amount > 12) amount = 12;  // hard safety cap per GUI update

    int16_t mag = 1;
    if (amount >= 8)      mag = 10;
    else if (amount >= 4) mag = 5;
    else if (amount >= 2) mag = 2;

    state.encoderStep = dir * mag;
    _encLastEmitMs = millis();
    events.post(dir > 0 ? PHX_EVT_ENCODER_RIGHT : PHX_EVT_ENCODER_LEFT);
  }

  state.encPress = _encButton.press;
  state.encLong = _encButton.longPress;
  if (state.encPress) events.post(PHX_EVT_ENCODER_PRESS);
  if (state.encLong) events.post(PHX_EVT_ENCODER_LONG);
}

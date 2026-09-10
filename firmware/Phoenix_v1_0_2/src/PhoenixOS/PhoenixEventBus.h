#pragma once
#include <Arduino.h>

enum PhoenixEventType : uint8_t {
  PHX_EVT_NONE = 0,
  PHX_EVT_F1,
  PHX_EVT_F2,
  PHX_EVT_F3,
  PHX_EVT_F4,
  PHX_EVT_F5,
  PHX_EVT_F6,
  PHX_EVT_F7,
  PHX_EVT_F8,
  PHX_EVT_ENCODER_LEFT,
  PHX_EVT_ENCODER_RIGHT,
  PHX_EVT_ENCODER_PRESS,
  PHX_EVT_ENCODER_LONG,
  PHX_EVT_RECORD_PRESSED,
  PHX_EVT_PLAY_PRESSED,
  PHX_EVT_AUDIO_PEAK_CHANGED
};

struct PhoenixEvent {
  PhoenixEventType type;
  int16_t value;
};

class PhoenixEventBus {
public:
  PhoenixEventBus();
  void clear();
  bool post(PhoenixEventType type, int16_t value = 0);
  bool poll(PhoenixEvent &event);

private:
  static const uint8_t CAPACITY = 16;
  PhoenixEvent _queue[CAPACITY];
  volatile uint8_t _head;
  volatile uint8_t _tail;
};

/*
 * Project Phoenix
 * Copyright (C) 2026 RealTimeAudioLab
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "PhoenixEventBus.h"

PhoenixEventBus::PhoenixEventBus() : _head(0), _tail(0) {}

void PhoenixEventBus::clear() {
  _head = 0;
  _tail = 0;
}

bool PhoenixEventBus::post(PhoenixEventType type, int16_t value) {
  const uint8_t next = (_head + 1) % CAPACITY;
  if (next == _tail) return false; // full
  _queue[_head].type = type;
  _queue[_head].value = value;
  _head = next;
  return true;
}

bool PhoenixEventBus::poll(PhoenixEvent &event) {
  if (_tail == _head) return false;
  event = _queue[_tail];
  _tail = (_tail + 1) % CAPACITY;
  return true;
}

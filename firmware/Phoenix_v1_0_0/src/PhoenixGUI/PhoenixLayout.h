#pragma once
#include <Arduino.h>

struct PhoenixLayout {
  // 128 x 64 SSD1309 Classic Layout
  static const uint8_t LEVEL_X = 0;
  static const uint8_t LEVEL_Y = 0;
  static const uint8_t LEVEL_W = 23;
  static const uint8_t LEVEL_H = 57;

  static const uint8_t WIN_X = 25;
  static const uint8_t WIN_Y = 0;
  static const uint8_t WIN_W = 102;
  static const uint8_t WIN_H = 57;

  static const uint8_t TITLE_Y = 2;
  static const uint8_t TITLE_LINE_Y = 8;
  static const uint8_t MENU_X_PAD = 4;
  static const uint8_t MENU_FIRST_Y = 9;
  static const uint8_t MENU_ROW_H = 6;

  static const uint8_t HELP_Y = 59;
};

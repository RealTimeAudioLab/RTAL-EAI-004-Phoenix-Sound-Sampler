#pragma once
#include <Arduino.h>

// PhoenixGUI Classic RC central metrics
// All typographic spacings for Classic OLED layout live here.
// Keep these values small and integer-pixel exact: SSD1309 128 x 64.
struct PhoenixMetrics {
  // Dialog / popup metrics
  static const uint8_t DIALOG_X = 27;
  static const uint8_t DIALOG_Y = 4;
  static const uint8_t DIALOG_W = 96;
  static const uint8_t DIALOG_H = 56;

  static const uint8_t DIALOG_TITLE_Y = 2;
  static const uint8_t DIALOG_TITLE_LINE_Y = 8;

  // Body text: top coordinate and raster pitch.
  static const uint8_t DIALOG_BODY_Y = 15;
  static const uint8_t DIALOG_BODY_PITCH = 7;

  // Footer has its own breathing room.
  static const uint8_t DIALOG_FOOTER_LINE_FROM_BOTTOM = 14;
  static const uint8_t DIALOG_FOOTER_TEXT_FROM_BOTTOM = 10;

  // Modal background clear margin.
  static const uint8_t DIALOG_CLEAR_MARGIN = 2;
};

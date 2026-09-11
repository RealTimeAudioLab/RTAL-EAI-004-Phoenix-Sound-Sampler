# Project Phoenix v1.0.3 — Hardware Reference

## Audio / I2S

| Signal | GPIO | Connection |
|---|---:|---|
| MCLK | 0 | PCM1808 SCKI/MCLK |
| BCLK | 18 | PCM1808 BCK + PCM5102A BCK |
| LRCK/WS | 16 | PCM1808 LRCK + PCM5102A LRCK |
| DOUT | 17 | PCM5102A DIN |
| DIN | 5 | PCM1808 DOUT |

## OLED SSD1309

SCK=12, MOSI=11, CS=10, DC=6, RST=7.

## SD card

SCK=12, MOSI=11, MISO=13, CS=9. OLED and SD share SCK/MOSI.

## MIDI DIN

RX=40, TX=39, 31,250 baud.

## Buttons

F1=21, F2=47, F3=45, F4=38, F5=4, F6=15, F7=3, F8=14.

## Encoder

A=1, B=2, switch=42.

## Native USB

GPIO19=D-, GPIO20=D+ on the target design.

## Notes

- Target MCU: ESP32-S3 @ 240 MHz.
- The validated target uses PSRAM and a 16 MB flash configuration.
- GPIO availability beyond this pin map depends on the exact ESP32-S3 board/module and its flash/PSRAM/strapping configuration.

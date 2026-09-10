#include "PhoenixDiagnostics.h"
#include "PhoenixVersion.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

const char *PhoenixDiagnostics::smartModeName(uint8_t mode) {
  if (mode == 1U) return "SMART";
  if (mode == 2U) return "FORCE";
  return "SAFE";
}

void PhoenixDiagnostics::printBytes(const __FlashStringHelper *label, uint32_t bytes) {
  Serial.print(label);
  Serial.printf("%lu bytes (%lu kB)\n",
                (unsigned long)bytes,
                (unsigned long)((bytes + 512UL) / 1024UL));
}

void PhoenixDiagnostics::printBootSnapshot(const char *stage, uint8_t smartMode) {
  const uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalMinimum = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t psramTotal = ESP.getPsramSize();
  const uint32_t psramFree = ESP.getFreePsram();
  const uint32_t psramLargest = psramTotal
      ? heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
      : 0U;

  Serial.println();
  Serial.println(F("========================================"));
  Serial.print(F(" PROJECT PHOENIX v")); Serial.print(PHX_VERSION_STRING); Serial.print(F(" ")); Serial.println(PHX_BUILD_CODE);
  Serial.println(F(" SYSTEM DIAGNOSTICS"));
  Serial.println(F("========================================"));
  Serial.printf("Stage             %s\n", stage ? stage : "UNKNOWN");
  Serial.printf("Chip              %s rev %u\n", ESP.getChipModel(), (unsigned)ESP.getChipRevision());
  Serial.printf("CPU               %u MHz\n", (unsigned)ESP.getCpuFreqMHz());
  printBytes(F("Flash total       "), ESP.getFlashChipSize());
  printBytes(F("Firmware          "), ESP.getSketchSize());
  printBytes(F("Firmware free     "), ESP.getFreeSketchSpace());
  printBytes(F("Internal free     "), internalFree);
  printBytes(F("Largest block     "), internalLargest);
  printBytes(F("Minimum free      "), internalMinimum);
  printBytes(F("PSRAM total       "), psramTotal);
  printBytes(F("PSRAM free        "), psramFree);
  printBytes(F("PSRAM largest     "), psramLargest);
  Serial.printf("Smart mode        %s\n", smartModeName(smartMode));
  Serial.printf("Build             %s %s\n", __DATE__, __TIME__);
  Serial.println(F("========================================"));
}

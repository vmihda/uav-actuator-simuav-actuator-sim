#include "device_info.h"

#include <Esp.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_mac.h>

DeviceInfo readDeviceInfo() {
  DeviceInfo info{};
  info.macValid = esp_efuse_mac_get_default(info.mac) == ESP_OK;
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  info.isEsp32 = chip.model == CHIP_ESP32;
  info.revision = chip.revision;
  info.model = ESP.getChipModel();
  uint32_t flashBytes = 0;
  // Detected chip size (JEDEC ID), not the size the image header claims.
  info.flashBytes = esp_flash_get_size(nullptr, &flashBytes) == ESP_OK ? flashBytes : 0;
  info.freeHeap = ESP.getFreeHeap();
  return info;
}

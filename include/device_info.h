#pragma once

#include <cstdint>

struct DeviceInfo {
  bool macValid;  // Factory MAC read from eFuse with a valid CRC.
  uint8_t mac[6];
  bool isEsp32;
  const char* model;
  uint8_t revision;
  uint32_t flashBytes;  // Detected flash chip size.
  uint32_t freeHeap;
};

DeviceInfo readDeviceInfo();

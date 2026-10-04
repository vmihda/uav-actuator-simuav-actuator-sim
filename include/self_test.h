#pragma once

#include <cstdint>
#include "device_info.h"
#include "fsm.h"
#include "power_monitor.h"

enum class SelfTestMode : uint8_t { Boot, Recovery };

struct SelfTestInputs {
  bool storageOk;
  bool settingsOk;
  const char* settingsError;
  DeviceInfo device;
  bool buttonEnabled;
  bool buttonStuck;
  bool contactStuck;  // Whiskers closed for contact.stuck_ms.
  PowerVerdict powerVerdict;
  PowerVerdict powerInstant;
  bool pwmReady;
  bool linksOk;
};

using SelfTestLog = void (*)(const char* line);

bool chipHealthy(const DeviceInfo& device, const char*& detail);
// Logs one POST line per check and returns the first failure's code.
ErrorCode runSelfTest(SelfTestMode mode, const SelfTestInputs& inputs, SelfTestLog log);

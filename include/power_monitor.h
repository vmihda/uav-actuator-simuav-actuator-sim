#pragma once

#include <cstdint>
#include "sensor.h"

enum class PowerVerdict : uint8_t { Ok, OutOfRange, SensorFailure };

const char* powerVerdictName(PowerVerdict verdict);

// Reports a bad supply only after it persists for stableMs.
class PowerMonitor {
 public:
  void configure(Sensor* sensor, int32_t minMv, int32_t maxMv, uint32_t stableMs);
  PowerVerdict update(uint32_t nowMs);
  // Persistent verdict used for faults.
  PowerVerdict verdict() const { return verdict_; }
  // Classification of the latest reading, without persistence.
  PowerVerdict instant() const { return instant_; }
  SensorReading reading() const { return reading_; }

 private:
  Sensor* sensor_ = nullptr;
  int32_t minMv_ = 0;
  int32_t maxMv_ = 0;
  uint32_t stableMs_ = 0;
  SensorReading reading_{false, 0};
  PowerVerdict instant_ = PowerVerdict::Ok;
  PowerVerdict verdict_ = PowerVerdict::Ok;
  uint32_t badSince_ = 0;
};

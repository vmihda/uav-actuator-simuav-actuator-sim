#pragma once

#include <cstdint>

struct SensorReading {
  bool valid;
  int32_t value;  // Sensor units, e.g. millivolts.
};

// Sensor abstraction: a physical driver and a simulation are interchangeable.
class Sensor {
 public:
  virtual ~Sensor() = default;
  virtual const char* name() const = 0;
  virtual bool isSimulated() const = 0;
  virtual SensorReading read(uint32_t nowMs) = 0;
};

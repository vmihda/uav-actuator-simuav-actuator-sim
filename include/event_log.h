#pragma once

#include <cstdint>

class EventLog {
 public:
  bool begin();
  bool recordDeployment(uint32_t now, uint32_t count);
};

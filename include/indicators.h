#pragma once

#include "fsm.h"

class Indicators {
 public:
  void begin();
  void update(const ActuatorFsm& fsm, uint32_t now);
 private:
  bool on_ = false;
};

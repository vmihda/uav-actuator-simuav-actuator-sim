#pragma once

#include "fsm.h"

class Indicators {
 public:
  // False when the LED or buzzer pin cannot drive an output.
  bool begin();
  void update(const ActuatorFsm& fsm, uint32_t now);
 private:
  bool on_ = false;
};

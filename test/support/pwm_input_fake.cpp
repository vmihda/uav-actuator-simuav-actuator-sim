#include <deque>

#include "pwm_input.h"

// Host fake: tests queue one pulse width per loop iteration.
std::deque<uint16_t> fakePwmPulses;
bool fakePwmBeginResult = true;
int fakePwmPin = -1;

bool pwmInputBegin(int pin) {
  fakePwmPin = pin;
  return fakePwmBeginResult;
}

bool pwmInputTake(uint16_t& widthUs) {
  if (fakePwmPulses.empty()) return false;
  widthUs = fakePwmPulses.front();
  fakePwmPulses.pop_front();
  return true;
}

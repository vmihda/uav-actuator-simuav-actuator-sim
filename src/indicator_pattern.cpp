#include "indicator_pattern.h"

bool indicatorOn(ActuatorState state, uint32_t elapsedMs) {
  struct BlinkPattern { ActuatorState state; uint32_t on; uint32_t off; };
  static const BlinkPattern patterns[] = {
    {ActuatorState::SAFE, 1000, 1000},
    {ActuatorState::ARMING, 250, 250},
    {ActuatorState::ARMED, 100, 100}
  };
  for (const auto& pattern : patterns) {
    if (state == pattern.state)
      return elapsedMs % (pattern.on + pattern.off) < pattern.on;
  }
  if (state == ActuatorState::FAULT) {
    // Three 100ms flashes separated by 100ms, followed by a 1100ms pause.
    const uint32_t phase = elapsedMs % 1600;
    return phase < 500 && phase % 200 < 100;
  }
  // ACTUATED follows the FSM pulse flag in Indicators::update, not elapsed time.
  return false;
}

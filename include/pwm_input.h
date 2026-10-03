#pragma once

#include <cstdint>

// Starts edge capture on the pin; false when the interrupt cannot be registered.
bool pwmInputBegin(int pin);
// Returns the latest pulse width once; false when no new pulse arrived.
bool pwmInputTake(uint16_t& widthUs);

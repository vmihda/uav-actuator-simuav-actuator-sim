#pragma once

#include <cstddef>
#include <cstdint>
#include "config.h"
#include "fsm.h"

enum class ParseKind { None, CommandReady, Invalid, Overflow, SimulateVin, SimulateContact };
struct ParseResult {
  ParseKind kind;
  Command command;
  int32_t value;  // Millivolts for SimulateVin, 0 or 1 for SimulateContact.
};

class UartLineParser {
 public:
  // Only the test-build USB console accepts simulation commands.
  explicit UartLineParser(bool allowSimulation = false) : allowSimulation_(allowSimulation) {}
  ParseResult feed(char byte);
 private:
  bool allowSimulation_;
  char buffer_[config::kUartLineCapacity] = {};
  std::size_t length_ = 0;
  bool discarding_ = false;
};

bool formatTelemetry(const ActuatorFsm& fsm, uint32_t now,
                     char* buffer, std::size_t capacity);

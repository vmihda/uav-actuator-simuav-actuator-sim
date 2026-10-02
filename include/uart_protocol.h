#pragma once

#include <cstddef>
#include "config.h"
#include "fsm.h"

enum class ParseKind { None, CommandReady, Invalid, Overflow };
struct ParseResult {
  ParseKind kind;
  Command command;
};

class UartLineParser {
 public:
  ParseResult feed(char byte);
 private:
  char buffer_[config::kUartLineCapacity] = {};
  std::size_t length_ = 0;
  bool discarding_ = false;
};

bool formatTelemetry(const ActuatorFsm& fsm, uint32_t now,
                     char* buffer, std::size_t capacity);

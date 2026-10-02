#pragma once

#include "fsm.h"

using CommandHandler = bool (*)(Command command, uint32_t now);

// Unavailable means the FSM did not answer in time: the command may still run.
enum class CommandResult { Accepted, Rejected, Unavailable };
using WebCommandHandler = CommandResult (*)(Command command, uint32_t now);

struct HttpCommand {
  Command command;
  uint32_t issuedAt;
  uint32_t id;
  static constexpr uint32_t kTimeoutMs = 1000;
  uint32_t remainingMs(uint32_t now) const {
    const uint32_t elapsed = now - issuedAt;
    return elapsed >= kTimeoutMs ? 0 : kTimeoutMs - elapsed;
  }
};

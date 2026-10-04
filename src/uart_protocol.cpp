#include "uart_protocol.h"
#include <cstdio>
#include <cstring>

namespace {
// Accepts exactly "SIM:VIN:<1-5 digits>" with a value of at most 30000 mV.
bool parseSimulatedVin(const char* line, int32_t& out) {
  static const char prefix[] = "SIM:VIN:";
  if (std::strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* digits = line + sizeof(prefix) - 1;
  const std::size_t count = std::strlen(digits);
  if (count == 0 || count > 5) return false;
  int32_t value = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (digits[i] < '0' || digits[i] > '9') return false;
    value = value * 10 + (digits[i] - '0');
  }
  if (value > 30000) return false;
  out = value;
  return true;
}

// Accepts exactly "SIM:CONTACT:0" (open) or "SIM:CONTACT:1" (closed).
bool parseSimulatedContact(const char* line, int32_t& out) {
  static const char prefix[] = "SIM:CONTACT:";
  if (std::strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* digit = line + sizeof(prefix) - 1;
  if ((digit[0] != '0' && digit[0] != '1') || digit[1] != '\0') return false;
  out = digit[0] - '0';
  return true;
}
}  // namespace

ParseResult UartLineParser::feed(char byte) {
  const ParseResult none{ParseKind::None, Command::Invalid, 0};
  if (discarding_) {
    if (byte == '\n') discarding_ = false;
    return none;
  }
  if (byte == '\n') {
    if (length_ > 0 && buffer_[length_ - 1] == '\r') --length_;
    buffer_[length_] = '\0';
    const bool empty = length_ == 0;
    length_ = 0;
    if (empty) return none;
    int32_t millivolts = 0;
    if (allowSimulation_ && parseSimulatedVin(buffer_, millivolts))
      return {ParseKind::SimulateVin, Command::Invalid, millivolts};
    int32_t contact = 0;
    if (allowSimulation_ && parseSimulatedContact(buffer_, contact))
      return {ParseKind::SimulateContact, Command::Invalid, contact};
    struct Mapping { const char* text; Command command; };
    static const Mapping commands[] = {
      {"CMD:START", Command::Start}, {"CMD:STOP", Command::Stop},
      {"CMD:DEPLOY", Command::Deploy}, {"CMD:STATUS", Command::Status}
    };
    for (const auto& mapping : commands) {
      if (std::strcmp(buffer_, mapping.text) == 0)
        return {ParseKind::CommandReady, mapping.command, 0};
    }
    return {ParseKind::Invalid, Command::Invalid, 0};
  }
  // Embedded NUL/control/non-ASCII bytes must never produce a valid prefix.
  const auto value = static_cast<unsigned char>(byte);
  if ((value < 32 && byte != '\r') || value > 126) {
    length_ = 0;
    discarding_ = true;
    return {ParseKind::Invalid, Command::Invalid, 0};
  }
  if (length_ >= sizeof(buffer_) - 1) {
    length_ = 0;
    discarding_ = true;
    return {ParseKind::Overflow, Command::Invalid, 0};
  }
  buffer_[length_++] = byte;
  return none;
}

bool formatTelemetry(const ActuatorFsm& fsm, uint32_t now,
                     char* buffer, std::size_t capacity) {
  if (buffer == nullptr || capacity == 0) return false;
  const int written = std::snprintf(buffer, capacity,
      "STATE:%s,TIME_LEFT:%lu,ERR:%u\n", ActuatorFsm::stateName(fsm.state()),
      static_cast<unsigned long>(fsm.timeLeftSeconds(now)),
      static_cast<unsigned>(fsm.error()));
  return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

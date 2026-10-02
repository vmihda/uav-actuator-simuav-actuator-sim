#include "uart_protocol.h"
#include <cstdio>
#include <cstring>

ParseResult UartLineParser::feed(char byte) {
  const ParseResult none{ParseKind::None, Command::Invalid};
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
    struct Mapping { const char* text; Command command; };
    static const Mapping commands[] = {
      {"CMD:START", Command::Start}, {"CMD:STOP", Command::Stop},
      {"CMD:DEPLOY", Command::Deploy}, {"CMD:STATUS", Command::Status}
    };
    for (const auto& mapping : commands) {
      if (std::strcmp(buffer_, mapping.text) == 0)
        return {ParseKind::CommandReady, mapping.command};
    }
    return {ParseKind::Invalid, Command::Invalid};
  }
  // Embedded NUL/control/non-ASCII bytes must never produce a valid prefix.
  const auto value = static_cast<unsigned char>(byte);
  if ((value < 32 && byte != '\r') || value > 126) {
    length_ = 0;
    discarding_ = true;
    return {ParseKind::Invalid, Command::Invalid};
  }
  if (length_ >= sizeof(buffer_) - 1) {
    length_ = 0;
    discarding_ = true;
    return {ParseKind::Overflow, Command::Invalid};
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

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

#include "fsm.h"
#include "uart_protocol.h"

namespace {
bool parseTick(const std::string& text, uint32_t& value) {
  if (text.empty()) return false;
  for (char byte : text) if (byte < '0' || byte > '9') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (errno != 0 || *end != '\0' || parsed >= 0x80000000UL) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

void status(const ActuatorFsm& fsm, uint32_t now) {
  char line[96];
  if (formatTelemetry(fsm, now, line, sizeof(line))) std::cout << line;
  std::cout << "PULSE:" << (fsm.pulseActive() ? 1 : 0)
            << ",DEPLOYMENTS:" << fsm.deploymentCount() << '\n';
}
}  // namespace

int main() {
  ActuatorFsm fsm;
  UartLineParser parser;
  uint32_t now = 0;
  fsm.completePost(true, now);
  std::cerr << "Commands: CMD:START / STOP / DEPLOY / STATUS, TICK:<ms>, FAULT\n";
  status(fsm, now);
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.compare(0, 5, "TICK:") == 0) {
      uint32_t duration = 0;
      if (parseTick(line.substr(5), duration)) { now += duration; fsm.update(now); }
      else fsm.handle(Command::Invalid, now);
    } else if (line == "FAULT") {
      fsm.fault(ErrorCode::SelfTestFailed, now);
    } else {
      line += '\n';
      for (char byte : line) {
        const auto result = parser.feed(byte);
        if (result.kind == ParseKind::CommandReady) fsm.handle(result.command, now);
        else if (result.kind == ParseKind::Overflow) fsm.fault(ErrorCode::UartOverflow, now);
        else if (result.kind == ParseKind::Invalid) fsm.handle(Command::Invalid, now);
      }
    }
    status(fsm, now);
  }
}

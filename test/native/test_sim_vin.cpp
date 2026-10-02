#include <string>

#include "harness.h"
#include "uart_handler.h"

namespace {
ParseResult feedLine(UartLineParser& parser, const std::string& line) {
  ParseResult result{ParseKind::None, Command::Invalid, 0};
  for (char byte : line) {
    const auto next = parser.feed(byte);
    if (next.kind != ParseKind::None) result = next;
  }
  return result;
}

ActuatorFsm* simTarget = nullptr;
int32_t simulatedMv = 0;
bool acceptCommand(Command command, uint32_t now) { return simTarget->handle(command, now); }
bool acceptSimulation(int32_t millivolts) {
  simulatedMv = millivolts;
  return true;
}
bool rejectSimulation(int32_t) { return false; }
}  // namespace

void runSimVinTests() {
  test("the default parser treats SIM:VIN as an invalid command", [] {
    UartLineParser parser;
    CHECK(feedLine(parser, "SIM:VIN:4200\n").kind == ParseKind::Invalid);
  });
  test("the simulation parser accepts SIM:VIN within 0..30000 mV only", [] {
    UartLineParser parser(true);
    const auto result = feedLine(parser, "SIM:VIN:4200\n");
    CHECK(result.kind == ParseKind::SimulateVin && result.value == 4200);
    CHECK(feedLine(parser, "SIM:VIN:0\r\n").value == 0);
    CHECK(feedLine(parser, "SIM:VIN:30000\n").value == 30000);
    for (const char* line : {"SIM:VIN:\n", "SIM:VIN:30001\n", "SIM:VIN:42a\n",
                             "SIM:VIN:-5\n", "SIM:VIN:100000\n", "SIM:VIN: 42\n"})
      CHECK(feedLine(parser, line).kind == ParseKind::Invalid);
    CHECK(feedLine(parser, "CMD:STATUS\n").command == Command::Status);
  });
  test("USB console applies SIM:VIN, replies, and does not count as a heartbeat", [] {
    ActuatorFsm fsm(60000);
    simTarget = &fsm;
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    HardwareSerial port;
    UartHandler console(port, fsm, acceptCommand, acceptSimulation);
    port.input = "SIM:VIN:4100\n";
    console.update(20000);
    CHECK(simulatedMv == 4100);
    CHECK(port.output.find("SIM:VIN:4100:OK\n") != std::string::npos);
    CHECK(fsm.state() == ActuatorState::ARMING);
    fsm.update(30000);
    CHECK(fsm.error() == ErrorCode::ControlTimeout);
  });
  test("USB console reports a rejected simulation", [] {
    ActuatorFsm fsm;
    simTarget = &fsm;
    fsm.completePost(true, 0);
    HardwareSerial port;
    UartHandler console(port, fsm, acceptCommand, rejectSimulation);
    port.input = "SIM:VIN:4100\n";
    console.update(1);
    CHECK(port.output.find("SIM:VIN:4100:REJECTED\n") != std::string::npos);
    CHECK(fsm.state() == ActuatorState::SAFE);
  });
}

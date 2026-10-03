#include "fsm.h"
#include "harness.h"

void runFsmHeartbeatTests() {
  test("PWM heartbeat keeps a five-minute arming alive", [] {
    ActuatorFsm fsm;
    fsm.completePost(true, 0);
    CHECK(fsm.handle(Command::Start, 0));
    for (uint32_t now = 20; now <= 300000; now += 20) fsm.heartbeat(now);
    CHECK(fsm.state() == ActuatorState::ARMED);
    CHECK(fsm.error() == ErrorCode::None);
  });
  test("a late heartbeat cannot clear an expired control deadline", [] {
    ActuatorFsm fsm;
    fsm.completePost(true, 0);
    CHECK(fsm.handle(Command::Start, 0));
    fsm.heartbeat(30000);
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.error() == ErrorCode::ControlTimeout);
  });
  test("new fault codes keep their wire values", [] {
    CHECK(static_cast<int>(ErrorCode::SettingsInvalid) == 6);
    CHECK(static_cast<int>(ErrorCode::ButtonStuck) == 7);
    CHECK(static_cast<int>(ErrorCode::PwmInvalid) == 8);
    CHECK(static_cast<int>(ErrorCode::PwmLost) == 9);
    CHECK(static_cast<int>(ErrorCode::PowerOutOfRange) == 10);
    CHECK(static_cast<int>(ErrorCode::SensorFailure) == 11);
  });
}

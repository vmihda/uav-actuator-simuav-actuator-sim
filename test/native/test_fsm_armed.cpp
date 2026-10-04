#include "fsm.h"
#include "harness.h"

namespace {
// Arms at 10 s; heartbeats every 20 ms keep the control deadline fresh.
ActuatorFsm armedFsm(uint32_t armedTimeoutMs) {
  ActuatorFsm fsm(10000, 30000, 3000);
  if (armedTimeoutMs > 0) fsm.setArmedTimeout(armedTimeoutMs);
  fsm.completePost(true, 0);
  fsm.handle(Command::Start, 0);
  for (uint32_t now = 20; now <= 10000; now += 20) fsm.heartbeat(now);
  return fsm;
}
}  // namespace

void runFsmArmedTests() {
  test("ARMED without contact returns to SAFE after the armed timeout", [] {
    ActuatorFsm fsm = armedFsm(5000);
    CHECK(fsm.state() == ActuatorState::ARMED);
    for (uint32_t now = 10020; now < 15000; now += 20) fsm.heartbeat(now);
    CHECK(fsm.state() == ActuatorState::ARMED);
    fsm.heartbeat(15000);
    CHECK(fsm.state() == ActuatorState::SAFE);
    CHECK(fsm.error() == ErrorCode::None);
  });
  test("the armed timeout is disabled unless configured", [] {
    ActuatorFsm fsm = armedFsm(0);
    for (uint32_t now = 10020; now <= 200000; now += 20) fsm.heartbeat(now);
    CHECK(fsm.state() == ActuatorState::ARMED);
  });
  test("DEPLOY before the armed timeout still actuates", [] {
    ActuatorFsm fsm = armedFsm(5000);
    CHECK(fsm.handle(Command::Deploy, 12000));
    fsm.update(20000);
    CHECK(fsm.state() == ActuatorState::ACTUATED);
  });
  test("the contact stuck fault keeps its wire value", [] {
    CHECK(static_cast<int>(ErrorCode::ContactStuck) == 12);
  });
}

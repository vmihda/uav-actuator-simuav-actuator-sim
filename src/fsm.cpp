#include "fsm.h"

ActuatorFsm::ActuatorFsm(uint32_t armingMs, uint32_t timeoutMs, uint32_t pulseMs)
    : armingMs_(armingMs), timeoutMs_(timeoutMs), pulseMs_(pulseMs) {}

void ActuatorFsm::completePost(bool healthy, uint32_t now, ErrorCode error) {
  if (state_ != ActuatorState::POST && state_ != ActuatorState::FAULT) return;
  healthy_ = healthy;
  if (!healthy) {
    fault(error, now);
  } else if (state_ == ActuatorState::POST) {
    transition(ActuatorState::SAFE, now);
  }
}

bool ActuatorFsm::handle(Command command, uint32_t now) {
  // Expired control deadlines take precedence over a late heartbeat or DEPLOY.
  update(now);
  if (command == Command::Invalid) {
    fault(ErrorCode::InvalidCommand, now);
    return false;
  }
  lastControlAt_ = now;
  switch (command) {
    case Command::Status:
      return true;
    case Command::Stop:
      if (state_ == ActuatorState::POST || !healthy_) return false;
      error_ = ErrorCode::None;
      transition(ActuatorState::SAFE, now);
      return true;
    case Command::Start:
      if (state_ != ActuatorState::SAFE) return false;
      transition(ActuatorState::ARMING, now);
      return true;
    case Command::Deploy:
      if (state_ != ActuatorState::ARMED) return false;
      ++deploymentCount_;
      transition(ActuatorState::ACTUATED, now);
      pulseActive_ = true;
      return true;
    case Command::Invalid:
      return false;
  }
  fault(ErrorCode::InvalidCommand, now);
  return false;
}

void ActuatorFsm::update(uint32_t now) {
  if ((state_ == ActuatorState::ARMING || state_ == ActuatorState::ARMED) &&
      static_cast<uint32_t>(now - lastControlAt_) >= timeoutMs_) {
    fault(ErrorCode::ControlTimeout, now);
    return;
  }
  if (state_ == ActuatorState::ARMING &&
      static_cast<uint32_t>(now - stateSince_) >= armingMs_) {
    transition(ActuatorState::ARMED, now);
  }
  if (state_ == ActuatorState::ARMED && armedTimeoutMs_ > 0 &&
      static_cast<uint32_t>(now - stateSince_) >= armedTimeoutMs_) {
    transition(ActuatorState::SAFE, now);
  }
  if (state_ == ActuatorState::ACTUATED && pulseActive_ &&
      static_cast<uint32_t>(now - stateSince_) >= pulseMs_) {
    pulseActive_ = false;
  }
}

void ActuatorFsm::heartbeat(uint32_t now) {
  // An expired deadline takes precedence over a late heartbeat, as in handle().
  update(now);
  lastControlAt_ = now;
}

void ActuatorFsm::fault(ErrorCode error, uint32_t now) {
  error_ = error == ErrorCode::None ? ErrorCode::SelfTestFailed : error;
  transition(ActuatorState::FAULT, now);
}

uint32_t ActuatorFsm::timeLeftSeconds(uint32_t now) const {
  if (state_ != ActuatorState::ARMING) return 0;
  const uint32_t elapsed = now - stateSince_;
  if (elapsed >= armingMs_) return 0;
  const uint32_t remaining = armingMs_ - elapsed;
  return remaining / 1000 + (remaining % 1000 != 0 ? 1 : 0);
}

const char* ActuatorFsm::stateName(ActuatorState state) {
  switch (state) {
    case ActuatorState::POST: return "POST";
    case ActuatorState::SAFE: return "SAFE";
    case ActuatorState::ARMING: return "ARMING";
    case ActuatorState::ARMED: return "ARMED";
    case ActuatorState::ACTUATED: return "ACTUATED";
    case ActuatorState::FAULT: return "FAULT";
  }
  return "FAULT";
}

void ActuatorFsm::transition(ActuatorState state, uint32_t now) {
  pulseActive_ = false;
  if (state_ != state) {
    state_ = state;
    stateSince_ = now;
  }
}

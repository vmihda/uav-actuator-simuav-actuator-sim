#pragma once

#include <cstdint>
#include "config.h"

enum class ActuatorState { POST, SAFE, ARMING, ARMED, ACTUATED, FAULT };
enum class Command { Start, Stop, Deploy, Status, Invalid };
enum class ErrorCode : uint8_t {
  None = 0,
  SelfTestFailed = 1,
  InvalidCommand = 2,
  ControlTimeout = 3,
  UartOverflow = 4,
  StorageFailure = 5,
  SettingsInvalid = 6,
  ButtonStuck = 7,
  PwmInvalid = 8,
  PwmLost = 9,
  PowerOutOfRange = 10,
  SensorFailure = 11,
  ContactStuck = 12
};

struct StatusSnapshot {
  ActuatorState state;
  ErrorCode error;
  uint32_t timeLeftSeconds;
  bool pulseActive;
  uint32_t deploymentCount;
};

class ActuatorFsm {
 public:
  explicit ActuatorFsm(uint32_t armingMs = config::kArmingDelayMs,
                       uint32_t timeoutMs = config::kControlTimeoutMs,
                       uint32_t pulseMs = config::kPulseDurationMs);
  void completePost(bool healthy, uint32_t now,
                    ErrorCode error = ErrorCode::SelfTestFailed);
  bool handle(Command command, uint32_t now);
  void update(uint32_t now);
  // Refreshes the control deadline without a command (valid PWM signal).
  void heartbeat(uint32_t now);
  // ARMED returns to SAFE after this long without a deployment; 0 disables it.
  void setArmedTimeout(uint32_t ms) { armedTimeoutMs_ = ms; }
  void fault(ErrorCode error, uint32_t now);
  ActuatorState state() const { return state_; }
  ErrorCode error() const { return error_; }
  uint32_t timeLeftSeconds(uint32_t now) const;
  bool pulseActive() const { return pulseActive_; }
  uint32_t deploymentCount() const { return deploymentCount_; }
  uint32_t stateSince() const { return stateSince_; }
  StatusSnapshot snapshot(uint32_t now) const {
    return {state_, error_, timeLeftSeconds(now), pulseActive_, deploymentCount_};
  }
  static const char* stateName(ActuatorState state);

 private:
  void transition(ActuatorState state, uint32_t now);
  ActuatorState state_ = ActuatorState::POST;
  ErrorCode error_ = ErrorCode::None;
  bool healthy_ = false;
  bool pulseActive_ = false;
  uint32_t stateSince_ = 0;
  uint32_t lastControlAt_ = 0;
  uint32_t deploymentCount_ = 0;
  uint32_t armingMs_;
  uint32_t timeoutMs_;
  uint32_t pulseMs_;
  uint32_t armedTimeoutMs_ = 0;
};

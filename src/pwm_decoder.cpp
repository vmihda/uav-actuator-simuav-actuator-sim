#include "pwm_decoder.h"

namespace {
constexpr PwmEvent kNoEvent{PwmEventKind::None, Command::Invalid};

bool inside(uint16_t widthUs, const PwmRange& range) {
  return widthUs >= range.minUs && widthUs <= range.maxUs;
}
}  // namespace

const char* pwmBandName(PwmBand band) {
  switch (band) {
    case PwmBand::None: return "NONE";
    case PwmBand::Invalid: return "INVALID";
    case PwmBand::Dead: return "DEAD";
    case PwmBand::Stop: return "STOP";
    case PwmBand::Start: return "START";
    case PwmBand::Deploy: return "DEPLOY";
  }
  return "NONE";
}

void PwmCommandDecoder::configure(const Settings& settings) {
  settings_ = settings;
  configured_ = true;
  release();
  havePulse_ = false;
  validRun_ = false;
  invalidRun_ = false;
  candidate_ = PwmBand::None;
  widthUs_ = 0;
}

PwmBand PwmCommandDecoder::classify(uint16_t widthUs) const {
  if (widthUs < settings_.pwmValidMinUs || widthUs > settings_.pwmValidMaxUs) return PwmBand::Invalid;
  if (inside(widthUs, settings_.stop)) return PwmBand::Stop;
  if (inside(widthUs, settings_.start)) return PwmBand::Start;
  if (inside(widthUs, settings_.deploy)) return PwmBand::Deploy;
  return PwmBand::Dead;
}

void PwmCommandDecoder::onPulse(uint16_t widthUs, uint32_t nowMs) {
  if (!configured_) return;
  havePulse_ = true;
  lastPulseAt_ = nowMs;
  widthUs_ = widthUs;
  const PwmBand band = classify(widthUs);
  if (band == PwmBand::Invalid) {
    // An isolated glitch is ignored; update() reports a persistent one.
    if (!invalidRun_) {
      invalidRun_ = true;
      invalidSince_ = nowMs;
    }
    return;
  }
  invalidRun_ = false;
  if (!validRun_) {
    validRun_ = true;
    validSince_ = nowMs;
  }
  if (band != candidate_) {
    candidate_ = band;
    candidateSince_ = nowMs;
  }
  if (captured_ && neutral_) heartbeatPending_ = true;
}

PwmEvent PwmCommandDecoder::update(uint32_t nowMs) {
  if (!configured_) return kNoEvent;
  const bool lost = havePulse_ &&
      static_cast<uint32_t>(nowMs - lastPulseAt_) >= settings_.pwmLossTimeoutMs;
  const bool invalid = invalidRun_ &&
      static_cast<uint32_t>(nowMs - invalidSince_) >= settings_.pwmLossTimeoutMs;
  if (lost || invalid) {
    const bool wasCaptured = captured_;
    release();
    validRun_ = false;
    invalidRun_ = false;
    candidate_ = PwmBand::None;
    if (lost) {
      havePulse_ = false;
      widthUs_ = 0;
    }
    if (!wasCaptured) return kNoEvent;
    return {lost ? PwmEventKind::Lost : PwmEventKind::Invalid, Command::Invalid};
  }
  // Capture and band stability are measured over received pulses, so a
  // signal that stops early can never mature into a command.
  if (!captured_ && validRun_ &&
      static_cast<uint32_t>(lastPulseAt_ - validSince_) >= settings_.pwmStableMs) {
    captured_ = true;
    firstBandPending_ = true;
  }
  if (!captured_ || candidate_ == PwmBand::None || candidate_ == stable_ ||
      static_cast<uint32_t>(lastPulseAt_ - candidateSince_) < settings_.pwmStableMs) return kNoEvent;
  stable_ = candidate_;
  if (stable_ == PwmBand::Dead) return kNoEvent;
  return enterCommandBand(stable_);
}

PwmEvent PwmCommandDecoder::enterCommandBand(PwmBand band) {
  if (firstBandPending_) {
    // The band present at capture is a starting position, not an operator action.
    firstBandPending_ = false;
    lastCommanded_ = band;
    neutral_ = band == PwmBand::Stop;
    return kNoEvent;
  }
  if (band == lastCommanded_) return kNoEvent;
  lastCommanded_ = band;
  if (band == PwmBand::Stop) {
    neutral_ = true;
    return {PwmEventKind::Command, Command::Stop};
  }
  if (!neutral_) return kNoEvent;
  return {PwmEventKind::Command, band == PwmBand::Start ? Command::Start : Command::Deploy};
}

void PwmCommandDecoder::onFault() {
  neutral_ = captured_ && lastCommanded_ == PwmBand::Stop;
  heartbeatPending_ = false;
}

bool PwmCommandDecoder::takeHeartbeat() {
  const bool due = heartbeatPending_;
  heartbeatPending_ = false;
  return due;
}

void PwmCommandDecoder::release() {
  captured_ = false;
  firstBandPending_ = false;
  neutral_ = false;
  lastCommanded_ = PwmBand::None;
  stable_ = PwmBand::None;
  heartbeatPending_ = false;
}

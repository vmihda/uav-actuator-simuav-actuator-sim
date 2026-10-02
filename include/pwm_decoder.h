#pragma once

#include <cstdint>
#include "fsm.h"
#include "settings.h"

enum class PwmBand : uint8_t { None, Invalid, Dead, Stop, Start, Deploy };
enum class PwmEventKind : uint8_t { None, Command, Lost, Invalid };

struct PwmEvent {
  PwmEventKind kind;
  Command command;  // Meaningful only for PwmEventKind::Command.
};

const char* pwmBandName(PwmBand band);

// Turns measured pulse widths into one-shot commands (spec rules 1-7).
class PwmCommandDecoder {
 public:
  void configure(const Settings& settings);
  void onPulse(uint16_t widthUs, uint32_t nowMs);
  PwmEvent update(uint32_t nowMs);
  // Called when the FSM enters FAULT.
  void onFault();
  // True once per valid pulse received while captured and neutral.
  bool takeHeartbeat();
  bool captured() const { return captured_; }
  bool neutral() const { return neutral_; }
  PwmBand band() const { return stable_; }
  uint16_t widthUs() const { return widthUs_; }

 private:
  PwmBand classify(uint16_t widthUs) const;
  PwmEvent enterCommandBand(PwmBand band);
  void release();

  Settings settings_{};
  bool configured_ = false;
  bool havePulse_ = false;
  uint32_t lastPulseAt_ = 0;
  bool validRun_ = false;
  uint32_t validSince_ = 0;
  bool invalidRun_ = false;
  uint32_t invalidSince_ = 0;
  PwmBand candidate_ = PwmBand::None;
  uint32_t candidateSince_ = 0;
  PwmBand stable_ = PwmBand::None;
  bool captured_ = false;
  bool firstBandPending_ = false;
  bool neutral_ = false;
  PwmBand lastCommanded_ = PwmBand::None;
  bool heartbeatPending_ = false;
  uint16_t widthUs_ = 0;
};

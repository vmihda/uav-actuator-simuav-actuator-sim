#include <vector>

#include "fixtures.h"
#include "harness.h"
#include "pwm_decoder.h"

namespace {
// Drives the decoder like the firmware: one 50 Hz pulse and one update per 20 ms.
struct PwmRun {
  explicit PwmRun(uint32_t start = 0) : now(start) { decoder.configure(defaultSettings()); }
  void hold(uint16_t widthUs, uint32_t durationMs) {
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 20) {
      decoder.onPulse(widthUs, now);
      record(decoder.update(now));
      now += 20;
    }
  }
  void silence(uint32_t durationMs) {
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 10) {
      record(decoder.update(now));
      now += 10;
    }
  }
  void record(const PwmEvent& event) {
    if (event.kind != PwmEventKind::None) events.push_back(event);
  }
  bool onlyCommands(std::initializer_list<Command> expected) const {
    if (events.size() != expected.size()) return false;
    std::size_t i = 0;
    for (Command command : expected) {
      if (events[i].kind != PwmEventKind::Command || events[i].command != command) return false;
      ++i;
    }
    return true;
  }
  PwmCommandDecoder decoder;
  uint32_t now;
  std::vector<PwmEvent> events;
};
}  // namespace

void runPwmDecoderTests() {
  test("PWM capture needs a stable valid signal and the first band is silent", [] {
    PwmRun run;
    run.hold(1000, 180);
    CHECK(!run.decoder.captured());
    run.hold(1000, 200);
    CHECK(run.decoder.captured());
    CHECK(run.decoder.neutral());
    CHECK(run.decoder.band() == PwmBand::Stop);
    CHECK(run.events.empty());
  });
  test("START after neutral is emitted once despite holding and edge jitter", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.hold(1500, 1000);
    CHECK(run.onlyCommands({Command::Start}));
    for (int i = 0; i < 50; ++i) run.hold(i % 2 ? 1399 : 1401, 20);
    run.hold(1500, 400);
    CHECK(run.onlyCommands({Command::Start}));
  });
  test("band edges are inclusive", [] {
    PwmRun run;
    run.hold(1300, 400);
    CHECK(run.decoder.band() == PwmBand::Stop && run.decoder.neutral());
    run.hold(1400, 400);
    CHECK(run.decoder.band() == PwmBand::Start);
    run.hold(2100, 400);
    CHECK(run.decoder.band() == PwmBand::Deploy);
    CHECK(run.onlyCommands({Command::Start, Command::Deploy}));
    run.hold(2101, 400);
    CHECK(run.decoder.band() == PwmBand::Dead);
    run.hold(1301, 400);
    CHECK(run.decoder.band() == PwmBand::Dead);
  });
  test("START and DEPLOY need neutral; STOP always commands and establishes it", [] {
    PwmRun run;
    run.hold(1500, 400);
    CHECK(run.decoder.captured() && !run.decoder.neutral());
    run.hold(2000, 400);
    CHECK(run.events.empty());
    run.hold(1000, 400);
    CHECK(run.decoder.neutral());
    run.hold(1500, 400);
    CHECK(run.onlyCommands({Command::Stop, Command::Start}));
  });
  test("a premature DEPLOY is emitted once and then consumed", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.hold(1500, 400);
    run.hold(2000, 3000);
    CHECK(run.onlyCommands({Command::Start, Command::Deploy}));
  });
  test("a band held shorter than stable_ms never commands", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.hold(1500, 160);
    run.hold(1000, 400);
    run.hold(2000, 160);
    run.hold(1000, 400);
    CHECK(run.events.empty());
  });
  test("pulses that stop before stable_ms neither capture nor command", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.hold(1500, 20);
    run.silence(300);
    CHECK(run.events.empty());
    PwmRun fresh;
    fresh.hold(1000, 100);
    fresh.silence(300);
    CHECK(!fresh.decoder.captured());
  });
  test("heartbeat is due only while captured and neutral", [] {
    PwmRun run;
    run.hold(1500, 400);
    CHECK(!run.decoder.takeHeartbeat());
    run.hold(1000, 400);
    CHECK(run.decoder.takeHeartbeat());
    CHECK(!run.decoder.takeHeartbeat());
    run.hold(1500, 20);
    CHECK(run.decoder.takeHeartbeat());
  });
  test("signal loss after capture emits Lost; before capture it is silent", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.silence(470);
    CHECK(run.events.empty() && run.decoder.captured());
    run.silence(20);
    CHECK(run.events.size() == 1 && run.events[0].kind == PwmEventKind::Lost);
    CHECK(!run.decoder.captured() && !run.decoder.neutral());
    PwmRun early;
    early.hold(1000, 100);
    early.silence(600);
    CHECK(early.events.empty());
  });
  test("a persistent invalid width emits Invalid; an isolated glitch is ignored", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.hold(2500, 20);
    run.hold(1000, 400);
    CHECK(run.events.empty() && run.decoder.captured() && run.decoder.neutral());
    run.hold(2500, 600);
    CHECK(run.events.size() == 1 && run.events[0].kind == PwmEventKind::Invalid);
    CHECK(!run.decoder.captured());
  });
  test("recapture after loss starts silent again", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.silence(600);
    run.events.clear();
    run.hold(1500, 400);
    CHECK(run.decoder.captured() && !run.decoder.neutral());
    CHECK(run.events.empty());
  });
  test("FAULT keeps neutral only while the last commanded band is STOP", [] {
    PwmRun run;
    run.hold(1000, 400);
    run.decoder.onFault();
    CHECK(run.decoder.neutral());
    run.hold(1500, 400);
    run.decoder.onFault();
    CHECK(!run.decoder.neutral());
    run.hold(2000, 400);
    run.hold(1000, 400);
    CHECK(run.onlyCommands({Command::Start, Command::Stop}));
    CHECK(run.decoder.neutral());
  });
  test("PWM timing survives millis rollover", [] {
    PwmRun run(UINT32_MAX - 300);
    run.hold(1000, 400);
    CHECK(run.decoder.captured());
    run.hold(1500, 400);
    CHECK(run.onlyCommands({Command::Start}));
    run.silence(600);
    CHECK(run.events.size() == 2 && run.events[1].kind == PwmEventKind::Lost);
  });
}

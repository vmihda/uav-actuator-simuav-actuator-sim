#include "button_monitor.h"
#include "harness.h"

void runButtonMonitorTests() {
  test("button bounce shorter than debounce_ms is ignored", [] {
    ButtonMonitor button;
    button.configure(50, 500);
    for (uint32_t now = 0; now < 200; now += 10) CHECK(!button.update((now / 10) % 2 == 0, now));
    CHECK(!button.pressed());
  });
  test("a debounced press is reported once", [] {
    ButtonMonitor button;
    button.configure(50, 500);
    int presses = 0;
    for (uint32_t now = 0; now <= 300; now += 10) presses += button.update(true, now) ? 1 : 0;
    CHECK(presses == 1 && button.pressed());
    for (uint32_t now = 310; now <= 400; now += 10) button.update(false, now);
    CHECK(!button.pressed());
    for (uint32_t now = 410; now <= 500; now += 10) presses += button.update(true, now) ? 1 : 0;
    CHECK(presses == 2);
  });
  test("a button held for stuck_ms is stuck; a shorter press is not", [] {
    ButtonMonitor button;
    button.configure(50, 500);
    for (uint32_t now = 0; now <= 400; now += 10) button.update(true, now);
    CHECK(!button.stuck(400));
    for (uint32_t now = 410; now <= 600; now += 10) button.update(false, now);
    CHECK(!button.stuck(600));
    for (uint32_t now = 610; now <= 1110; now += 10) button.update(true, now);
    CHECK(button.stuck(1110));
  });
  test("button timing survives millis rollover", [] {
    ButtonMonitor button;
    button.configure(50, 500);
    const uint32_t start = UINT32_MAX - 100;
    bool pressed = false;
    for (uint32_t step = 0; step <= 60; ++step) pressed = button.update(true, start + step * 10) || pressed;
    CHECK(pressed);
    CHECK(button.stuck(start + 600));
  });
}

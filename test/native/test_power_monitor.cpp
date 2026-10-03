#include "harness.h"
#include "power_monitor.h"
#include "simulated_sensor.h"

void runPowerMonitorTests() {
  test("simulated sensor reports its value and can be invalidated", [] {
    SimulatedSensor sensor("supply_mv", 5000);
    CHECK(sensor.isSimulated());
    CHECK(sensor.read(0).valid && sensor.read(0).value == 5000);
    sensor.setInvalid();
    CHECK(!sensor.read(0).valid);
    sensor.set(4200);
    CHECK(sensor.read(0).valid && sensor.read(0).value == 4200);
  });
  test("power verdict needs a persistent excursion", [] {
    SimulatedSensor sensor("supply_mv", 5000);
    PowerMonitor monitor;
    monitor.configure(&sensor, 4500, 5500, 1000);
    CHECK(monitor.update(0) == PowerVerdict::Ok);
    sensor.set(4000);
    CHECK(monitor.update(100) == PowerVerdict::Ok);
    CHECK(monitor.instant() == PowerVerdict::OutOfRange);
    CHECK(monitor.update(1099) == PowerVerdict::Ok);
    CHECK(monitor.update(1100) == PowerVerdict::OutOfRange);
    sensor.set(5000);
    CHECK(monitor.update(1200) == PowerVerdict::Ok);
    CHECK(monitor.instant() == PowerVerdict::Ok);
  });
  test("a brief dip below stable_ms is ignored", [] {
    SimulatedSensor sensor("supply_mv", 5000);
    PowerMonitor monitor;
    monitor.configure(&sensor, 4500, 5500, 1000);
    monitor.update(0);
    sensor.set(4400);
    for (uint32_t now = 10; now < 1000; now += 10) CHECK(monitor.update(now) == PowerVerdict::Ok);
    sensor.set(5000);
    CHECK(monitor.update(1500) == PowerVerdict::Ok);
  });
  test("power bounds are inclusive", [] {
    SimulatedSensor sensor("supply_mv", 4500);
    PowerMonitor monitor;
    monitor.configure(&sensor, 4500, 5500, 100);
    monitor.update(0);
    CHECK(monitor.update(200) == PowerVerdict::Ok);
    sensor.set(5500);
    CHECK(monitor.update(400) == PowerVerdict::Ok);
    sensor.set(4499);
    monitor.update(500);
    CHECK(monitor.update(600) == PowerVerdict::OutOfRange);
  });
  test("invalid readings and a missing sensor report SensorFailure", [] {
    SimulatedSensor sensor("supply_mv", 5000);
    PowerMonitor monitor;
    monitor.configure(&sensor, 4500, 5500, 1000);
    monitor.update(0);
    sensor.setInvalid();
    CHECK(monitor.update(10) == PowerVerdict::Ok);
    CHECK(monitor.instant() == PowerVerdict::SensorFailure);
    CHECK(monitor.update(1010) == PowerVerdict::SensorFailure);
    PowerMonitor missing;
    missing.configure(nullptr, 4500, 5500, 1000);
    missing.update(0);
    CHECK(missing.update(1000) == PowerVerdict::SensorFailure);
  });
  test("power persistence survives millis rollover", [] {
    SimulatedSensor sensor("supply_mv", 5000);
    PowerMonitor monitor;
    monitor.configure(&sensor, 4500, 5500, 1000);
    const uint32_t start = UINT32_MAX - 500;
    monitor.update(start);
    sensor.set(6000);
    monitor.update(start + 10);
    CHECK(monitor.update(start + 1009) == PowerVerdict::Ok);
    CHECK(monitor.update(start + 1010) == PowerVerdict::OutOfRange);
  });
}

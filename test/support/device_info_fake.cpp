#include "device_info.h"

// Host fake: a healthy ESP32-D0WD-V3 with 4 MB flash.
DeviceInfo fakeDeviceInfo{true, {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03}, true, "ESP32-D0WD-V3", 3,
                          4UL * 1024 * 1024, 200000};

DeviceInfo readDeviceInfo() { return fakeDeviceInfo; }

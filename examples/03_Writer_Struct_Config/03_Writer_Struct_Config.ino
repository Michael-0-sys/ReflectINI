#include "ReflectINI_writer.h"
#include <optional>

// A normal C++ struct can be serialized automatically by adding REFLECT().
struct DeviceData {
  int32_t id = 7;
  float temperature = 23.456f;
  float batteryVoltage = 41.287f;
  bool online = true;
  int16_t samples[3] = {120, 121, 119};
  std::optional<int32_t> errorCode = std::nullopt;
  const char* note = nullptr;

  REFLECT(
    MEMBER(id),

    // fn(value, precision) writes a floating-point value with a chosen precision.
    MEMBER(temperature, fn(temperature, 1)),

    // MEMBER_AS changes the INI key.
    // fx(value, precision) stores a float as an integer scaled by 10^precision.
    MEMBER_AS("battery_mV", fx(batteryVoltage, 3)),

    MEMBER(online),
    MEMBER(samples),
    MEMBER(errorCode),
    MEMBER(note)
  )
};

void printResult(const char* title, INIwriter &writer) {
  Serial.println(title);
  if(writer.hasError()) {
    Serial.print("Error: ");
    Serial.println(writer.getErrorMsg());
  } else {
    Serial.println(writer.getINI());
  }
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  DeviceData data;
  INIwriter writer(768);

  // A reflected struct with a key becomes a section.
  // Empty std::optional values and nullptr strings are omitted.
  writer.write(data, "device");
  printResult("Default IniConfig:", writer);

  // resetINI() reuses the same writer and its already allocated buffers.
  writer.resetINI();

  // Writer formatting is controlled by IniConfig.
  IniConfig compact;
  compact.spaceAroundEquals = false;
  compact.blankLineBeforeSection = false;
  writer.setConfig(compact);

  writer.write(1, "version");
  writer.write(data, "device");
  printResult("Compact formatting:", writer);
}

void loop() {}

/*
Serial output:

Default IniConfig:
[device]
id = 7
temperature = 23.5
battery_mV = 41287
online = true
samples = 120, 121, 119


Compact formatting:
version=1
[device]
id=7
temperature=23.5
battery_mV=41287
online=true
samples=120, 121, 119
*/

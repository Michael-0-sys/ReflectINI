#include "ReflectINI_reader.h"
#include <optional>

// REFLECT() allows the Reader to map an INI section directly into a C++ struct.
struct DeviceData {
  int32_t id = 99;
  float temperature = 20.0f;
  float batteryVoltage = 0.0f;
  bool online = false;
  int16_t samples[3] = {0, 0, 0};
  std::optional<int32_t> errorCode = std::nullopt;
  const char* note = nullptr;

  REFLECT(
    MEMBER(id),
    MEMBER(temperature, fn(temperature, 1)),
    MEMBER_AS("battery_mV", fx(batteryVoltage, 3)),
    MEMBER(online),
    MEMBER(samples),
    MEMBER(errorCode),
    MEMBER(note)
  )
};

void printDevice(const DeviceData &d) {
  Serial.print("id          = "); Serial.println(d.id);
  Serial.print("temperature = "); Serial.println(d.temperature);
  Serial.print("battery     = "); Serial.println(d.batteryVoltage, 3);
  Serial.print("online      = "); Serial.println(d.online);
  Serial.print("samples     = ");
  Serial.print(d.samples[0]); Serial.print(", ");
  Serial.print(d.samples[1]); Serial.print(", ");
  Serial.println(d.samples[2]);
  Serial.print("errorCode   = ");
  if(d.errorCode.has_value()) Serial.println(*d.errorCode); else Serial.println("null");
  Serial.print("note        = "); Serial.println(d.note ? d.note : "null");
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // "note" and "errorCode" are missing and "futureField" is unknown.
  // The default IniConfig accepts both situations.
  const char* ini =
    "[device]\n"
    "id = 7\n"
    "temperature = 23.4\n"
    "battery_mV = 41287\n"
    "online = true\n"
    "samples = 120, 121, 119\n"
    "futureField = 123\n";

  INIreader reader(32, 8);
  DeviceData data;

  if(!reader.parse(ini) || !reader.read(data, "device")) {
    Serial.print("Error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.println("Default IniConfig:");
  printDevice(data);

  // strict() requires all non-nullable reflected members and rejects unknown keys/sections.
  reader.setConfig(IniConfig::strict());
  DeviceData strictData;

  Serial.println();
  Serial.println("Strict IniConfig:");
  if(!reader.parse(ini) || !reader.read(strictData, "device")) {
    Serial.print("Expected error: ");
    Serial.println(reader.getErrorMsg());
  } else {
    printDevice(strictData);
  }
}

void loop() {}

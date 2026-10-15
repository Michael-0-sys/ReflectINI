#include "ReflectINI_writer.h"
#include <optional>

// This example combines most Writer features in one sketch.

enum class DeviceState : uint8_t {
  Off = 0,
  Ready = 1,
  Error = 2
};

struct Calibration {
  float offset = -0.1254f;
  float scale = 1.00234f;

  REFLECT(
    MEMBER(offset, fn(offset, 3)),
    MEMBER(scale, fn(scale, 4))
  )
};

struct Sensor {
  uint8_t id = 0;
  float value = 0.0f;

  REFLECT(
    MEMBER(id),
    MEMBER(value, fn(value, 2))
  )
};

struct Telemetry {
  uint32_t uptime = 123456;
  DeviceState state = DeviceState::Ready;
  char channel = 'A';
  float temperature = 22.876f;
  float voltage = 41.275f;
  double distance = 1234.56789;
  bool charging = false;
  const char* text = "quotes: \"hello\", slash: \\";
  const char* nullableText = nullptr;
  std::optional<int32_t> errorCode = std::nullopt;
  int16_t samples[4] = {101, 104, 99, 102};
  Sensor sensors[2] = {{1, 12.345f}, {2, 67.891f}};
  Calibration calibration;

  REFLECT(
    MEMBER(uptime),
    MEMBER(state),
    MEMBER(channel),
    MEMBER(temperature, fn(temperature, 2)),

    // voltage is represented as an integer number of millivolts in the INI.
    MEMBER_AS("voltage_mV", fx(voltage, 3)),

    MEMBER(distance, fn(distance, 3)),
    MEMBER(charging),
    MEMBER(text),
    MEMBER(nullableText),
    MEMBER(errorCode),
    MEMBER(samples),
    MEMBER(sensors),
    MEMBER(calibration)
  )
};

void printWriter(INIwriter &writer) {
  if(writer.hasError()) {
    Serial.print("Writer error code: ");
    Serial.println((int)writer.getErrorCode());
    Serial.print("Writer error: ");
    Serial.println(writer.getErrorMsg());
    Serial.print("Writer path: ");
    Serial.println(writer.getErrorPath() ? writer.getErrorPath() : "<none>");
    return;
  }

  Serial.println(writer.getINI());
  Serial.print("INI bytes: ");
  Serial.println(writer.getINISize());
  Serial.print("Keys: ");
  Serial.println(writer.getKeyCount());
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  Telemetry data;
  INIwriter writer(2048, 64);

  writer.comment("ReflectINI maximum Writer example");
  writer.write(1, "formatVersion");

  // A reflected struct with a key becomes [telemetry].
  // Nested structs become [telemetry.calibration], arrays of structs become
  // [telemetry.sensors.0], [telemetry.sensors.1], ...
  writer.write(data, "telemetry");

  // Manual sections can be mixed with reflected structs.
  writer.startSection("meta");
  writer.write("1.0", "protocol");
  writer.write(20261014u, "build");

  // Arduino String is supported directly when compiling for Arduino.
  String boardName = "Pico 2 W";
  writer.write(boardName, "board");

  printWriter(writer);

  // The same writer can be reused without reallocating its internal buffers.
  Serial.println();
  Serial.println("Reused writer:");

  writer.resetINI();
  writer.write(999, "counter");
  writer.write("second document", "text");
  printWriter(writer);
}

void loop() {}

#include "ReflectINI_reader.h"
#include <optional>

// This example combines most Reader features in one sketch.

enum class DeviceState : uint8_t {
  Off = 0,
  Ready = 1,
  Error = 2
};

struct Calibration {
  float offset = 0;
  float scale = 1;

  REFLECT(
    MEMBER(offset, fn(offset, 3)),
    MEMBER(scale, fn(scale, 4))
  )
};

struct Sensor {
  uint8_t id = 0;
  float value = 0;

  REFLECT(
    MEMBER(id),
    MEMBER(value, fn(value, 2))
  )
};

struct Telemetry {
  uint32_t uptime = 0;
  DeviceState state = DeviceState::Off;
  char channel = '\0';
  float temperature = 0;
  float voltage = 0;
  double distance = 0;
  bool charging = false;
  const char* text = nullptr;
  const char* nullableText = nullptr;
  std::optional<int32_t> errorCode = 123;
  int16_t samples[4] = {0, 0, 0, 0};
  Sensor sensors[2];
  Calibration calibration;

  REFLECT(
    MEMBER(uptime),
    MEMBER(state),
    MEMBER(channel),
    MEMBER(temperature, fn(temperature, 2)),
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

void printReaderError(INIreader &reader) {
  Serial.print("Reader error code: ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Reader error: ");
  Serial.println(reader.getErrorMsg());
  Serial.print("Reader path: ");
  Serial.println(reader.getErrorPath() ? reader.getErrorPath() : "<none>");
  Serial.print("Reader line: ");
  Serial.println(reader.getErrorLine());
  Serial.print("Reader column: ");
  Serial.println(reader.getErrorColumn());
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // This buffer is writable. With parse(ini, true), unescaped const char* values
  // may be returned as zero-copy pointers directly into this buffer.
  char ini[] =
    "[telemetry]\n"
    "uptime = 123456\n"
    "state = 1\n"
    "channel = A\n"
    "temperature = 22.88\n"
    "voltage_mV = 41275\n"
    "distance = 1234.568\n"
    "charging = false\n"
    "text = \"hello # still text\"\n"
    "errorCode =\n"
    "samples = 101, 104, 99, 102\n"
    "\n"
    "[telemetry.sensors.0]\n"
    "id = 1\n"
    "value = 12.35\n"
    "\n"
    "[telemetry.sensors.1]\n"
    "id = 2\n"
    "value = 67.89\n"
    "\n"
    "[telemetry.calibration]\n"
    "offset = -0.125\n"
    "scale = 1.0023\n";

  INIreader reader(32, 8);

  // The second argument tells the Reader that the input buffer may be modified internally.
  if(!reader.parse(ini, true)) {
    printReaderError(reader);
    return;
  }

  Serial.print("Entries:  "); Serial.println(reader.getEntryCount());
  Serial.print("Sections: "); Serial.println((int)reader.getSectionCount());

  Telemetry data;
  if(!reader.read(data, "telemetry")) {
    printReaderError(reader);
    return;
  }

  Serial.println();
  Serial.println("Decoded struct:");
  Serial.print("uptime       = "); Serial.println(data.uptime);
  Serial.print("state        = "); Serial.println((int)data.state);
  Serial.print("channel      = "); Serial.println(data.channel);
  Serial.print("temperature  = "); Serial.println(data.temperature, 2);
  Serial.print("voltage      = "); Serial.println(data.voltage, 3);
  Serial.print("distance     = "); Serial.println(data.distance, 3);
  Serial.print("text         = "); Serial.println(data.text ? data.text : "null");
  Serial.print("nullableText = "); Serial.println(data.nullableText ? data.nullableText : "null");
  Serial.print("errorCode    = "); Serial.println(data.errorCode.has_value() ? "value" : "null");
  Serial.print("sensor[1]    = "); Serial.println(data.sensors[1].value, 2);

  // Individual values can be read directly from a section.
  float scale = 0;
  if(reader.read(scale, "scale", "telemetry.calibration")) {
    Serial.print("scale        = ");
    Serial.println(scale, 4);
  }

  // By default every read verifies that the original INI buffer is unchanged.
  // This can be disabled for extra speed when your application guarantees that the buffer is stable.
  reader.setBufferCheck(false);
  Serial.print("Buffer check = ");
  Serial.println(reader.isBufferCheckEnabled() ? "on" : "off");
}

void loop() {}

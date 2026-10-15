#include "ReflectINI_reader.h"

struct Telemetry {
  float speed = 0;
  int32_t power = 0;

  REFLECT(
    MEMBER(speed),
    MEMBER(power)
  )
};

void printError(INIreader &reader) {
  Serial.print("Error code: ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Message: ");
  Serial.println(reader.getErrorMsg());
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  const char* ini =
    "[legacy]\n"
    "power = unknown\n"
    "\n"
    "[bike.telemetry]\n"
    "speed = 18.6\n"
    "power = 650\n"
    "\n"
    "[bike.settings]\n"
    "speedLimit = 25\n";

  INIreader reader(32, 8);
  if(!reader.parse(ini)) {
    printError(reader);
    return;
  }

  float speed = 0;
  int32_t limit = 0;

  // The third argument is the complete section name.
  if(!reader.read(speed, "speed", "bike.telemetry")) {
    printError(reader);
    return;
  }

  if(!reader.read(limit, "speedLimit", "bike.settings")) {
    printError(reader);
    return;
  }

  // A reflected struct can also be read from a nested section.
  // section="bike", key="telemetry" resolves to [bike.telemetry].
  Telemetry telemetry;
  if(!reader.read(telemetry, "telemetry", "bike")) {
    printError(reader);
    return;
  }

  Serial.print("speed      = "); Serial.println(speed);
  Serial.print("speedLimit = "); Serial.println(limit);
  Serial.print("power      = "); Serial.println(telemetry.power);
}

void loop() {}

/*
Serial output:
speed      = 18.60
speedLimit = 25
power      = 650
*/

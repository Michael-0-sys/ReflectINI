#include "ReflectINI_writer.h"

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  INIwriter writer(768);

  // Global keys must be written before the first section.
  writer.write(1, "version");

  // startSection(name) selects a section. Nested names use dots.
  writer.startSection("device");
  writer.write("Pico 2 W", "name");
  writer.write(3, "id");

  // Scalar arrays are written as comma-separated values.
  int samples[3] = {101, 102, 103};
  writer.write(samples, "samples");

  writer.startSection("device.status");
  writer.write(true, "online");
  writer.write(41.28f, "voltage");

  // Arrays of objects are represented by numbered sections.
  writer.startSection("events.0");
  writer.write(1, "id");
  writer.write("boot", "name");

  writer.startSection("events.1");
  writer.write(2, "id");
  writer.write("ready", "name");

  if(writer.hasError()) {
    Serial.print("INI error code: ");
    Serial.println((int)writer.getErrorCode());
    Serial.print("Message: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  Serial.println(writer.getINI());
}

void loop() {}

/*
Serial output:
version = 1

[device]
name = Pico 2 W
id = 3
samples = 101, 102, 103

[device.status]
online = true
voltage = 41.28

[events.0]
id = 1
name = boot

[events.1]
id = 2
name = ready
*/

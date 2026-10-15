#include "ReflectINI_writer.h"

void setup() {
  // Start the serial monitor. The short delay is especially useful on RP2040/RP2350 boards,
  // because the USB serial connection may not be ready immediately after reset.
  Serial.begin(115200);
  delay(1000);

  // Create a writer with a 256 byte output buffer.
  INIwriter writer(256);

  // Add primitive values to the global section.
  writer.write(42, "answer");
  writer.write(true, "ready");
  writer.write(23.75f, "temperature");
  writer.write("hello", "text");

  // INI needs no finish(): getINI() is valid after every successful write().
  if(writer.hasError()) {
    Serial.print("INI error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  // getINI() returns the zero-terminated INI string.
  // getINISize() returns its length without the terminating '\0'.
  Serial.println(writer.getINI());
  Serial.print("INI bytes: ");
  Serial.println(writer.getINISize());
}

void loop() {}

/*
Serial output:
answer = 42
ready = true
temperature = 23.75
text = hello

INI bytes: 58
*/

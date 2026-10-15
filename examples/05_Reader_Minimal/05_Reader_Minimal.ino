#include "ReflectINI_reader.h"

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  // The Reader accepts a zero-terminated INI string.
  const char* ini =
    "answer = 42\n"
    "ready = true\n"
    "temperature = 23.75\n"
    "text = hello\n";

  // Reserve space for up to 32 key/value entries and 8 sections.
  INIreader reader(32, 8);

  // parse() validates and indexes the complete INI document.
  if(!reader.parse(ini)) {
    Serial.print("Parse error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  int32_t answer = 0;
  bool ready = false;
  float temperature = 0;
  const char* text = nullptr;

  // read(value, key) reads a value from the global section.
  if(!reader.read(answer, "answer") ||
     !reader.read(ready, "ready") ||
     !reader.read(temperature, "temperature") ||
     !reader.read(text, "text")) {
    Serial.print("Read error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.print("answer      = "); Serial.println(answer);
  Serial.print("ready       = "); Serial.println(ready);
  Serial.print("temperature = "); Serial.println(temperature);
  Serial.print("text        = "); Serial.println(text);
}

void loop() {}

/*
Serial output:
answer      = 42
ready       = 1
temperature = 23.75
text        = hello
*/

#include "ReflectINI_writer.h"

struct Status {
  float speed = 18.64f;
  uint8_t assist = 3;
  bool light = true;

  REFLECT(
    MEMBER(speed, fn(speed, 1)),
    MEMBER(assist),
    MEMBER(light)
  )
};

void printDocument(const char* title, INIwriter &writer) {
  Serial.println(title);
  if(writer.hasError()) {
    Serial.print("Writer error: ");
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

  Status status;
  INIwriter writer(768);

  // 1) A reflected struct WITH a key becomes a section.
  writer.write(status, "status");
  printDocument("1. Struct with a key:", writer);

  writer.resetINI();

  // 2) A reflected struct WITHOUT a key is flattened into the current section.
  writer.write(status);
  printDocument("2. Struct without a key (flattened):", writer);

  writer.resetINI();

  // 3) Flattening can be mixed with normal keys in the global section.
  writer.write(42, "id");
  writer.write(status);
  writer.write("ok", "state");
  printDocument("3. Flattened into the global section:", writer);

  writer.resetINI();

  // 4) The same works inside a manually selected section.
  writer.startSection("device");
  writer.write(42, "id");
  writer.write(status);
  printDocument("4. Flattened into [device]:", writer);

  writer.resetINI();

  // 5) An array of reflected structs becomes numbered sections automatically.
  Status history[2];
  history[1].assist = 4;
  history[1].speed = 21.23f;
  writer.write(history, "history");
  printDocument("5. Array of structs:", writer);

  writer.resetINI();

  // 6) Flattened members share the current section's key namespace.
  writer.write(99.0f, "speed");
  writer.write(status);
  printDocument("6. Duplicate key while flattening:", writer);
}

void loop() {}

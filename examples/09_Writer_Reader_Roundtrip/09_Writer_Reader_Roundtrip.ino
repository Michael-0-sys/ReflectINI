#include "ReflectINI.h"
#include <optional>

// The same reflected struct can be used by both INIwriter and INIreader.
struct BikeStatus {
  uint32_t uptime = 0;
  float speed = 0;
  float voltage = 0;
  uint8_t assist = 0;
  bool light = false;
  std::optional<int32_t> error = std::nullopt;

  REFLECT(
    MEMBER(uptime),
    MEMBER(speed, fn(speed, 1)),
    MEMBER_AS("voltage_mV", fx(voltage, 3)),
    MEMBER(assist),
    MEMBER(light),
    MEMBER(error)
  )
};

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  BikeStatus tx;
  tx.uptime = 12345;
  tx.speed = 18.64f;
  tx.voltage = 41.287f;
  tx.assist = 3;
  tx.light = true;

  // Serialize the complete struct into an INI section.
  INIwriter writer(512);
  writer.write(tx, "status");

  if(writer.hasError()) {
    Serial.print("Writer error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  Serial.println("TX INI:");
  Serial.println(writer.getINI());

  // Parse the generated INI again.
  INIreader reader(32, 8);
  if(!reader.parse(writer.getINI())) {
    Serial.print("Parser error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  // Decode it back into another instance of the same struct.
  BikeStatus rx;
  if(!reader.read(rx, "status")) {
    Serial.print("Reader error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.println();
  Serial.println("RX values:");
  Serial.print("uptime  = "); Serial.println(rx.uptime);
  Serial.print("speed   = "); Serial.println(rx.speed, 1);
  Serial.print("voltage = "); Serial.println(rx.voltage, 3);
  Serial.print("assist  = "); Serial.println((int)rx.assist);
  Serial.print("light   = "); Serial.println(rx.light);
  Serial.print("error   = "); Serial.println(rx.error.has_value() ? "value" : "null");
}

void loop() {}

/*
TX INI:
[status]
uptime = 12345
speed = 18.6
voltage_mV = 41287
assist = 3
light = true

RX values:
uptime  = 12345
speed   = 18.6
voltage = 41.287
assist  = 3
light   = 1
error   = null
*/

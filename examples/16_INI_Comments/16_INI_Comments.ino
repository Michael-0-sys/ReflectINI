/*
  ReflectINI example: manual comments and COMMENT() inside REFLECT()

  This example shows two different ways to add comments to an INI file:

    1. INIwriter::comment()
       Writes a free-standing comment directly at the current output position.
       Useful for file headers, warnings or generated-file information.

    2. COMMENT("...")
       Used inside REFLECT().
       The comment belongs to the next MEMBER()/MEMBER_AS() entry and is written
       directly before that key or section.

  COMMENT() is only used by ReflectINI:
    - INIwriter writes it
    - INIreader ignores it
    - ReflectJSON ignores it
    - ReflectObserver ignores it

  If the following member is omitted by the INI writer, for example an empty
  std::optional or a nullptr string, its COMMENT() is omitted as well.
*/

#include "ReflectINI.h"
#include <optional>

struct Calibration {
  uint32_t offset = 120;
  float factor = 1.025f;

  // Empty optionals are not written to INI.
  // The COMMENT() directly before this member is therefore omitted too.
  std::optional<int32_t> serviceOffset = std::nullopt;

  REFLECT(
    COMMENT("Raw sensor offset used during calibration"),
    MEMBER(offset),

    COMMENT("Calibration multiplier"),
    MEMBER(factor, fn(factor, 3)),

    COMMENT("Optional service-only offset"),
    MEMBER(serviceOffset)
  )
};

struct BikeConfig {
  bool lightEnabled = true;
  uint8_t assistLevel = 3;
  char bikeName[24] = "Adventure 2";
  Calibration calibration;

  REFLECT(
    COMMENT("Default state of the bicycle light"),
    MEMBER(lightEnabled),

    COMMENT("Initial assist level after startup"),
    MEMBER(assistLevel),

    COMMENT("Display name of the bicycle"),
    MEMBER(bikeName),

    // Because Calibration is a reflected struct, it becomes its own [calibration]
    // section. This comment is written directly before that section header.
    COMMENT("Sensor calibration settings"),
    MEMBER(calibration)
  )
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  BikeConfig config;

  // bufferSize = 1024:
  //   Maximum size of the generated INI text, including comments.
  //
  // maxKeys = 16:
  //   Maximum number of "key = value" entries in the complete document.
  //   Comments and section headers do not count as keys.
  INIwriter writer(1024, 16);

  // comment() writes a normal free-standing INI comment.
  // It is not connected to any reflected member.
  writer.comment("Bike configuration file");
  writer.comment("Generated automatically - edit values if required");

  // Add an empty line between the file header and the reflected configuration.
  writer.blankLine();

  // write(config) writes the complete reflected structure.
  // COMMENT() entries inside REFLECT() are emitted automatically.
  writer.write(config);

  if(writer.hasError()) {
    Serial.print("INI writer error: ");
    Serial.println(writer.getErrorMsg());
    return;
  }

  Serial.println("Generated INI:");
  Serial.println();
  Serial.println(writer.getINI());

  // The reader completely ignores both normal INI comments and COMMENT()
  // output. They do not affect deserialization.
  INIreader reader(16, 8);
  BikeConfig decoded;

  if(!reader.parse(writer.getINI()) || !reader.read(decoded)) {
    Serial.print("INI reader error: ");
    Serial.println(reader.getErrorMsg());
    return;
  }

  Serial.println("Decoded values:");
  Serial.print("lightEnabled = ");
  Serial.println(decoded.lightEnabled);

  Serial.print("assistLevel  = ");
  Serial.println(decoded.assistLevel);

  Serial.print("bikeName     = ");
  Serial.println(decoded.bikeName);

  Serial.print("offset       = ");
  Serial.println(decoded.calibration.offset);

  Serial.print("factor       = ");
  Serial.println(decoded.calibration.factor, 3);

  Serial.print("serviceOffset = ");
  Serial.println(decoded.calibration.serviceOffset.has_value() ? "set" : "empty");
}

void loop() {}

/*
  Example output:

  ; Bike configuration file
  ; Generated automatically - edit values if required

  ; Default state of the bicycle light
  lightEnabled = true
  ; Initial assist level after startup
  assistLevel = 3
  ; Display name of the bicycle
  bikeName = Adventure 2

  ; Sensor calibration settings
  [calibration]
  ; Raw sensor offset used during calibration
  offset = 120
  ; Calibration multiplier
  factor = 1.025

  Notice that these two lines are NOT generated:

    ; Optional service-only offset
    serviceOffset = ...

  serviceOffset is std::nullopt, so both the member and its COMMENT() are
  omitted from the INI file.
*/

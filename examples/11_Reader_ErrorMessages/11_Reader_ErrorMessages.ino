#include "ReflectINI_reader.h"

// This example intentionally creates Reader errors.
// Parse errors report line + column; read/mapping errors provide a path + source line.
// The human-readable error text itself contains no location information.

struct Settings {
  int32_t speed = 25;
  REFLECT(MEMBER(speed))
};

struct MotorStatus {
  int32_t speed = 0;
  REFLECT(MEMBER(speed))
};

struct MachineStatus {
  MotorStatus motor;
  REFLECT(MEMBER(motor))
};

void printReaderError(INIreader &reader) {
  Serial.print("Error code : ");
  Serial.println((int)reader.getErrorCode());
  Serial.print("Error text : ");
  Serial.println(reader.getErrorMsg() ? reader.getErrorMsg() : "<none>");
  Serial.print("Error path : ");
  Serial.println(reader.getErrorPath() ? reader.getErrorPath() : "<none>");
  Serial.print("Error line : ");
  Serial.println(reader.getErrorLine());
  Serial.print("Error col  : ");
  Serial.println(reader.getErrorColumn());
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  INIreader reader(32, 8);

  Serial.println("=== 1. Missing '=' ===");
  if(!reader.parse("speed 25\n")) printReaderError(reader);

  Serial.println("=== 2. Duplicate key ===");
  if(!reader.parse("speed = 25\nspeed = 30\n")) printReaderError(reader);

  Serial.println("=== 3. Type mismatch ===");
  if(reader.parse("speed = fast\n")) {
    int32_t speed = 0;
    if(!reader.read(speed, "speed")) printReaderError(reader);
  }

  Serial.println("=== 4. Numeric overflow ===");
  if(reader.parse("value = 500\n")) {
    int8_t value = 0;
    if(!reader.read(value, "value")) printReaderError(reader);
  }

  Serial.println("=== 5. Missing struct member in strict mode ===");
  reader.setConfig(IniConfig::strict());
  if(reader.parse("[settings]\n")) {
    Settings settings;
    if(!reader.read(settings, "settings")) printReaderError(reader);
  }

  Serial.println("=== 6. Unknown struct member in strict mode ===");
  if(reader.parse("[settings]\nspeed = 25\nunknown = 123\n")) {
    Settings settings;
    if(!reader.read(settings, "settings")) printReaderError(reader);
  }

  Serial.println("=== 7. Requested key not found ===");
  reader.setConfig(IniConfig{});
  if(reader.parse("speed = 25\n")) {
    int32_t power = 0;
    if(!reader.read(power, "power")) printReaderError(reader);
  }

  Serial.println("=== 8. Nested error path ===");
  if(reader.parse("[status.motor]\nspeed = fast\n")) {
    MachineStatus status;
    if(!reader.read(status, "status")) printReaderError(reader);
  }

  Serial.println("=== 9. Invalid section syntax ===");
  if(!reader.parse("[status\nspeed = 25\n")) printReaderError(reader);

  Serial.println("=== 10. Disable detailed error information ===");
  reader.setCreateErrorInfo(false);
  if(reader.parse("speed = fast\n")) {
    int32_t speed = 0;
    if(!reader.read(speed, "speed")) printReaderError(reader);
  }

  Serial.println("=== 11. Disable error information in the constructor ===");
  INIreader codeOnlyReader(32, false);
  if(codeOnlyReader.parse("speed = fast\n")) {
    int32_t speed = 0;
    if(!codeOnlyReader.read(speed, "speed")) printReaderError(codeOnlyReader);
  }
}

void loop() {}

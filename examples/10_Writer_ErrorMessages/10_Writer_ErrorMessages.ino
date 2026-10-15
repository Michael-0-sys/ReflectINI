#include "ReflectINI_writer.h"
#include <cmath>

// This example intentionally creates Writer errors.
// It demonstrates the numeric status code, human-readable message and error path.

struct Child {
  int value = 1;
  REFLECT(MEMBER(value))
};

struct DocumentWithSection {
  int id = 7;
  Child child;
  REFLECT(MEMBER(id), MEMBER(child))
};

void printWriterError(INIwriter &writer) {
  Serial.print("Error code : ");
  Serial.println((int)writer.getErrorCode());
  Serial.print("Error text : ");
  Serial.println(writer.getErrorMsg() ? writer.getErrorMsg() : "<none>");
  Serial.print("Error path : ");
  Serial.println(writer.getErrorPath() ? writer.getErrorPath() : "<none>");
  Serial.println();
}

void setup() {
  // Give USB serial enough time to become available after reset.
  Serial.begin(115200);
  delay(1000);

  INIwriter writer(256);

  Serial.println("=== 1. Duplicate key ===");
  writer.write(10, "speed");
  writer.write(20, "speed");
  printWriterError(writer);

  writer.resetINI();

  Serial.println("=== 2. Invalid floating-point number ===");
  const float invalidNumber = NAN;
  writer.write(invalidNumber, "temperature");
  printWriterError(writer);

  Serial.println("=== 3. Output buffer too small ===");
  INIwriter smallWriter(32);
  char longText[80];
  for(size_t i = 0; i < sizeof(longText) - 1; i++) longText[i] = 'A';
  longText[sizeof(longText) - 1] = '\0';
  smallWriter.write(longText, "text");
  printWriterError(smallWriter);

  Serial.println("=== 4. Maximum key count ===");
  INIwriter keyLimitedWriter(256, 2);
  keyLimitedWriter.write(1, "a");
  keyLimitedWriter.write(2, "b");
  keyLimitedWriter.write(3, "c");
  printWriterError(keyLimitedWriter);

  writer.resetINI();

  Serial.println("=== 5. Global key after a section ===");
  DocumentWithSection doc;
  writer.write(doc);            // writes id, then [child], then returns logically to the root
  writer.write(99, "lateKey");  // a new global key is no longer possible
  printWriterError(writer);

  writer.resetINI();

  Serial.println("=== 6. Invalid key ===");
  writer.write(123, "bad=key");
  printWriterError(writer);

  writer.resetINI();

  Serial.println("=== 7. Invalid section name ===");
  writer.startSection("bad[section");
  printWriterError(writer);

  writer.resetINI();

  Serial.println("=== 8. char[N] without terminating zero ===");
  char badText[4] = {'t', 'e', 's', 't'};
  writer.write(badText, "text");
  printWriterError(writer);

  writer.resetINI();

  Serial.println("=== 9. Disable detailed error information ===");
  writer.setCreateErrorInfo(false);
  writer.write(10, "speed");
  writer.write(20, "speed");
  printWriterError(writer);

  Serial.println("=== 10. Disable error information in the constructor ===");
  INIwriter codeOnlyWriter(256, false);
  codeOnlyWriter.write(10, "speed");
  codeOnlyWriter.write(20, "speed");
  printWriterError(codeOnlyWriter);
}

void loop() {}

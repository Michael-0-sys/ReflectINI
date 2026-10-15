// Host test for ReflectINI: Messages longer than the buffer are cut off cleanly; full error path; maximum line / column.
// Build and run: see test/Makefile.
// Too long error messages are cut off cleanly at INI_ERR_MAX_MSG - 1; the path is not part of the message.
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <string>
int main(){
  bool ok = true;
  // read error with a very long location: the message stays short, the path is in getErrorPath()
  std::string sec(64, 's'), key(32, 'k');
  std::string ini = "[" + sec + "]\n" + key + " = " + std::string(40, 'v') + "\n";
  INIreader r(4, 2);
  r.parse(ini.c_str());
  int x; r.read(x, key.c_str(), sec.c_str());
  printf("read : %s\npath : %s (len=%zu)\n", r.getErrorMsg(), r.getErrorPath(), strlen(r.getErrorPath()));
  ok = ok && !strcmp(r.getErrorMsg(), "cannot convert 'vvvvvvvvvvvvvvvvvvvvvvvv' to int32_t")
          && r.getErrorLine() == 2 && r.getErrorColumn() == 0;
  ok = ok && r.getErrorPath() == sec + "." + key;               // full path, never cut off

  // longest possible path: 64-char section + key + array index
  std::string ini2 = "[" + sec + "]\n" + key + " = 1, x\n";
  r.parse(ini2.c_str());
  int arr[2]; r.read(arr, key.c_str(), sec.c_str());
  printf("path : %s (len=%zu, buffer %zu)\n", r.getErrorPath(), strlen(r.getErrorPath()), INI_ERR_MAX_PATH_LEN);
  ok = ok && r.getErrorPath() == sec + "." + key + "[1]";
  static_assert(INI_ERR_MAX_PATH_LEN >= INI_MAX_SECTION_LEN + 1 + INI_MAX_KEY_LEN + sizeof("[65535]"), "path buffer too small");

  // parse error longer than the buffer: cut off at INI_ERR_MAX_MSG - 1, suffix truncated cleanly
  std::string bad = "[" + std::string(60, 'a') + "[b]\n";
  r.parse(bad.c_str());
  const char *m = r.getErrorMsg();
  printf("parse: %s (len=%zu, max %zu)\n", m, strlen(m), INI_ERR_MAX_MSG - 1);
  ok = ok && strlen(m) == INI_ERR_MAX_MSG - 1 && !strncmp(m, "invalid section name 'aaa", 25)
          && r.getErrorLine() == 1 && r.getErrorColumn() == 62;

  // maximum positions: a 65535 byte line ("[" without "]"): the error is at its '\0' = column 65536
  {
    std::string longLine = "[" + std::string(INI_MAX_LEN - 1, 'a');
    INIreader big(4, 2);
    const bool parsed = big.parse(longLine.c_str());
    printf("max column: %s line %u column %lu\n", big.getErrorMsg(), big.getErrorLine(), (unsigned long)big.getErrorColumn());
    ok = ok && !parsed && big.getErrorCode() == IniReaderStatus::INI_ERR_MISSING_BRACKET && big.getErrorLine() == 1 && big.getErrorColumn() == 65536;
  }
  // maximum line: 65534 line breaks + "x" (no '=') -> line 65535 (uint16_t is enough: an error needs a character)
  {
    std::string manyLines = std::string(INI_MAX_LEN - 1, '\n') + "x";
    INIreader big(4, 2);
    const bool parsed = big.parse(manyLines.c_str());
    printf("max line  : %s line %u column %lu\n", big.getErrorMsg(), big.getErrorLine(), (unsigned long)big.getErrorColumn());
    ok = ok && !parsed && big.getErrorLine() == 65535 && big.getErrorColumn() == 1;
  }
  printf("%s\n", ok ? "TRUNCATION OK" : "TRUNCATION FAILED");
  return ok ? 0 : 1;
}

// Host test for ReflectINI: char[N] without terminating zero, array index in the writer error path.
// Build and run: see test/Makefile.
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

struct Full   { char text[4] = {'a', 'b', 'c', 'd'}; REFLECT(MEMBER(text)) };
struct Fits   { char text[4] = "abc"; REFLECT(MEMBER(text)) };
struct List   { char l[2][4] = {"ab", {'w', 'x', 'y', 'z'}}; REFLECT(MEMBER(l)) };
struct FArr  { float f[3] = {1, 2, NAN}; REFLECT(MEMBER(f)) };
struct InSec { FArr s; REFLECT(MEMBER(s)) };
struct Then  { int a[2] = {1, 2}; float bad = NAN; REFLECT(MEMBER(a), MEMBER(bad)) };
struct Single { char c[1] = {'q'}; REFLECT(MEMBER(c)) };

int main(){
  {   // char[N] without '\0': writer refuses instead of writing something the reader cannot read back
    Full f;
    INIwriter w(128);
    w.write(f);
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_STRING_NOT_TERMINATED && w.getINI() == nullptr);
    printf("full: %s | path=%s\n", w.getErrorMsg(), w.getErrorPath());
  }
  {   // N-1 characters: roundtrip
    Fits f;
    INIwriter w(128);
    w.write(f);
    INIreader r(4, 1, IniConfig::strict());
    Fits o; o.text[0] = 0;
    CHECK(!w.hasError() && r.parse(w.getINI()) && r.read(o) && !strcmp(o.text, "abc"));
  }
  {   // inside an array of char[N]
    List l;
    INIwriter w(128);
    w.write(l);
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_STRING_NOT_TERMINATED);
    printf("list: %s | path=%s\n", w.getErrorMsg(), w.getErrorPath());
    CHECK(w.getErrorPath() && !strcmp(w.getErrorPath(), "l[1]"));
  }
  {   // array index in the writer error path (same format as the reader)
    InSec s;
    INIwriter w(128);
    w.write(s);
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_INVALID_NUMBER && w.getErrorPath() && !strcmp(w.getErrorPath(), "s.f[2]"));
    printf("nan in section array: %s | path=%s\n", w.getErrorMsg(), w.getErrorPath());

    Then t;                                  // error after a complete array: no stale index
    w.resetINI().write(t);
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_INVALID_NUMBER && w.getErrorPath() && !strcmp(w.getErrorPath(), "bad"));

    int big[10] = {100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000};
    INIwriter small(40);                     // buffer runs out inside the array
    small.write(big, "big");
    CHECK(small.getErrorCode() == IniWriterStatus::INI_ERR_NO_SPACE && small.getErrorPath() && !strncmp(small.getErrorPath(), "big[", 4));
    printf("no space: %s | path=%s\n", small.getErrorMsg(), small.getErrorPath());

    small.resetINI().write(1, "x").write(1, "x");   // resetINI() clears the index
    CHECK(small.getErrorCode() == IniWriterStatus::INI_ERR_DUPLICATE_KEY && !strcmp(small.getErrorPath(), "x"));
  }
  {   // char[1]: only "" is possible
    Single s;
    INIwriter w(128);
    w.write(s);
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_STRING_NOT_TERMINATED);
    s.c[0] = 0;
    w.resetINI().write(s);
    INIreader r(4, 1);
    Single o; o.c[0] = 'z';
    CHECK(!w.hasError() && r.parse(w.getINI()) && r.read(o) && o.c[0] == 0);
  }
  {   // every fill level of char[4], every byte value at every position: writer ok <=> reader gives the same back
    long n = 0;
    for(int len = 0; len <= 4; len++) for(int b = 1; b < 256; b++){
      Full f;
      memset(f.text, 'a', 4);
      for(int i = 0; i < len; i++) f.text[i] = char(b);
      if(len < 4) f.text[len] = 0;
      INIwriter w(128);
      w.write(f);
      if(len == 4){ CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_STRING_NOT_TERMINATED); continue; }
      INIreader r(4, 1, IniConfig::strict());
      Full o;
      const bool ok = !w.hasError() && r.parse(w.getINI()) && r.read(o) && !memcmp(o.text, f.text, size_t(len) + 1);
      if(!ok){ printf("FAIL len=%d byte=0x%02x %s\n", len, b, w.hasError() ? w.getErrorMsg() : r.getErrorMsg()); failures++; break; }
      n++;
    }
    printf("char[4] roundtrips: %ld\n", n);
  }
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

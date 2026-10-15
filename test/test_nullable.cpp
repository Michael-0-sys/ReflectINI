// Host test for ReflectINI: Single read() of nullable targets (std::optional, const char*, char*): missing key / section = null.
// Build and run: see test/Makefile.
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <optional>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

struct OnlyOpt { std::optional<int> v; REFLECT(MEMBER(v)) };

int main(){
  INIreader r(8, 4);
  CHECK(r.parse(""));
  {   // the cases from the review
    std::optional<int> x = 123;
    CHECK(r.read(x, "x") && !x.has_value());
    const char *s = "old";
    CHECK(r.read(s, "s") && s == nullptr);
    char *m = (char *)"old";
    CHECK(r.read(m, "m") && m == nullptr);
    std::optional<int> y = 5;                          // missing section: also null
    CHECK(r.read(y, "y", "nosection") && !y.has_value());
  }
  {   // non-nullable targets: still an error, target unchanged
    int i = 7;
    CHECK(!r.read(i, "i") && r.getErrorCode() == IniReaderStatus::INI_ERR_KEY_MISSING && i == 7);
    char c[4] = "abc";
    CHECK(!r.read(c, "c") && r.getErrorCode() == IniReaderStatus::INI_ERR_KEY_MISSING && !strcmp(c, "abc"));
    CHECK(!r.read(i, "i", "nosection") && r.getErrorCode() == IniReaderStatus::INI_ERR_SECTION_MISSING);
    OnlyOpt o;                                         // struct section: still INI_ERR_SECTION_MISSING
    CHECK(!r.read(o, "o") && r.getErrorCode() == IniReaderStatus::INI_ERR_SECTION_MISSING);
  }
  {   // values that are present are read normally
    INIreader p(8, 4);
    CHECK(p.parse("x = 4\ns = hi\n[sec]\ny = 9\nempty =\n"));
    std::optional<int> x; CHECK(p.read(x, "x") && x == 4);
    const char *s = nullptr; CHECK(p.read(s, "s") && s && !strcmp(s, "hi"));
    std::optional<int> y; CHECK(p.read(y, "y", "sec") && y == 9);
    std::optional<int> e = 1; CHECK(p.read(e, "empty", "sec") && !e.has_value());
    std::optional<int> bad = 3;                        // present but invalid: still an error, unchanged
    CHECK(p.parse("bad = abc\n") && !p.read(bad, "bad") && p.getErrorCode() == IniReaderStatus::INI_ERR_TYPE_MISMATCH && bad == 3);
  }
  {   // writer -> reader with single values, global and in sections
    std::optional<int> x;
    const char *s = nullptr;
    std::optional<int> y;
    OnlyOpt o;                                         // all members empty -> no [o] header at all
    INIwriter w(256);
    w.write(x, "x").write(s, "s").write(1, "one").write(o, "o").startSection("sec").write(y, "y");
    CHECK(!w.hasError());
    printf("written:\n%s---\n", w.getINI());

    INIreader q(8, 4);
    CHECK(q.parse(w.getINI()));
    std::optional<int> x2 = 99; const char *s2 = "old"; std::optional<int> y2 = 99; std::optional<int> ov = 99; int one = 0;
    CHECK(q.read(x2, "x") && !x2.has_value());
    CHECK(q.read(s2, "s") && s2 == nullptr);
    CHECK(q.read(one, "one") && one == 1);
    CHECK(q.read(y2, "y", "sec") && !y2.has_value());
    CHECK(q.read(ov, "v", "o") && !ov.has_value());   // section [o] was never written
  }
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

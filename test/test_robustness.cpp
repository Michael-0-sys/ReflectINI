// Host test for ReflectINI: Buffer check (64-bit hash), 64-bit duplicate key hash, rotating string buffer limit, reflection expressions evaluated once, Arduino String.
// Build and run: see test/Makefile.
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

// ---------- 1. buffer check ----------
struct A { int a = 0; const char *b = nullptr; REFLECT(MEMBER(a), MEMBER(b)) };
static void testBufferCheck(){
  char buf[] = "a=15";
  INIreader r(4, 1);
  CHECK(r.parse(buf));
  buf[2] = '5'; buf[3] = '2';                         // "a=52": cancels out in a weighted sum
  int x = 0;
  CHECK(!r.read(x, "a") && r.getErrorCode() == IniReaderStatus::INI_ERR_INI_CHANGED && x == 0);

  // every change of one or two bytes (many delta combinations) must be detected
  const char base[] = "a=15\nb=xy";
  const int n = int(sizeof(base) - 1);
  const int deltas[] = {1, -1, 2, -2, 3, -3, 4, -4, 7, -7, 16, -16, 32, -32, 64, -64, 100, -100, 127, -128};
  long checked = 0, missed = 0;
  for(int i = 0; i < n; i++) for(int j = i; j < n; j++) for(int di : deltas) for(int dj : deltas){
    if(i == j && dj != deltas[0]) continue;
    char w[sizeof(base)]; memcpy(w, base, sizeof(base));
    INIreader q(4, 1);
    if(!q.parse(w)) continue;
    w[i] = char(w[i] + di);
    if(j != i) w[j] = char(w[j] + dj);
    if(!w[i] || !w[j]) continue;                        // would shorten the string
    if(!memcmp(w, base, sizeof(base))) continue;
    int v; checked++;
    if(q.read(v, "a") || q.getErrorCode() != IniReaderStatus::INI_ERR_INI_CHANGED) missed++;
  }
  printf("buffer check: %ld modifications, %ld missed\n", checked, missed);
  CHECK(missed == 0);

  // zero-copy: hash is recomputed after the '\0' writes, later modifications are still detected
  char zb[] = "a=1\nb=hello\n";
  INIreader z(4, 1);
  CHECK(z.parse(zb, true));
  A s1, s2;
  CHECK(z.read(s1) && z.read(s2) && s1.b == s2.b && !strcmp(s1.b, "hello"));
  zb[2] = '9';
  CHECK(!z.read(s1) && z.getErrorCode() == IniReaderStatus::INI_ERR_INI_CHANGED);
}

// ---------- 2. 64-bit duplicate check ----------
static uint32_t fnv32(const char *k){       // old 32-bit hash of (global section, key)
  uint32_t h = 2166136261u; h ^= 0x1Fu; h *= 16777619u;
  for(const char *p = k; *p; p++){ h ^= (uint8_t)*p; h *= 16777619u; }
  return h;
}
static void testDuplicateHash(){
  CHECK(fnv32("7_ylyz0_") == fnv32("ogiqifso"));     // really a 32-bit collision
  INIwriter w(256);
  w.write(1, "7_ylyz0_").write(2, "ogiqifso");
  CHECK(!w.hasError());
  if(w.hasError()) printf("  %s\n", w.getErrorMsg());
  w.write(3, "OGIQIFSO");                               // real duplicate (case-insensitive) still found
  CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_DUPLICATE_KEY);
}

// ---------- 3. rotating string buffers ----------
struct Ten { const char *s[10] = {}; REFLECT(MEMBER_AS("s0", s[0]), MEMBER_AS("s1", s[1]), MEMBER_AS("s2", s[2]), MEMBER_AS("s3", s[3]),
                                             MEMBER_AS("s4", s[4]), MEMBER_AS("s5", s[5]), MEMBER_AS("s6", s[6]), MEMBER_AS("s7", s[7]),
                                             MEMBER_AS("s8", s[8]), MEMBER_AS("s9", s[9])) };
struct Eight { const char *s[8] = {}; REFLECT(MEMBER_AS("s0", s[0]), MEMBER_AS("s1", s[1]), MEMBER_AS("s2", s[2]), MEMBER_AS("s3", s[3]),
                                              MEMBER_AS("s4", s[4]), MEMBER_AS("s5", s[5]), MEMBER_AS("s6", s[6]), MEMBER_AS("s7", s[7])) };
struct Arr9 { const char *l[9] = {}; REFLECT(MEMBER(l)) };
static const char *NAMES[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
static std::string doc(int count, bool escaped){
  std::string d;
  for(int i = 0; i < count; i++){
    d += "s" + std::to_string(i) + " = ";
    d += escaped ? std::string("\"") + NAMES[i] + "\\t\"\n" : std::string(NAMES[i]) + "\n";
  }
  return d;
}
static void testRing(){
  {   // read-only, 10 strings: error instead of wrong pointers
    const std::string d = doc(10, false);
    INIreader r(16, 1);
    Ten t;
    CHECK(r.parse(d.c_str()) && !r.read(t) && r.getErrorCode() == IniReaderStatus::INI_ERR_STRING_BUFFER_LIMIT);
    printf("ring: %s\n", r.getErrorMsg());
    CHECK(t.s[0] == nullptr);                            // target unchanged
  }
  {   // read-only, 8 strings: exactly fits, all distinct and correct
    const std::string d = doc(8, false);
    INIreader r(16, 1);
    Eight e;
    CHECK(r.parse(d.c_str()) && r.read(e));
    bool okAll = true;
    for(int i = 0; i < 8; i++) okAll = okAll && e.s[i] && !strcmp(e.s[i], NAMES[i]);
    CHECK(okAll);
    // every read() starts with a full ring again
    Eight e2;
    CHECK(r.read(e2));
  }
  {   // writable + escapes: 10 copies needed -> error
    std::string d = doc(10, true);
    INIreader r(16, 1);
    Ten t;
    CHECK(r.parse(&d[0], true) && !r.read(t) && r.getErrorCode() == IniReaderStatus::INI_ERR_STRING_BUFFER_LIMIT);
  }
  {   // writable without escapes: zero-copy, no ring used -> 10 strings are fine
    std::string d = doc(10, false);
    INIreader r(16, 1);
    Ten t;
    CHECK(r.parse(&d[0], true) && r.read(t));
    bool okAll = true;
    for(int i = 0; i < 10; i++) okAll = okAll && t.s[i] && !strcmp(t.s[i], NAMES[i]);
    CHECK(okAll);
  }
  {   // arrays are never zero-copy: 9 elements -> error, also in writable mode
    std::string d = "l = a, b, c, d, e, f, g, h, i\n";
    INIreader r(4, 1);
    Arr9 a;
    CHECK(r.parse(&d[0], true) && !r.read(a) && r.getErrorCode() == IniReaderStatus::INI_ERR_STRING_BUFFER_LIMIT);
  }
}

// ---------- 6. reflection expressions are evaluated once ----------
static int calls = 0;
struct Side {
  int x = 7;
  int &ref(){ calls++; return x; }
  const int &ref() const { calls++; return x; }
  REFLECT(MEMBER_AS("x", ref()))
};
struct Outer { int y = 1; Side side; REFLECT(MEMBER(y), MEMBER(side)) };
static void testSideEffects(){
  Side s;
  calls = 0;
  INIwriter w(256);
  w.write(s);
  CHECK(calls == 1);
  printf("MEMBER_AS calls per write: %d\n", calls);

  Outer o;
  calls = 0;
  INIwriter w2(256);
  w2.write(o);
  CHECK(calls == 1);

  INIreader r(4, 2);
  CHECK(r.parse(w2.getINI()));
  Outer in; in.side.x = 0;
  calls = 0;
  CHECK(r.read(in) && in.side.x == 7);
  CHECK(calls == 1);
  printf("MEMBER_AS calls per read: %d\n", calls);
}

#ifdef ARDUINO
// ---------- 4. Arduino String: read-only by default ----------
struct Mix { const char *s[8] = {}; String t; REFLECT(MEMBER_AS("s0", s[0]), MEMBER_AS("s1", s[1]), MEMBER_AS("s2", s[2]), MEMBER_AS("s3", s[3]),
             MEMBER_AS("s4", s[4]), MEMBER_AS("s5", s[5]), MEMBER_AS("s6", s[6]), MEMBER_AS("s7", s[7]), MEMBER(t)) };
struct SA { const char *name = nullptr; String label; int n = 0; REFLECT(MEMBER(name), MEMBER(label), MEMBER(n)) };
static void testArduinoString(){
  String ini("name = bob\nlabel = \"x\\ty\"\nn = 3\n");
  const String before = ini;
  {
    INIreader r(4, 1);
    SA s;
    CHECK(r.parse(ini) && r.read(s));
    CHECK(!strcmp(s.name, "bob") && s.label == "x\ty" && s.n == 3);
    CHECK(ini == before);                                // parse(String&) did not modify the String
    CHECK(!(s.name >= ini.c_str() && s.name < ini.c_str() + ini.length()));
  }
  {
    INIreader r(4, 1);
    SA s;
    CHECK(r.parse(ini, true) && r.read(s));             // explicit zero-copy
    CHECK(s.name >= ini.c_str() && s.name < ini.c_str() + ini.length() && !strcmp(s.name, "bob"));
    CHECK(!(ini == before));                             // documented: the String content changed
  }
  {   // String targets never consume / overwrite ring slots of const char* targets
    const std::string d = doc(8, false) + "t = string\n";
    INIreader r(16, 1);
    Mix m;
    CHECK(r.parse(d.c_str()) && r.read(m));
    CHECK(!strcmp(m.s[0], "zero") && !strcmp(m.s[7], "seven") && m.t == "string");
  }
  {   // writer with String values / keys
    SA s; s.name = "n1"; s.label = "a;b"; s.n = 5;
    INIwriter w(256);
    w.write(s).write(1, String("extra"));
    CHECK(!w.hasError());
    printf("arduino writer:\n%s", w.getINI());
  }
}
#endif

int main(){
  testBufferCheck();
  testDuplicateHash();
  testRing();
  testSideEffects();
#ifdef ARDUINO
  testArduinoString();
  printf("(ARDUINO paths tested)\n");
#endif
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

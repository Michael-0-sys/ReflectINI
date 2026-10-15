// Host test for ReflectINI: nullptr in strict mode, caseSensitive per document, writable / zero-copy mode, buffer check.
// Build and run: see test/Makefile.
// nullptr in strict mode, caseSensitive snapshot, writable / zero-copy mode, buffer check
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <optional>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

struct NConfig {
  int x = 1;
  const char *name = nullptr;
  char *mut = nullptr;
  std::optional<int> opt;
  REFLECT(MEMBER(x), MEMBER(name), MEMBER(mut), MEMBER(opt))
};

static void testStrictNullptr(){
  NConfig in;                                 // name/mut nullptr, opt empty
  INIwriter w(256, uint16_t(16), IniConfig::strict());
  w.write(in);
  CHECK(!w.hasError());
  printf("strict writer:\n%s", w.getINI());

  INIreader r(16, 4, IniConfig::strict());
  CHECK(r.parse(w.getINI()));
  NConfig out;
  out.name = "default"; out.opt = 5;          // non-null defaults must become null
  const bool ok = r.read(out);
  CHECK(ok);
  if(!ok) printf("  %s\n", r.getErrorMsg());
  CHECK(out.x == 1 && out.name == nullptr && out.mut == nullptr && !out.opt.has_value());

  // tolerant reader: missing key keeps the default (unchanged behaviour)
  INIreader t(16, 4);
  CHECK(t.parse(w.getINI()));
  NConfig keep; keep.name = "default"; keep.opt = 5;
  CHECK(t.read(keep) && keep.name && !strcmp(keep.name, "default") && keep.opt == 5);

  // strict: a missing non-nullable key is still an error
  INIreader s(16, 4, IniConfig::strict());
  CHECK(s.parse("name = a\n"));
  NConfig e;
  CHECK(!s.read(e) && s.getErrorCode() == IniReaderStatus::INI_ERR_KEY_MISSING);
  printf("strict missing int: %s\n", s.getErrorMsg());

  // with values: roundtrip of non-null pointers
  NConfig v; v.name = "hello"; char buf[] = "world"; v.mut = buf; v.opt = 3;
  INIwriter w2(256, uint16_t(16), IniConfig::strict());
  w2.write(v);
  INIreader r2(16, 4, IniConfig::strict());
  NConfig o2;
  CHECK(r2.parse(w2.getINI()) && r2.read(o2));
  CHECK(o2.name && !strcmp(o2.name, "hello") && o2.mut && !strcmp(o2.mut, "world") && o2.opt == 3);
}

static void testCaseSensitiveSnapshot(){
  // writer: switching caseSensitive in the middle of a document does not change the duplicate check
  {
    INIwriter w(256);
    w.write(1, "A");
    IniConfig cs; cs.caseSensitive = true;
    w.setConfig(cs);
    w.write(2, "a");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_DUPLICATE_KEY);   // still case-insensitive
    w.resetINI();                                                        // new document: now case-sensitive
    w.write(1, "A").write(2, "a");
    CHECK(!w.hasError());
    w.resetINI();
    w.write(1, "A");
    w.setConfig(IniConfig{});                                            // back to insensitive: not before resetINI
    w.write(2, "a");
    CHECK(!w.hasError());
  }
  // writer: constructor with config + case-sensitive from the start
  {
    IniConfig cs; cs.caseSensitive = true;
    INIwriter w(256, uint16_t(8), cs);
    w.write(1, "A").write(2, "a");
    CHECK(!w.hasError());
  }
  // reader: caseSensitive is fixed by parse()
  {
    INIreader r(8, 2);
    CHECK(r.parse("Port = 5\n"));
    IniConfig cs; cs.caseSensitive = true;
    r.setConfig(cs);
    int p = 0;
    CHECK(r.read(p, "port") && p == 5);          // document was parsed case-insensitive
    CHECK(r.parse("Port = 5\nport = 6\n"));      // new parse: case-sensitive -> no duplicate
    CHECK(r.read(p, "port") && p == 6);
    CHECK(r.read(p, "Port") && p == 5);
  }
}

struct ZC {
  const char *name = nullptr;
  const char *q = nullptr;
  const char *sq = nullptr;
  const char *esc = nullptr;
  const char *empty = "x";
  const char *list[2] = {nullptr, nullptr};
  const char *last = nullptr;
  char copy[8] = "";
  REFLECT(MEMBER(name), MEMBER(q), MEMBER(sq), MEMBER(esc), MEMBER(empty), MEMBER(list), MEMBER(last), MEMBER(copy))
};
static bool inside(const char *p, const char *buf, size_t n){ return p >= buf && p < buf + n; }

static void testZeroCopy(){
  char buf[] = "name = hello ; comment\r\n"
               "q = \"quoted text\"\n"
               "sq = 'single'   # c\n"
               "esc = \"a\\tb\"\n"
               "empty =\n"
               "list = x, \"y\"\n"
               "copy = abc\n"
               "last = end";
  const size_t n = sizeof(buf);
  INIreader r(16, 2);
  CHECK(r.parse(buf, true));
  ZC z;
  CHECK(r.read(z));
  if(r.hasReadError()) printf("  %s\n", r.getErrorMsg());
  CHECK(!strcmp(z.name, "hello") && inside(z.name, buf, n));
  CHECK(!strcmp(z.q, "quoted text") && inside(z.q, buf, n));
  CHECK(!strcmp(z.sq, "single") && inside(z.sq, buf, n));
  CHECK(!strcmp(z.esc, "a\tb") && !inside(z.esc, buf, n));      // escapes: copy
  CHECK(!strcmp(z.empty, "") && inside(z.empty, buf, n));
  CHECK(!strcmp(z.list[0], "x") && !strcmp(z.list[1], "y") && !inside(z.list[0], buf, n));   // arrays: copy
  CHECK(!strcmp(z.last, "end") && inside(z.last, buf, n));
  CHECK(!strcmp(z.copy, "abc"));

  // read again: same result (quote flags from parse, checksum updated for the written '\0')
  ZC z2;
  CHECK(r.read(z2));
  if(r.hasReadError()) printf("  %s\n", r.getErrorMsg());
  CHECK(z2.name == z.name && z2.q == z.q && z2.sq == z.sq && !strcmp(z2.esc, "a\tb") && z2.last == z.last);
  const char *one = nullptr;
  CHECK(r.read(one, "q") && one == z.q);

  // buffer modified after parse -> INI_ERR_INI_CHANGED
  buf[2] = 'X';
  CHECK(!r.read(z2) && r.getErrorCode() == IniReaderStatus::INI_ERR_INI_CHANGED);
  printf("changed: %s\n", r.getErrorMsg());
  r.setBufferCheck(false);
  CHECK(r.read(one, "q") && !strcmp(one, "quoted text"));

  // read-only buffer (default) is never modified
  const std::string ro = "name = hello\nq = \"quoted\"\nlast = end";
  const std::string before = ro;
  INIreader r3(16, 2);
  CHECK(r3.parse(ro.c_str()));
  ZC z3;
  CHECK(r3.read(z3) && r3.read(z3));
  CHECK(ro == before && !inside(z3.name, ro.c_str(), ro.size()));
}

// Random documents: read-only and zero-copy must give identical results, also on a second read
struct FZ {
  const char *a = nullptr; const char *b = nullptr; int i = 0; char c[6] = ""; const char *l[2] = {nullptr, nullptr};
  REFLECT(MEMBER(a), MEMBER(b), MEMBER(i), MEMBER(c), MEMBER(l))
};
static std::string snap(const FZ &f){
  auto s = [](const char *p){ return p ? std::string("<") + p + ">" : std::string("null"); };
  return s(f.a) + s(f.b) + std::to_string(f.i) + "|" + f.c + "|" + s(f.l[0]) + s(f.l[1]);
}
static void fuzzZeroCopy(){
  const char alpha[] = "ab=;# \"'\\,\n\r\t[]x1";
  srand(7);
  long compared = 0;
  for(int it = 0; it < 100000; it++){
    std::string doc;
    const int lines = 1 + rand() % 5;
    for(int k = 0; k < lines; k++){
      const char *keys[] = {"a", "b", "i", "c", "l"};
      doc += keys[rand() % 5];
      doc += rand() % 2 ? " = " : "=";
      const int len = rand() % 10;
      for(int j = 0; j < len; j++) doc += alpha[rand() % (sizeof(alpha) - 1)];
      doc += (rand() % 3 == 0) ? "\r\n" : "\n";
    }
    INIreader ro(8, 2);
    FZ f1;
    const bool p1 = ro.parse(doc.c_str());
    const bool ok1 = p1 && ro.read(f1);
    const std::string s1 = ok1 ? snap(f1) : "";
    FZ f1b;
    const bool ok1b = p1 && ro.read(f1b);
    const std::string s1b = ok1b ? snap(f1b) : "";   // snapshot now: rotating buffers are reused by later reads

    std::string w = doc;           // writable copy
    INIreader zc(8, 2);
    FZ f2, f3;
    const bool p2 = zc.parse(&w[0], true);
    const bool ok2 = p2 && zc.read(f2);
    const std::string s2 = ok2 ? snap(f2) : "";
    const bool ok3 = p2 && zc.read(f3);
    const std::string s3 = ok3 ? snap(f3) : "";

    if(p1 != p2 || ok1 != ok2 || ok2 != ok3 || ok1 != ok1b || s1 != s2 || s2 != s3 || s1b != s1){
      if(failures < 10){
        printf("FAIL zero-copy mismatch: ");
        for(char ch : doc) printf(ch == '\n' ? "\\n" : ch == '\r' ? "\\r" : "%c", ch);
        printf("\n  ro=%s\n  zc=%s\n  zc2=%s\n", s1.c_str(), s2.c_str(), s3.c_str());
      }
      failures++;
    }
    if(ok1) compared++;
  }
  printf("zero-copy fuzz: %ld documents compared\n", compared);
}

int main(){
  testStrictNullptr();
  testCaseSensitiveSnapshot();
  testZeroCopy();
  fuzzZeroCopy();
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

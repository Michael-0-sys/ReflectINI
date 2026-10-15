// Host test for ReflectINI: Exact text of every error message, error path, line and column.
// Build and run: see test/Makefile.
// Snapshot of every error message (exact text) + the MachineStatus path case
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

static int failures = 0;
static void expect(const char *what, const char *got, const char *want){
  if(!got || strcmp(got, want)){
    printf("FAIL %-22s\n   got:  %s\n   want: %s\n", what, got ? got : "(null)", want);
    failures++;
  }else{
    printf("ok   %-22s %s\n", what, got);
  }
}
static void expectPos(INIreader *r, unsigned line, unsigned col){
  if(r->getErrorLine() != line || r->getErrorColumn() != col){
    printf("FAIL position: got line %u col %u, want line %u col %u\n", r->getErrorLine(), r->getErrorColumn(), line, col);
    failures++;
  }
}
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

struct Motor { int32_t speed = 0; REFLECT(MEMBER(speed)) };
struct MachineStatus { Motor motor; REFLECT(MEMBER(motor)) };
struct Big { const char *s[9] = {}; REFLECT(MEMBER_AS("a", s[0]), MEMBER_AS("b", s[1]), MEMBER_AS("c", s[2]), MEMBER_AS("d", s[3]),
             MEMBER_AS("e", s[4]), MEMBER_AS("f", s[5]), MEMBER_AS("g", s[6]), MEMBER_AS("h", s[7]), MEMBER_AS("i", s[8])) };
struct Strict { int x = 0; REFLECT(MEMBER(x)) };
struct NoTerm { char t[2] = {'a', 'b'}; REFLECT(MEMBER(t)) };
struct In { int v = 1; REFLECT(MEMBER(v)) };
struct Deep2 { int v = 1; REFLECT(MEMBER(v)) };
struct Deep1 { Deep2 d; REFLECT(MEMBER_AS("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", d)) };
struct Deep0 { Deep1 d; REFLECT(MEMBER_AS("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", d)) };

static INIreader *lastParse = nullptr;
static const char *parseMsg(const char *ini, uint16_t entries = 8, uint8_t sections = 4){
  INIreader *&r = lastParse;
  delete r;
  r = new INIreader(entries, sections);
  r->parse(ini);
  return r->getErrorMsg();
}
static INIreader *lastRead = nullptr;
template<typename T>
static const char *readMsg(INIreader &r, const char *ini, const char *key, const char *section = nullptr){
  r.parse(ini);
  T v{};
  r.read(v, key, section);
  lastRead = &r;
  return r.getErrorMsg();
}

int main(){
  // ---------- MachineStatus: message and path ----------
  {
    INIreader reader(8, 4);
    CHECK(reader.parse("[status.motor]\nspeed = fast\n"));
    MachineStatus status;
    CHECK(!reader.read(status, "status"));
    expect("MachineStatus msg", reader.getErrorMsg(), "cannot convert 'fast' to int32_t");
    expect("MachineStatus path", reader.getErrorPath(), "status.motor.speed");
    // the path stays the same when it is queried several times / after the message
    expect("path again", reader.getErrorPath(), "status.motor.speed");
    expectPos(&reader, 2, 0);
  }

  // ---------- reader: parse errors ----------
  expect("missing ]",          parseMsg("[abc\n"), "missing ']'"); expectPos(lastParse, 1, 5);
  expect("empty section",      parseMsg("[ ]\n"), "empty section name"); expectPos(lastParse, 1, 3);
  expect("section too long",   parseMsg(("[" + std::string(65, 's') + "]").c_str()), "section name too long (limit=64)"); expectPos(lastParse, 1, 2);
  expect("invalid section",    parseMsg("[a[b]\n"), "invalid section name 'a[b' (must not contain [ or control characters)"); expectPos(lastParse, 1, 3);
  expect("char after ]",       parseMsg("[a] x\n"), "unexpected character 'x' after ']'"); expectPos(lastParse, 1, 5);
  expect("ctrl char after ]",  parseMsg("[a] \x01\n"), "unexpected character 0x01 after ']'"); expectPos(lastParse, 1, 5);
  expect("utf8 after quote",   parseMsg("k = \"v\"\xC3\xA4\n"), "unexpected character 0xC3 after closing quote"); expectPos(lastParse, 1, 8);
  expect("section count",      parseMsg("[a]\n[b]\n", 8, 1), "maximum section count exceeded (limit=1)"); expectPos(lastParse, 2, 1);
  expect("missing =",          parseMsg("x\n"), "missing '=' (expected key = value)"); expectPos(lastParse, 1, 1);
  expect("empty key",          parseMsg("= 1\n"), "empty key"); expectPos(lastParse, 1, 1);
  expect("key too long",       parseMsg((std::string(33, 'k') + "=1").c_str()), "key too long (limit=32)"); expectPos(lastParse, 1, 1);
  expect("invalid key",        parseMsg("a#b = 1\n"), "invalid key 'a#b' (must not contain [ ] ; # or control characters)"); expectPos(lastParse, 1, 2);
  expect("missing quote",      parseMsg("k = \"abc\n"), "missing closing quote '\"'"); expectPos(lastParse, 1, 5);
  expect("missing quote '",    parseMsg("k = 'abc\n"), "missing closing quote \"'\""); expectPos(lastParse, 1, 5);
  expect("char after quote",   parseMsg("k = \"a\" b\n"), "unexpected character 'b' after closing quote"); expectPos(lastParse, 1, 9);
  expect("duplicate key",      parseMsg("x = 1\n\nX = 2\n"), "duplicate key 'X' (first defined on line 1)"); expectPos(lastParse, 3, 1);
  expect("key count",          parseMsg("a=1\nb=2\n", 1), "maximum key count exceeded (limit=1)"); expectPos(lastParse, 2, 1);
  expect("nullptr INI",        parseMsg(nullptr), "INI buffer is nullptr");
  expect("too large",          parseMsg(std::string(65536, ';').c_str()), "INI too large (limit=65535 bytes)");

  // ---------- reader: read errors ----------
  INIreader r(16, 4);
  { INIreader np; int x; np.read(x, "x"); expect("not parsed", np.getErrorMsg(), "INI has not been parsed"); }
  { r.parse("x=1"); int x; r.read(x, nullptr); expect("nullptr key", r.getErrorMsg(), "key must not be nullptr"); }
  { char b[] = "x=1"; r.parse(b); b[2] = '2'; int x; r.read(x, "x"); expect("modified", r.getErrorMsg(), "INI buffer was modified after parsing"); }
  expect("missing key",        readMsg<int>(r, "a=1", "x"), "missing key (expected int32_t)");
  expect("missing section",    readMsg<int>(r, "a=1", "x", "s"), "missing section");
  expect("missing section st", readMsg<In>(r, "a=1", "in"), "missing section");
  expect("cannot convert",     readMsg<int>(r, "x = abc", "x"), "cannot convert 'abc' to int32_t"); expectPos(lastRead, 1, 0);
  expect("exceeds range",      readMsg<int8_t>(r, "x = 500", "x"), "value '500' exceeds range of int8_t"); expectPos(lastRead, 1, 0);
  expect("below range",        readMsg<uint8_t>(r, "x = -1", "x"), "value '-1' is below range of uint8_t"); expectPos(lastRead, 1, 0);
  expect("string too long",    readMsg<char[4]>(r, "x = abcd", "x"), "string too long (limit=3)"); expectPos(lastRead, 1, 0);
  expect("array too few",      readMsg<int[3]>(r, "x = 1, 2", "x"), "array has too few elements (got 2, expected 3)"); expectPos(lastRead, 1, 0);
  expect("array too many",     readMsg<int[3]>(r, "x = 1, 2, 3, 4", "x"), "array has too many elements (got 4, expected 3)"); expectPos(lastRead, 1, 0);
  expect("array element",      readMsg<int[2]>(r, "[s]\nx = 1, z", "x", "s"), "cannot convert 'z' to int32_t"); expectPos(lastRead, 2, 0);
  expect("section name long",  readMsg<int>(r, "x=1", "x", std::string(65, 's').c_str()), "section name too long (limit=64)");
  {
    INIreader s(8, 4, IniConfig::strict());
    Strict v;
    s.parse("x = 1\ny = 2\n"); s.read(v); expect("unknown key", s.getErrorMsg(), "unknown key"); expectPos(&s, 2, 0);
    s.parse("x = 1\n[z]\n"); s.read(v); expect("unknown section", s.getErrorMsg(), "unknown section"); expectPos(&s, 2, 0);
    s.parse(""); s.read(v); expect("missing key strict", s.getErrorMsg(), "missing key (expected int32_t)");
  }
  {
    INIreader b(16, 1);
    b.parse("a=1\nb=2\nc=3\nd=4\ne=5\nf=6\ng=7\nh=8\ni=9\n");
    Big v; b.read(v);
    expect("string ring", b.getErrorMsg(), "too many const char* strings in one read() (limit=8, use char[N], read() in parts or parse(ini, true))"); expectPos(&b, 9, 0);
  }

  // ---------- writer ----------
  {
    INIwriter w(32);  w.write("0123456789012345678901234567890123", "k");
    expect("w output full", w.getErrorMsg(), "output buffer full (capacity=32)");
  }
  { INIwriter w(256); w.write(1, std::string(33, 'k').c_str()); expect("w key too long", w.getErrorMsg(), "key too long (limit=32)"); }
  { INIwriter w(256); w.startSection(std::string(65, 's').c_str()); expect("w section too long", w.getErrorMsg(), "section name too long (limit=64)"); }
  { INIwriter w(1024); std::string s(255, 'x'); w.write(s.c_str(), "k"); expect("w string too long", w.getErrorMsg(), "string too long (limit=254)"); }
  { INIwriter w(256, uint16_t(1)); w.write(1, "a").write(2, "b"); expect("w key count", w.getErrorMsg(), "maximum key count exceeded (limit=1)"); }
  { INIwriter w(256); w.write(1, "a=b"); expect("w invalid key", w.getErrorMsg(), "invalid key 'a=b'"); }
  { INIwriter w(256); w.write(1); expect("w key required", w.getErrorMsg(), "a key is required for this value"); }
  { INIwriter w(256); w.startSection("a]b"); expect("w invalid section", w.getErrorMsg(), "invalid section name"); }
  { INIwriter w(256); w.write(1, "a").write(2, "A"); expect("w duplicate", w.getErrorMsg(), "duplicate key 'A'"); }
  { INIwriter w(256); w.startSection("s").write(1, "a"); In in; w.write(in, "t"); w.resetINI();
    w.write(in, "t").write(1, "g"); expect("w global after", w.getErrorMsg(), "global key 'g' cannot be written after a section"); }
  { INIwriter w(256); w.write(NAN, "f"); expect("w invalid number", w.getErrorMsg(), "invalid number for key 'f'"); }
  { INIwriter w(256); NoTerm t; w.write(t); expect("w not terminated", w.getErrorMsg(), "char[] for key 't' is not null-terminated"); }

  // ---------- writer: no error path for a rejected startSection() name ----------
  {
    INIwriter w(256);
    w.startSection("good");
    w.startSection("[bad");
    expect("w startSection msg", w.getErrorMsg(), "invalid section name");
    CHECK(w.getErrorPath() == nullptr);
  }
  { INIwriter w(256); w.startSection("good").startSection(std::string(65, 's').c_str()); CHECK(w.getErrorPath() == nullptr); }
  { INIwriter w(256); w.startSection("good").startSection(nullptr); CHECK(w.hasError() && w.getErrorPath() == nullptr); }
  { INIwriter w(256); w.startSection(""); CHECK(w.hasError() && w.getErrorPath() == nullptr); }
  {   // nested struct section too long: the path (parent section + member key) is still reported
    INIwriter w(512); Deep0 d; w.write(d);
    expect("w nested too long", w.getErrorMsg(), "section name too long (limit=64)");
    expect("w nested path", w.getErrorPath(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  }

  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

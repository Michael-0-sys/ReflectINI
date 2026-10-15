// Host test for ReflectINI: Exhaustive writer -> reader roundtrip of special characters; reader and writer accept the same key / section names.
// Build and run: see test/Makefile.
// Exhaustive roundtrip + key/section consistency between INIwriter and INIreader
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <string>

struct S {
  char text[8] = "";
  const char *ptr = nullptr;
  char list[3][8] = {"", "", ""};
  char single = 0;
  REFLECT(MEMBER(text), MEMBER(ptr), MEMBER(list), MEMBER(single))
};

static const char ALPHA[] = { 'a', ',', '"', '\'', ';', '#', ' ', '\\', '=', '\t', '\n', '[', ']', '\r', 'x' };
static const size_t NA = sizeof(ALPHA);

static long tested = 0, failures = 0;

static void report(const char *what, const std::string &s, const char *detail){
  if(failures < 15){
    printf("FAIL %s: \"", what);
    for(char c : s){ if((unsigned char)c < 0x20) printf("\\x%02x", (unsigned char)c); else putchar(c); }
    printf("\"  %s\n", detail ? detail : "");
  }
  failures++;
}

static void roundtrip(const std::string &s){
  S in;
  memcpy(in.text, s.c_str(), s.size() + 1);
  in.ptr = s.c_str();
  memcpy(in.list[0], s.c_str(), s.size() + 1);
  strcpy(in.list[1], "x");
  memcpy(in.list[2], s.c_str(), s.size() + 1);
  in.single = s[0];

  INIwriter w(512);
  w.write(in);
  if(w.hasError()){ report("write", s, w.getErrorMsg()); return; }

  INIreader r(16, 4, IniConfig::strict());
  if(!r.parse(w.getINI())){ report("parse", s, r.getErrorMsg()); return; }
  S out;
  if(!r.read(out)){ report("read", s, r.getErrorMsg()); return; }

  if(strcmp(out.text, s.c_str()))     report("char[8]", s, w.getINI());
  if(!out.ptr || strcmp(out.ptr, s.c_str())) report("const char*", s, w.getINI());
  if(strcmp(out.list[0], s.c_str()) || strcmp(out.list[1], "x") || strcmp(out.list[2], s.c_str())) report("array", s, w.getINI());
  if(out.single != s[0])              report("char", s, w.getINI());
  tested++;
}

// Key accepted by the writer <=> the same key in a hand-written line is accepted by the parser (and found again)
static void keyConsistency(const std::string &k){
  INIwriter w(256);
  w.write(1, k.c_str());
  const bool writerOk = !w.hasError();

  // A trimmed key is a different key for the reader, and '=' / line breaks change the line structure itself:
  // only names without blanks at the ends and without = \n \r can be compared directly.
  if(k.empty() || INI_IS_BLANK(k.front()) || INI_IS_BLANK(k.back())) return;
  if(k.find_first_of("=\n\r") != std::string::npos) return;
  if(k[0] == '[' || k[0] == ';' || k[0] == '#') return;   // line is a section header / comment, not a key

  const std::string line = k + "=1\n";
  INIreader r(4, 2);
  const bool parseOk = r.parse(line.c_str());
  int v = 0;
  const bool readOk = parseOk && r.read(v, k.c_str()) && v == 1;

  if(writerOk != parseOk) report("key consistency", k, writerOk ? "writer ok, reader rejects" : "writer rejects, reader accepts");
  else if(writerOk && !readOk) report("key roundtrip", k, r.getErrorMsg());
  tested++;
}

static void sectionConsistency(const std::string &n){
  INIwriter w(256);
  w.startSection(n.c_str()).write(1, "v");
  const bool writerOk = !w.hasError();

  if(n.empty() || INI_IS_BLANK(n.front()) || INI_IS_BLANK(n.back())) return;
  if(n.find_first_of("]\n\r") != std::string::npos) return;   // ']' ends the header, line breaks end the line

  const std::string text = "[" + n + "]\nv=1\n";
  INIreader r(4, 2);
  const bool parseOk = r.parse(text.c_str());
  int v = 0;
  const bool readOk = parseOk && r.read(v, "v", n.c_str()) && v == 1;

  if(writerOk != parseOk) report("section consistency", n, writerOk ? "writer ok, reader rejects" : "writer rejects, reader accepts");
  else if(writerOk && !readOk) report("section roundtrip", n, r.getErrorMsg());
  if(writerOk && !r.parse(w.getINI())) report("section writer->reader", n, r.getErrorMsg());
  tested++;
}

template<typename F>
static void forAllStrings(size_t maxLen, F fn){
  std::string s;
  for(size_t len = 1; len <= maxLen; len++){
    size_t total = 1;
    for(size_t i = 0; i < len; i++) total *= NA;
    for(size_t n = 0; n < total; n++){
      s.assign(len, ' ');
      size_t x = n;
      for(size_t i = 0; i < len; i++){ s[i] = ALPHA[x % NA]; x /= NA; }
      fn(s);
    }
  }
}

int main(){
  forAllStrings(4, roundtrip);
  printf("roundtrip: %ld strings\n", tested);
  const long t1 = tested;
  forAllStrings(3, keyConsistency);
  forAllStrings(3, sectionConsistency);
  printf("key/section consistency: %ld names\n", tested - t1);

  // the concrete cases from the review
  {
    S in; strcpy(in.text, "a,\"x");
    INIwriter w(256); w.write(in);
    printf("a,\"x -> %s", strstr(w.getINI(), "text"));
  }
  {
    INIreader r(4, 2);
    printf("getSectionCount() after constructor: %u\n", r.getSectionCount());
    printf("bad#key=1    -> %s\n", r.parse("bad#key=1") ? "accepted" : r.getErrorMsg());
    printf("[bad[name]   -> %s\n", r.parse("[bad[name]\nv=1") ? "accepted" : r.getErrorMsg());
  }

  printf("\n%s (%ld failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

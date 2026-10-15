// Host test for ReflectINI: Functional test of reader and writer, plus the same struct with ReflectJSON (needs ReflectJSON >= 1.1.0).
// Build and run: see test/Makefile.
#include "ReflectJSON.h"
#include "ReflectINI.h"
#include <cstdio>
#include <cstring>
#include <optional>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

enum class Mode : uint8_t { Off = 0, Eco = 1, Sport = 2 };

struct Wifi {
  char ssid[33] = "";
  char pass[65] = "";
  bool dhcp = true;
  uint8_t ip[4] = {0, 0, 0, 0};
  REFLECT(MEMBER(ssid), MEMBER(pass), MEMBER(dhcp), MEMBER(ip))
};
struct Network {
  uint16_t port = 80;
  Wifi wifi;
  REFLECT(MEMBER(port), MEMBER(wifi))
};
struct Slot {
  char name[16] = "";
  int32_t value = 0;
  REFLECT(MEMBER(name), MEMBER(value))
};
struct Config {
  char deviceName[24] = "dev";
  int16_t offset = 0;
  float temp = 0;
  double gain = 0;
  float fixedV = 0;
  float rounded = 0;
  Mode mode = Mode::Off;
  char unit = 'C';
  std::optional<int> opt;
  const char *label = nullptr;
  int limits[3] = {0, 0, 0};
  char tags[2][8] = {"", ""};
  Network network;
  Slot slots[2];
  REFLECT(
    MEMBER(deviceName), MEMBER(offset), MEMBER(temp), MEMBER(gain),
    MEMBER(fixedV, fx(fixedV, 2)), MEMBER(rounded, fn(rounded, 1)),
    MEMBER(mode), MEMBER(unit), MEMBER(opt), MEMBER(label), MEMBER(limits), MEMBER(tags),
    MEMBER_AS("net", network), MEMBER(slots)
  )
};

struct Inner { int x = 1; REFLECT(MEMBER(x)) };

static void testRoundtrip(){
  Config c;
  strcpy(c.deviceName, "Bike ; #1");
  c.offset = -12;
  c.temp = 21.456f;
  c.gain = 0.12345;
  c.fixedV = 3.14f;
  c.rounded = 2.25f;
  c.mode = Mode::Sport;
  c.unit = 'F';
  c.opt = 7;
  c.label = "  padded \"q\"  ";
  c.limits[0] = 1; c.limits[1] = -2; c.limits[2] = 3;
  strcpy(c.tags[0], "a,b"); strcpy(c.tags[1], "plain");
  c.network.port = 8080;
  strcpy(c.network.wifi.ssid, "MyNet");
  strcpy(c.network.wifi.pass, "p=ss;w0rd\n");
  c.network.wifi.dhcp = false;
  c.network.wifi.ip[0] = 192; c.network.wifi.ip[1] = 168; c.network.wifi.ip[2] = 1; c.network.wifi.ip[3] = 50;
  strcpy(c.slots[0].name, "first"); c.slots[0].value = 100;
  strcpy(c.slots[1].name, "second"); c.slots[1].value = -100;

  INIwriter w(1024);
  w.comment("generated");
  w.write(c);
  CHECK(!w.hasError());
  if(w.hasError()) printf("writer: %s (%s)\n", w.getErrorMsg(), w.getErrorPath());
  const char *text = w.getINI();
  printf("---- written INI ----\n%s---------------------\n", text);

  INIreader r(64, 16, IniConfig::strict());
  CHECK(r.parse(text));
  if(r.hasParseError()) printf("parse: %s\n", r.getErrorMsg());
  Config d;
  CHECK(r.read(d));
  if(r.hasReadError()) printf("read: %s\n", r.getErrorMsg());
  CHECK(!strcmp(d.deviceName, "Bike ; #1"));
  CHECK(d.offset == -12);
  CHECK(d.temp > 21.45f && d.temp < 21.47f);
  CHECK(d.gain > 0.12349 && d.gain < 0.12351);   // double default precision 4
  CHECK(d.fixedV > 3.139f && d.fixedV < 3.141f);
  CHECK(d.rounded > 2.29f && d.rounded < 2.31f);   // fn(1): 2.25 -> "2.3"
  CHECK(d.mode == Mode::Sport);
  CHECK(d.unit == 'F');
  CHECK(d.opt.has_value() && *d.opt == 7);
  CHECK(d.label && !strcmp(d.label, "  padded \"q\"  "));
  CHECK(d.limits[0] == 1 && d.limits[1] == -2 && d.limits[2] == 3);
  CHECK(!strcmp(d.tags[0], "a,b") && !strcmp(d.tags[1], "plain"));
  CHECK(d.network.port == 8080);
  CHECK(!strcmp(d.network.wifi.ssid, "MyNet"));
  CHECK(!strcmp(d.network.wifi.pass, "p=ss;w0rd\n"));
  CHECK(!d.network.wifi.dhcp);
  CHECK(d.network.wifi.ip[0] == 192 && d.network.wifi.ip[3] == 50);
  CHECK(!strcmp(d.slots[0].name, "first") && d.slots[0].value == 100);
  CHECK(!strcmp(d.slots[1].name, "second") && d.slots[1].value == -100);

  // the same struct also works with ReflectJSON
  JSONwriter jw(2048, uint8_t(6));
  jw.write(d).finish();
  CHECK(!jw.hasError());
  if(jw.hasError()) printf("json: %s\n", jw.getErrorMsg());
}

static void testHandwritten(){
  const char *ini =
    "\xEF\xBB\xBF; device config\r\n"
    "deviceName = it's me  \r\n"
    "offset=0x10   ; hex\r\n"
    "temp = -1.5e1\r\n"
    "mode = 1\r\n"
    "unknownKey = whatever\r\n"
    "\r\n"
    "[ net ]\r\n"
    "port = 443 # comment\r\n"
    "[net.wifi]\r\n"
    "ssid = Home Net\r\n"
    "DHCP = Yes\r\n"
    "ip = 10, 0, 0, 1\r\n"
    "[slots.1]\r\n"
    "name = \"x\\x41y\"\r\n"
    "[NET]\r\n"
    "extra = 1\r\n"
    "[other]\r\n"
    "foo = bar\r\n";

  INIreader r(32, 8);
  CHECK(r.parse(ini));
  if(r.hasParseError()) printf("parse: %s\n", r.getErrorMsg());
  Config c;
  CHECK(r.read(c));
  if(r.hasReadError()) printf("read: %s\n", r.getErrorMsg());
  CHECK(!strcmp(c.deviceName, "it's me"));
  CHECK(c.offset == 16);
  CHECK(c.temp == -15.0f);
  CHECK(c.mode == Mode::Eco);
  CHECK(c.network.port == 443);
  CHECK(!strcmp(c.network.wifi.ssid, "Home Net"));
  CHECK(c.network.wifi.dhcp == true);
  CHECK(c.network.wifi.ip[0] == 10 && c.network.wifi.ip[3] == 1);
  CHECK(!strcmp(c.slots[1].name, "xAy"));

  // single reads
  uint16_t port = 0;
  CHECK(r.read(port, "port", "net") && port == 443);
  char ssid[33];
  CHECK(r.read(ssid, "ssid", "net.wifi") && !strcmp(ssid, "Home Net"));
  Wifi wifi;
  CHECK(r.read(wifi, "wifi", "net") && wifi.ip[3] == 1);
  Slot slots[2];
  CHECK(r.read(slots, "slots") && !strcmp(slots[1].name, "xAy"));
  int missing = 5;
  CHECK(!r.read(missing, "nope", "net") && r.getErrorCode() == IniReaderStatus::INI_ERR_KEY_MISSING && missing == 5);
  printf("missing: %s | path=%s\n", r.getErrorMsg(), r.getErrorPath());
  CHECK(!r.read(missing, "x", "nosection") && r.getErrorCode() == IniReaderStatus::INI_ERR_SECTION_MISSING);

  // strict: unknown key / section
  r.setConfig(IniConfig::strict());
  CHECK(!r.read(c));
  printf("strict: %s | path=%s line=%u\n", r.getErrorMsg(), r.getErrorPath(), r.getErrorLine());
}

static void expectParseError(const char *ini, IniReaderStatus code){
  INIreader r(16, 4);
  const bool ok = r.parse(ini);
  CHECK(!ok && r.getErrorCode() == code);
  printf("parse error: %s\n", r.getErrorMsg() ? r.getErrorMsg() : "(none)");
}
template<typename T>
static void expectReadError(const char *ini, IniReaderStatus code){
  INIreader r(16, 4);
  CHECK(r.parse(ini));
  T v{};
  CHECK(!r.read(v, "v") && r.getErrorCode() == code);
  printf("read error: %s\n", r.getErrorMsg() ? r.getErrorMsg() : "(none)");
}

static void testErrors(){
  expectParseError("[abc\nx=1", IniReaderStatus::INI_ERR_MISSING_BRACKET);
  expectParseError("x=1\nfoo\n", IniReaderStatus::INI_ERR_MISSING_EQUALS);
  expectParseError("x=1\nX = 2\n", IniReaderStatus::INI_ERR_DUPLICATE_KEY);
  expectParseError("x = \"abc\n", IniReaderStatus::INI_ERR_MISSING_QUOTE);
  expectParseError("x = \"abc\" d\n", IniReaderStatus::INI_ERR_UNEXPECTED_CHAR);
  expectParseError("[a] b\n", IniReaderStatus::INI_ERR_UNEXPECTED_CHAR);
  expectParseError("[]\n", IniReaderStatus::INI_ERR_EMPTY_SECTION_NAME);
  expectParseError(" = 3\n", IniReaderStatus::INI_ERR_EMPTY_KEY);
  expectParseError("[a]\n[b]\n[c]\n[d]\n[e]\n", IniReaderStatus::INI_ERR_SECTION_LIMIT);

  expectReadError<uint8_t>("v = 300", IniReaderStatus::INI_ERR_TYPE_OVERFLOW);
  expectReadError<uint8_t>("v = -1", IniReaderStatus::INI_ERR_TYPE_UNDERFLOW);
  expectReadError<int>("v = 12abc", IniReaderStatus::INI_ERR_TYPE_MISMATCH);
  expectReadError<int>("v = 1.5", IniReaderStatus::INI_ERR_TYPE_MISMATCH);
  expectReadError<bool>("v = maybe", IniReaderStatus::INI_ERR_TYPE_MISMATCH);
  expectReadError<float>("v = 1e999", IniReaderStatus::INI_ERR_TYPE_OVERFLOW);
  expectReadError<int[3]>("v = 1, 2", IniReaderStatus::INI_ERR_ARRAY_TOO_FEW_ELEMENTS);
  expectReadError<int[2]>("v = 1, 2, 3", IniReaderStatus::INI_ERR_ARRAY_TOO_MANY_ELEMENTS);
  expectReadError<char[4]>("v = toolong", IniReaderStatus::INI_ERR_STRING_LEN_LIMIT);
  expectReadError<int[2]>("v = 1, x", IniReaderStatus::INI_ERR_TYPE_MISMATCH);

  // empty values
  {
    INIreader r(16, 4);
    CHECK(r.parse("a =\nb = ;c\ns = \"\"\no ="));
    int a = 3; CHECK(r.read(a, "a") && a == 3);
    char s[4] = "zz"; CHECK(r.read(s, "s") && s[0] == '\0');
    char b[4] = "zz"; CHECK(r.read(b, "b") && b[0] == '\0');
    std::optional<int> o = 4; CHECK(r.read(o, "o") && !o.has_value());
    r.setConfig(IniConfig::strict());
    CHECK(!r.read(a, "a") && r.getErrorCode() == IniReaderStatus::INI_ERR_TYPE_MISMATCH);
  }
  // int limits
  {
    INIreader r(16, 4);
    CHECK(r.parse("a = -128\nb = 18446744073709551615\nc = -9223372036854775808\nd = 18446744073709551616\n"));
    int8_t a = 0; CHECK(r.read(a, "a") && a == -128);
    uint64_t b = 0; CHECK(r.read(b, "b") && b == UINT64_MAX);
    int64_t c = 0; CHECK(r.read(c, "c") && c == INT64_MIN);
    uint64_t d = 0; CHECK(!r.read(d, "d") && r.getErrorCode() == IniReaderStatus::INI_ERR_TYPE_OVERFLOW);
  }
}

static void testWriterErrors(){
  {
    INIwriter w(256);
    w.write(1, "a").write(2, "A");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_DUPLICATE_KEY);
    printf("writer: %s | %s\n", w.getErrorMsg(), w.getErrorPath());
  }
  {
    INIwriter w(256);
    w.startSection("s").write(1, "a").write(2, "a");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_DUPLICATE_KEY);
    printf("writer: %s | %s\n", w.getErrorMsg(), w.getErrorPath());
  }
  {
    INIwriter w(256);
    w.startSection("s").write(1, "a");
    w.write(2, "a");   // still in [s]
    CHECK(w.hasError());
  }
  {
    INIwriter w(32);
    w.write("a long value that does not fit in the buffer", "k");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_NO_SPACE && w.getINI() == nullptr);
    printf("writer: %s | %s\n", w.getErrorMsg(), w.getErrorPath());
  }
  {
    INIwriter w(256);
    w.write(1, "bad=key");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_INVALID_KEY);
  }
  {
    INIwriter w(256);
    float nan = NAN;
    w.write(nan, "f");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_INVALID_NUMBER);
  }
  {
    // global key after a section is not possible
    Inner in;
    INIwriter w(256);
    w.write(in, "inner").write(5, "late");
    CHECK(w.getErrorCode() == IniWriterStatus::INI_ERR_GLOBAL_AFTER_SECTION);
    printf("writer: %s | %s\n", w.getErrorMsg(), w.getErrorPath());
  }
  {
    // manual API
    INIwriter w(512, uint16_t(8), IniConfig{});
    int arr[3] = {1, 2, 3};
    w.write("hello", "greeting").write(arr, "list").startSection("extra").comment("line1\nline2").write(true, "flag");
    CHECK(!w.hasError());
    printf("---- manual ----\n%s----------------\n", w.getINI());
    INIreader r;
    CHECK(r.parse(w.getINI()));
    bool flag = false; CHECK(r.read(flag, "flag", "extra") && flag);
    char g[8]; CHECK(r.read(g, "greeting") && !strcmp(g, "hello"));
  }
}

int main(){
  testRoundtrip();
  testHandwritten();
  testErrors();
  testWriterErrors();
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL OK", failures);
  return failures ? 1 : 0;
}

# ReflectINI

Fast INI **writer and reader for microcontrollers** (RP2040/RP2350, ESP32/ESP32-S3, Teensy 4.x, Arduino UNO R4) with **struct reflection**:
describe a struct once, and read or write it as an INI file without hand-written parsing code.

```cpp
#include <ReflectINI.h>

struct Wifi {
  char ssid[33] = "";
  bool dhcp = true;
  uint8_t ip[4] = {0, 0, 0, 0};
  REFLECT(MEMBER(ssid), MEMBER(dhcp), MEMBER(ip))
};

struct Config {
  char name[24] = "bike";
  uint16_t port = 80;
  Wifi wifi;                                   // nested struct -> [wifi]
  REFLECT(MEMBER(name), MEMBER(port), MEMBER(wifi))
};

void setup() {
  Serial.begin(115200);

  Config cfg;
  INIreader reader(32, 8);                     // up to 32 keys and 8 sections
  if (reader.parse("port = 8080\n[wifi]\nssid = HomeNet\nip = 192, 168, 1, 50\n") && reader.read(cfg)) {
    Serial.println(cfg.port);                  // 8080
  }

  INIwriter writer(512);
  writer.write(cfg);
  Serial.println(writer.getINI());
}
```

```ini
name = bike
port = 8080

[wifi]
ssid = HomeNet
dhcp = true
ip = 192, 168, 1, 50
```

## Features

- **Reflection**: `REFLECT(MEMBER(a), MEMBER(b))` maps structs, nested structs, arrays and arrays of structs in both
  directions. The same macros as [ReflectJSON](https://github.com/Michael-0-sys/ReflectJSON): one struct works for
  JSON **and** INI.
- **Writer**: buffer-based writer with validation (key / section names, duplicate keys, buffer size), automatic quoting
  and escaping of strings, comments, `std::optional`, struct flattening, `fn()` / `fx()` precision control.
- **Reader**: single-pass parser with strict syntax checks, reads whole documents into structs, single sections or
  single values. Read-only by default; optional zero-copy strings for writable buffers.
- **Configurable** (`IniConfig`): unknown keys / sections, required keys, empty values, case sensitivity, output format.
- **Precise errors**: a message (*what* is wrong) plus the location via `getErrorPath()` (e.g. `wifi.ip[2]`),
  `getErrorLine()` and `getErrorColumn()`.
- **Exact number output**: floats are formatted with integer arithmetic only (fast on MCUs without FPU) and
  correctly rounded from the binary value.
- **Made for small MCUs**: no exceptions, no RTTI, no `std::string`; heap use only for the buffers you size in the
  constructors.

## Installation

- **Arduino IDE**: Sketch > Include Library > Add .ZIP Library... (use the repository zip), or copy the folder to
  `Arduino/libraries/ReflectINI`.
- **PlatformIO**: add the repository URL to `lib_deps`. The library needs C++17 (`build_flags = -std=gnu++17`,
  `build_unflags = -std=gnu++11 -std=gnu++14`; ESP32 and arduino-pico already default to C++17 in recent cores).

Include everything with `#include <ReflectINI.h>` (or only `ReflectINI_writer.h` / `ReflectINI_reader.h`).

### Using ReflectINI together with ReflectJSON

Both libraries ship an identical copy of `ReflectCore.h` (`REFLECT`, `MEMBER`, `fn()`, `fx()`, number formatting).
ReflectINI does **not** need ReflectJSON. If you use both in one sketch, install **ReflectJSON 1.1.0 or newer**
(the first release with `ReflectCore.h`). Two different `ReflectCore.h` versions in one build stop with a clear
compile error instead of mixing silently. See `examples/15_INI_JSON_SameStruct`.

## How C++ maps to INI

| C++ | INI |
|---|---|
| scalar (`int`, `float`, `double`, `bool`, `enum`, `char`) | `key = value` |
| `char[N]`, `const char*`, Arduino `String` | `key = text` (quoted only when needed) |
| `T[N]` | `key = 1, 2, 3` |
| `char[M][N]` | `key = a, "b,c"` (list of strings) |
| reflected struct | `[name]`, nested `[name.sub]` |
| array of structs `S[N]` | `[name.0]`, `[name.1]`, ... |
| `std::optional<T>` / `nullptr` | key omitted when empty |
| `fn(x, 1)` / `fx(x, 3)` | 1 decimal place / integer `x * 1000` |

**Note:** unlike ReflectJSON, `char[N]` is a **string** in INI, not an array of characters. It is the best choice
for strings: the value is copied into the struct. A `char[N]` holds at most N-1 characters plus `'\0'`.

## INI syntax

```ini
; comment                     (also # comment; after a value only when preceded by a blank)
global = 1                    ; keys before the first section are global

[section]
key = value                   ; blanks around key and value are trimmed
text = "a ; b \"quoted\"\n"   ; double quotes: \\ \" \' \n \r \t \0 \xHH
raw = 'C:\no\escapes'         ; single quotes: no escapes
list = 1, 2, "three, 3"       ; arrays

[section.sub]                 ; nested struct
```

- bool: `true/false`, `yes/no`, `on/off`, `1/0` (case-insensitive). Integers: decimal or `0x` hex. Floats: `1.5`, `-2e3`, `.5`.
- Line endings `\n`, `\r\n` or `\r`, optional UTF-8 BOM.
- A section header that appears twice is merged. Duplicate keys in one section are an error.
- Keys and section names are case-insensitive by default.
- Names: keys must not contain `= [ ] ; #` or control characters, section names must not contain `[ ]` or control
  characters; blanks inside are allowed, at the ends they are trimmed. Reader and writer use the same rules.
- No multi-line values.

## Writing

```cpp
INIwriter writer(1024);                 // output buffer in bytes, up to 64 keys (duplicate check)
INIwriter writer2(1024, 128);           // up to 128 keys
INIwriter quiet(1024, false);           // no error messages / paths

writer.comment("generated");
writer.write(cfg);                      // whole struct: global keys first, then its sections
writer.write(42, "answer");             // key in the current section
writer.write("hello", "text");          // string literals work directly (char[N] is a string)
writer.write(wifi, "wifi");             // struct -> [wifi]
writer.startSection("limits");          // following keys go into [limits]
writer.write(25, "speed");

const char *ini = writer.getINI();      // complete after every write(); nullptr after an error
size_t size = writer.getINISize();
writer.resetINI();                      // start a new document
```

- **Global keys must come first.** INI has no end marker for a section, so a global key after a section is an error
  (`INI_ERR_GLOBAL_AFTER_SECTION`). `write(struct)` orders this automatically.
- A struct without a key is **flattened** into the current section.
- Empty `std::optional` and `nullptr` strings are omitted.
- Strings are quoted automatically when they would otherwise be read differently (blanks at the ends, `;`, `#`,
  quotes, line breaks, commas in arrays).

## Reading

```cpp
INIreader reader(64, 16);               // up to 64 keys and 16 sections

if (!reader.parse(text)) { /* syntax error */ }

reader.read(cfg);                       // whole document into a struct
reader.read(port, "port");              // global key
reader.read(ssid, "ssid", "wifi");      // key in [wifi]
reader.read(wifi, "wifi");              // section [wifi] into a struct
reader.read(slots, "slots");            // [slots.0], [slots.1], ... into an array of structs
```

- **The INI text is not modified** by default (string literals and flash are fine). It must stay unchanged and alive
  while values are read; every `read()` checks this with a 64-bit hash (`setBufferCheck(false)` turns it off).
- **Zero-copy**: `parse(buffer, true)` allows `const char*` members to point directly into a writable buffer (the
  character behind the value is overwritten with `'\0'`). Do not parse the same buffer again afterwards. Arduino
  `String` is read-only by default, too: zero-copy only with `parse(str, true)`.
- Otherwise `const char*` values are copied into one of 8 shared static buffers. They stay valid until the next
  `read()` that copies strings. One `read()` that needs more than 8 copies fails with `INI_ERR_STRING_BUFFER_LIMIT`
  instead of returning overwritten pointers. Prefer `char[N]` members.
- On failure the target variable is left unchanged.
- **Nullable targets** (`std::optional`, `const char*`, `char*`): a missing key or section is not an error, the target
  becomes empty / `nullptr` - exactly the values the writer omits.

## Configuration

`IniConfig` is shared by reader and writer:

| Setting | Default | Meaning |
|---|---|---|
| `ignoreUnknownKeys` | `true` | reader: unknown keys are skipped (otherwise error) |
| `ignoreUnknownSections` | `true` | reader: unknown sections are skipped (otherwise error) |
| `requireAllKeys` | `false` | reader: a missing key is an error (otherwise the default is kept). A missing nullable member becomes null. |
| `coerceInputValues` | `true` | reader: an empty value (`key =`) for a number / bool keeps the default |
| `caseSensitive` | `false` | compare keys and section names case-sensitively; fixed per document |
| `spaceAroundEquals` | `true` | writer: `key = value` instead of `key=value` |
| `blankLineBeforeSection` | `true` | writer: empty line in front of every `[section]` |

```cpp
INIreader strictReader(64, 16, IniConfig::strict());   // all keys required, no unknown keys / sections, no empty values
```

## Errors

```cpp
if (!reader.read(cfg)) {
  Serial.println(reader.getErrorMsg());     // cannot convert 'fast' to int32_t
  Serial.println(reader.getErrorPath());    // status.motor.speed
  Serial.println(reader.getErrorLine());    // 2
}
```

- The **message says what** is wrong; it never contains the location.
- **Where**: `getErrorPath()` (read errors, e.g. `wifi.ip[2]`, `slots.1.name`; `nullptr` if there is no location),
  `getErrorLine()` (parse and read errors) and `getErrorColumn()` (parse errors, in bytes).
- `getErrorCode()` returns an `IniReaderStatus` / `IniWriterStatus`; the writer also has `getErrorPath()`.
- With `createErrorInfo = false` no messages and paths are generated (codes, line and column still work).

## Numbers

- `float` is written with 2, `double` with 4 decimal places; `fn(x, n)` sets 0..8 places, `fx(x, n)` writes the
  integer `x * 10^n`.
- The output is **correctly rounded** from the binary value (round half up), computed with integer arithmetic only.
  Values from 2^64 (about 1.8e19) on are written in exponential notation.
- Integers are written with two digits per division (fast on MCUs without hardware division).
- `long double` is not supported (it differs between platforms); NaN and infinity are errors.

## Limits

| What | Limit |
|---|---|
| Key length | 32 |
| Section name length (full dotted path) | 64 |
| String length | 254 |
| INI size for the reader | 65535 bytes |
| Keys / sections (reader) | chosen in the constructor (max. 65535 / 254) |
| Keys (writer, duplicate check) | chosen in the constructor (default 64) |
| Decimal places | 8 |

Memory: the reader needs 12 bytes per key and 6 bytes per section, plus 9 x 255 bytes of static string buffers; the
writer needs its output buffer plus 8 bytes per key. No heap use while reading or writing.

## Notes

- Requires **C++17**. Intended targets: RP2040/RP2350 (arduino-pico), ESP32 / ESP32-S3, Teensy 4.x, Arduino UNO R4
  Minima/WiFi. AVR, ESP8266, SAM/SAMD and STM32 cores are not supported.
- Not thread-safe: the static string buffers are shared. Use one task/core at a time.
- Structs must be declared at namespace scope (local structs cannot use `REFLECT`).
- Reflection expressions (`MEMBER(x, expr)`, `MEMBER_AS(key, expr)`) should be side-effect-free; they are evaluated
  once per `read()` / `write()` of the struct.
- For sections, `.` is the path separator: a `MEMBER_AS("a.b", x)` must not alias the section of another member
  (`MEMBER(a)` with a member `b`), otherwise both write into `[a.b]`.
- Time-critical code: run `examples/14_IniSpeedTest` on your board. Timings differ a lot between MCUs.

## Examples

| Example | Shows |
|---|---|
| `01_Writer_Minimal` | the smallest writer |
| `02_Writer_Sections_Arrays` | sections, nested sections, arrays, numbered sections |
| `03_Writer_Struct_Config` | a reflected struct, `fn()` / `fx()`, `MEMBER_AS`, compact formatting |
| `04_Writer_Maximum` | most writer features in one sketch |
| `05_Reader_Minimal` | the smallest reader |
| `06_Reader_Sections` | values and structs from (nested) sections |
| `07_Reader_Struct_Config` | a struct with default and strict configuration |
| `08_Reader_Maximum` | most reader features, zero-copy, buffer check |
| `09_Writer_Reader_Roundtrip` | write a struct and read it back |
| `10_Writer_ErrorMessages` / `11_Reader_ErrorMessages` | every kind of error with message, path, line and column |
| `12_Writer_Formatting` | `IniConfig` output format |
| `13_Writer_Struct_Flatten` | structs with and without key, arrays of structs |
| `14_IniSpeedTest` | timings on your board |
| `15_INI_JSON_SameStruct` | one struct as JSON and INI (needs ReflectJSON >= 1.1.0) |

## Tests

```
make -C test            # unit tests on the PC, AddressSanitizer + UndefinedBehaviorSanitizer
make -C test examples   # builds and runs the examples against a small Arduino stand-in
make -C test json REFLECTJSON=../../ReflectJSON/src   # ReflectINI + ReflectJSON together
```

## License

Dual license:

- **Noncommercial use** (private, hobby, education, research, non-profit) is free under the
  [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0).
- **Commercial use requires a separate written license**; see [COMMERCIAL.md](COMMERCIAL.md).

This is a source-available license, not an OSI-approved open-source license.

# Changelog

## 1.0.0

First public release.

- `INIwriter`: buffer-based writer with validation (key / section names, duplicate keys, buffer size, global keys
  after sections), struct reflection via `REFLECT` / `MEMBER` / `MEMBER_AS`, nested structs as `[a.b]`, arrays of
  structs as `[name.0]`, scalar arrays as `a, b, c`, `std::optional`, struct flattening, comments, automatic
  quoting and escaping of strings.
- `INIreader`: single-pass parser with strict syntax checks (quotes, escapes, names, duplicate keys), reflection
  mapping into structs, sections, arrays and single values. Read-only by default; optional zero-copy strings with
  `parse(buffer, true)`; 64-bit FNV-1a buffer check (`setBufferCheck()`).
- `IniConfig` shared by reader and writer: `ignoreUnknownKeys`, `ignoreUnknownSections`, `requireAllKeys`,
  `coerceInputValues`, `caseSensitive`, `spaceAroundEquals`, `blankLineBeforeSection`; `IniConfig::strict()`.
- Error locations: `getErrorPath()` (e.g. `wifi.ip[2]`), `getErrorLine()`, `getErrorColumn()`. Error messages
  describe only *what* is wrong.
- Shares `ReflectCore.h` (version 5) with ReflectJSON >= 1.1.0: one struct works for JSON and INI.
- Exact, correctly rounded float output with integer arithmetic only; fast integer output (two digits per division).
- Supported: RP2040/RP2350, ESP32 / ESP32-S3, Teensy 4.x, Arduino UNO R4 Minima/WiFi.

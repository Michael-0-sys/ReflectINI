/*
 ReflectINI - INI writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once
#include "ReflectINI_tools.h"

// Error and status codes returned by INIreader.
// INI_READER_OK (0) indicates successful parsing or reading with no errors.
enum class IniReaderStatus {
  // general
  INI_READER_OK = 0,                // No error
  INI_ERR_EMPTY_INI,                // Input is nullptr
  INI_ERR_INI_NOT_PARSED,           // Access attempted before parsing
  INI_ERR_INI_TOO_LARGE,            // INI is longer than INI_MAX_LEN bytes
  INI_ERR_OUT_OF_MEMORY,            // The internal buffers could not be allocated (constructor)
  INI_ERR_INVALID_KEY,              // nullptr key / section name in read()

  // parsing errors
  INI_ERR_MISSING_BRACKET,          // "[section" without closing ']'
  INI_ERR_EMPTY_SECTION_NAME,       // "[]"
  INI_ERR_SECTION_LEN_LIMIT,        // Section name exceeds INI_MAX_SECTION_LEN
  INI_ERR_SECTION_LIMIT,            // More sections than configured (constructor maxSections)
  INI_ERR_MISSING_EQUALS,           // Line is neither comment, section nor "key = value"
  INI_ERR_EMPTY_KEY,                // "= value"
  INI_ERR_KEY_LEN_LIMIT,            // Key exceeds INI_MAX_KEY_LEN
  INI_ERR_ENTRY_LIMIT,              // More keys than configured (constructor maxEntries)
  INI_ERR_DUPLICATE_KEY,            // Same key twice in the same section
  INI_ERR_MISSING_QUOTE,            // Quoted value without closing quote on the same line
  INI_ERR_UNEXPECTED_CHAR,          // Text after ']' or after a closing quote

  // mapping errors
  INI_ERR_KEY_MISSING,              // Key not in the INI (single read, or requireAllKeys)
  INI_ERR_SECTION_MISSING,          // Section not in the INI (single read)
  INI_ERR_EXTRA_KEY,                // INI contains a key not present in the struct (ignoreUnknownKeys = false)
  INI_ERR_EXTRA_SECTION,            // INI contains a section not present in the struct (ignoreUnknownSections = false)
  INI_ERR_TYPE_MISMATCH,            // Value cannot be converted to the target type
  INI_ERR_TYPE_UNDERFLOW,           // Value is smaller than target type allows
  INI_ERR_TYPE_OVERFLOW,            // Value exceeds target type range
  INI_ERR_STRING_LEN_LIMIT,         // String longer than char[N] / INI_MAX_STRING_LEN
  INI_ERR_ARRAY_TOO_MANY_ELEMENTS,  // INI list has more elements than the target array
  INI_ERR_ARRAY_TOO_FEW_ELEMENTS,   // INI list has fewer elements than the target array

  // --- added later (appended so existing numeric values stay unchanged) ---
  INI_ERR_INVALID_KEY_NAME,         // Key in the INI contains [ ] ; # or control characters (same rule as INIwriter)
  INI_ERR_INVALID_SECTION_NAME,     // Section name contains [ or control characters (same rule as INIwriter)
  INI_ERR_INI_CHANGED,              // INI buffer was modified after parse() (setBufferCheck)
  INI_ERR_STRING_BUFFER_LIMIT       // One read() needs more const char* copies than INI_ROTATING_STR_BUFFER_COUNT
};

// INIreader parses an INI document into a small table (sections + key/value positions)
// and maps values onto REFLECT structs.
//
//   INIreader ini(64, 16);            // up to 64 keys and 16 sections
//   if(ini.parse(text) && ini.read(config)) { ... }
//
// Section names are built from the reflection keys, '.' is the path separator (see INIwriter):
// a MEMBER_AS("a.b", x) must not alias another reflected section path such as MEMBER(a) -> member b.
//
// Supported syntax:
//   ; comment  /  # comment            whole line, or after a value (must be preceded by a blank)
//   [section]  /  [section.sub]        nested structs, arrays of structs as [name.0], [name.1], ...
//   key = value                        blanks around key and value are trimmed
//   key = "text ; with \"escapes\""    double quotes: \\ \" \' \n \r \t \0 \xHH
//   key = 'raw text'                   single quotes: no escapes
//   list = 1, 2, 3                     arrays T[N] (elements may be quoted)
//   bool: true/false, yes/no, on/off, 1/0 (case-insensitive)
//   integers: decimal or 0x hex, floats: 1.5, -2e3
//   Line endings \n, \r\n or \r, optional UTF-8 BOM.
//
// The INI buffer must stay unchanged and alive between parse() and read() (checked, see setBufferCheck()).
// Read-only buffers (default) are never modified. With parse(ini, true) const char* members may point
// directly into the buffer (zero-copy, see parse()).
// The reader stores only offsets: sizeof(IniEntry) = 12 bytes per key (11 bytes data + alignment),
// sizeof(IniSection) = 6 bytes per section.
//
// MEMBER(x, expr) / MEMBER_AS(key, expr): reflection expressions must be side-effect-free.
// They are evaluated once per read()/write() of the struct (and once per element of a struct array).
INI_MSG_TRUNCATION_BEGIN   // error messages are cut off on purpose (see ReflectINI_tools.h)
class INIreader {
public:
    // INIreader(64, false) means "no error info"; without these overloads the bool would
    // silently be taken as maxSections.
    template<typename B, typename = std::enable_if_t<std::is_same_v<B, bool>>>
    explicit INIreader(uint16_t maxEntries, B createErrorInfo)
      : INIreader(maxEntries, uint8_t(16), createErrorInfo){}
    explicit INIreader(uint16_t maxEntries, uint8_t maxSections, const IniConfig &config, bool createErrorInfo = true)
      : INIreader(maxEntries, maxSections, createErrorInfo){
      _config = config;
    }
    // Constructs an INIreader.
    //
    // maxEntries: maximum number of key = value lines in the whole document (minimum 1).
    // maxSections: maximum number of distinct [sections] (minimum 1, maximum 254).
    //
    // If the allocation fails, parse() returns false with INI_ERR_OUT_OF_MEMORY.
    explicit INIreader(uint16_t maxEntries = 64, uint8_t maxSections = 16, bool createErrorInfo = true){
      _maxEntries = maxEntries == 0 ? 1 : maxEntries;
      _maxSections = uint8_t((maxSections == 0 ? 1 : (maxSections > 254 ? 254 : maxSections)) + 1);   // + global section
      _createErrorInfo = createErrorInfo;
      _entries = new (std::nothrow) IniEntry[_maxEntries];
      _sections = new (std::nothrow) IniSection[_maxSections];
      resetAll();
    }
    ~INIreader(){
      delete[] _entries;
      delete[] _sections;
    }
    INIreader(const INIreader&) = delete;
    INIreader &operator=(const INIreader&) = delete;

    // Settings shared with INIwriter (see IniConfig). Applies to all following read() calls.
    // Exception: caseSensitive is taken over by parse() and stays fixed for that document (the duplicate check
    // of the parser and all key/section comparisons of the reads always use the same mode).
    INIreader &setConfig(const IniConfig &config){ _config = config; return *this; }
    const IniConfig &getConfig() const{ return _config; }

    // Enables or disables the integrity check of the INI buffer on every read.
    //
    // When enabled (default), every read() compares a 64-bit hash (FNV-1a) of the buffer with the hash
    // from parse() and fails with INI_ERR_INI_CHANGED if it differs. This detects accidental modifications
    // (overwritten buffer, reused String, ...) with overwhelming probability. It is NOT a security feature:
    // FNV-1a is not cryptographic, deliberately constructed collisions are possible.
    // The check costs one pass over the INI per read call (plus one more after a zero-copy read).
    // It can be disabled when the buffer is guaranteed to stay untouched between parse() and the reads.
    void setBufferCheck(bool on){ _verifyBuffer = on; }
    bool isBufferCheckEnabled() const{ return _verifyBuffer; }

    // Enables or disables creation of extended error information (message, path).
    void setCreateErrorInfo(bool on){ _createErrorInfo = on; }
    bool isCreateErrorInfoEnabled() const{ return _createErrorInfo; }

    // Number of parsed key = value lines / sections (without the global section). 0 if not parsed / parse error.
    uint16_t getEntryCount() const{ return _parsed ? _entryCount : 0; }
    uint8_t getSectionCount() const{ return (_parsed && _sectionCount > 0) ? uint8_t(_sectionCount - 1) : 0; }
    uint16_t getMaxEntries() const{ return _maxEntries; }
    uint8_t getMaxSections() const{ return uint8_t(_maxSections - 1); }

    bool isINIParsed() const{ return _parsed; }
    bool hasParseError() const{ return _hasParseError; }
    bool hasReadError() const{ return _hasReadError; }

    // Returns the error code of the last error, or INI_READER_OK (0).
    IniReaderStatus getErrorCode() const{ return _code; }
    // Returns the human-readable message of the last parse or read error,
    // or nullptr if there is no error or createErrorInfo is disabled.
    const char *getErrorMsg() const{
      if(!_createErrorInfo || !(_hasParseError || _hasReadError)) return nullptr;
      return _message;
    }
    // Returns the location of the last read error, e.g. "port", "network.wifi.ssid", "motor.limits[2]", "slots.1.name".
    // Returns nullptr if there is no read error, no location or createErrorInfo is disabled.
    const char *getErrorPath() const{
      if(!_createErrorInfo || !_hasReadError || !_errPath[0]) return nullptr;
      return _errPath;
    }
    // Returns the line (1-based) of the last parse or read error, or 0 if the error has no line.
    uint16_t getErrorLine() const{ return (_hasParseError || _hasReadError) ? _errLine : 0; }
    // Returns the column (1-based, in bytes: a UTF-8 multibyte character counts as several columns)
    // of the last parse error, or 0 for read errors and errors without a position.
    //
    // The error text never contains the location: use getErrorPath() / getErrorLine() / getErrorColumn().
    // 32 bit: a line can be INI_MAX_LEN (65535) bytes long, the position of its terminating '\0' is column 65536.
    uint32_t getErrorColumn() const{ return _hasParseError ? _errCol : 0; }

  #ifdef ARDUINO
    // Arduino String: read-only by default (the String is never modified).
    // parse(ini, true) explicitly allows zero-copy: const char* members then point into the String and the
    // characters behind those values are overwritten with '\0' (the String content changes!).
    // Unlike JSONreader::parse(String&), zero-copy is NOT enabled automatically.
    inline bool parse(String &ini, bool writableINIBuffer = false){ return parse(ini.c_str(), writableINIBuffer); }
    inline bool parse(const String &ini){ return parse(ini.c_str(), false); }
    template<typename T>
    inline bool read(T &out, const String &key){ return read(out, key.c_str(), nullptr); }
    template<typename T>
    inline bool read(T &out, const String &key, const String &section){ return read(out, key.c_str(), section.c_str()); }
  #endif

    // Parses a zero-terminated INI document.
    //
    // If writableINIBuffer is true, read() may modify the INI buffer (zero-copy string views, same as
    // JSONreader::parse(json, true)).
    // IMPORTANT: Set to false for read-only or non-writable buffers
    // (e.g. string literals, Flash/rodata, PROGMEM). Otherwise, undefined behavior (UB) will occur.
    //
    // Strings read into const char* / char* members:
    // - writable buffer and no escape sequences: the pointer points into the INI buffer (the character
    //   behind the value is overwritten with '\0') and stays valid as long as the buffer exists.
    //   Do not parse() the same buffer again afterwards, the overwritten characters are missing then.
    // - otherwise (read-only buffer, escapes, array elements) the string is copied into one of
    //   INI_ROTATING_STR_BUFFER_COUNT shared static buffers. If one read() needs more of these copies,
    //   it fails with INI_ERR_STRING_BUFFER_LIMIT (instead of returning overwritten pointers).
    //   The pointers stay valid until the next read() that copies strings (by any INIreader instance).
    //   Copy the string if you need it longer.
    // char[N] and Arduino String members are always copied.
    bool parse(const char *ini, bool writableINIBuffer = false){
      resetAll();
      if(!_entries || !_sections) return parseError(IniReaderStatus::INI_ERR_OUT_OF_MEMORY, nullptr, "%s", "out of memory");
      if(!ini) return parseError(IniReaderStatus::INI_ERR_EMPTY_INI, nullptr, "%s", "INI buffer is nullptr");

      const size_t len = iniBoundedLen(ini, INI_MAX_LEN);
      if(len > INI_MAX_LEN) return parseError(IniReaderStatus::INI_ERR_INI_TOO_LARGE, nullptr, "INI too large (limit=%u bytes)", unsigned(INI_MAX_LEN));

      _text = ini;
      _len = uint16_t(len);
      _writable = writableINIBuffer;
      _cs = _config.caseSensitive;
      _sections[0] = IniSection{0, 0, 0, 0};    // global section
      _sectionCount = 1;

      const char *p = ini;
      if((uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;   // UTF-8 BOM
      _line = 1;
      _lineStart = p;
      uint8_t cur = 0;

      while(*p){
        while(INI_IS_BLANK(*p)) p++;
        const char c = *p;
        if(c == ';' || c == '#'){
          while(!INI_IS_EOL(*p)) p++;
        }else if(c == '['){
          if(!parseSection(p, cur)) return false;
        }else if(!INI_IS_EOL(c)){
          if(!parseEntry(p, cur)) return false;
        }
        // p points at the end of the line
        if(*p == '\r'){ p++; if(*p == '\n') p++; }
        else if(*p == '\n'){ p++; }
        _line++;
        _lineStart = p;
      }

      _checksum = iniHash(_text, _len);
      _parsed = true;
      return true;
    }

    // Reads the whole document into a REFLECT struct:
    // scalar members = global keys, struct members = [sections].
    //
    // On failure the target variable is left unchanged.
    template<typename T>
    bool read(T &out){
      static_assert(is_reflectable_v<T>, "read(out) without key needs a REFLECT struct (the whole INI document)");
      const bool ok = readDocument(out);
      endRead();
      return ok;
    }

    // Reads a single value.
    //
    //   read(port, "port")                    global key
    //   read(ssid, "ssid", "network.wifi")    key in [network.wifi]
    //   read(wifi, "wifi", "network")         REFLECT struct: section [network.wifi]
    //   read(slots, "slots")                  array of structs: [slots.0], [slots.1], ...
    //
    // Fails with INI_ERR_KEY_MISSING / INI_ERR_SECTION_MISSING if it does not exist.
    // Exception: nullable targets (std::optional, const char*, char*) - a missing key or section is not an error,
    // the target becomes empty / nullptr. INIwriter::write() omits exactly these values, so both directions match.
    // On failure the target variable is left unchanged.
    template<typename T>
    bool read(T &out, const char *key, const char *section = nullptr){
      const bool ok = readSingle(out, key, section);
      endRead();
      return ok;
    }

private:
  REGION(readapi)
    template<typename T>
    bool readDocument(T &out){
      if(!beginRead("")) return false;

      T tmp{};
      copyValue(tmp, out);
      if(!readStruct(tmp, 0)) return false;

      if(!_config.ignoreUnknownSections){
        for(uint8_t s = 1; s < _sectionCount; s++){
          if(_sections[s].flags & FLAG_USED) continue;
          const IniSection &sec = _sections[s];
          return fail(IniReaderStatus::INI_ERR_EXTRA_SECTION, sec.line, _text + sec.nameStart, sec.nameLen,
                      nullptr, 0, -1, "%s", "unknown section");
        }
      }

      copyValue(out, tmp);
      return true;
    }

    template<typename T>
    bool readSingle(T &out, const char *key, const char *section){
      if(!beginRead(key)) return false;

      if(section && *section){
        const size_t sl = iniBoundedLen(section, INI_MAX_SECTION_LEN);
        if(!pushName(section, sl, nullptr)) return false;
      }

      static_assert(!is_json_fixed_v<remove_cvref_t<T>> && !is_json_number_v<remove_cvref_t<T>>,
                    "JsonFixed/JsonNumber cannot be read directly (they only reference a variable). "
                    "Use a struct member instead: struct Data { float x; REFLECT(MEMBER(x, fx(x, 2))) };");

      const size_t keyLen = strlen(key);
      T tmp{};
      copyValue(tmp, out);

      if constexpr (isIniSection<T>()){
        const uint8_t old = _secLen;
        if(!pushName(key, keyLen, key)) return false;
        const bool exists = findSection(_sec, _secLen) >= 0 || hasSectionPrefix(_sec, _secLen);
        popName(old);
        if(!exists){
          return fail(IniReaderStatus::INI_ERR_SECTION_MISSING, 0, _sec, _secLen, key, keyLen, -1, "%s", "missing section");
        }
        FieldRef<T&> f{key, uint8_t(keyLen > 255 ? 255 : keyLen), tmp};
        if(!readField(f, -1)) return false;
      }else{
        const int16_t sec = findSection(_sec, _secLen);
        const int16_t e = sec >= 0 ? findEntry(uint8_t(sec), key, keyLen) : int16_t(-1);
        if(e < 0 && isIniNullable<T>()){
          // nullable target: missing key (or section) = null, symmetric to INIwriter::write(), which omits
          // an empty optional / nullptr (and a section that would contain nothing else)
          iniSetNull(out);
          return true;
        }
        if(sec < 0){
          return fail(IniReaderStatus::INI_ERR_SECTION_MISSING, 0, _sec, _secLen, nullptr, 0, -1, "%s", "missing section");
        }
        if(e < 0){
          return fail(IniReaderStatus::INI_ERR_KEY_MISSING, 0, _sec, _secLen, key, keyLen, -1,
                      "missing key (expected %s)", iniTypeName<T>());
        }
        if(!readValue(tmp, e, key)) return false;
      }

      copyValue(out, tmp);
      return true;
    }

    // After every read (success or failure): zero-copy reads changed the buffer -> new reference hash
    inline void endRead(){
      if(_zeroCopyDirty){
        _checksum = iniHash(_text, _len);
        _zeroCopyDirty = false;
      }
    }
  ENDREGION()

  REGION(deklaration)
    enum Flags : uint8_t{
      FLAG_USED = 1,    // read state: entry / section was mapped to a struct member
      FLAG_DQ   = 2,    // parse: the whole value is enclosed in "..."
      FLAG_SQ   = 4,    // parse: the whole value is enclosed in '...'
      FLAG_ESC  = 8     // parse: "..." value contains a backslash (needs decoding, no zero-copy)
    };
    struct IniEntry{
      uint16_t keyStart;
      uint16_t valStart;
      uint16_t valLen;
      uint16_t line;
      uint8_t keyLen;
      uint8_t section;
      uint8_t flags;
    };
    struct IniSection{
      uint16_t nameStart;
      uint16_t line;
      uint8_t nameLen;
      uint8_t flags;
    };

    const char *_text = nullptr;
    IniEntry *_entries = nullptr;
    IniSection *_sections = nullptr;
    uint16_t _maxEntries = 0;
    uint16_t _entryCount = 0;
    uint8_t _maxSections = 0;
    uint8_t _sectionCount = 0;

    uint16_t _len = 0;
    uint64_t _checksum = 0;       // iniHash() of the buffer (setBufferCheck)
    bool _zeroCopyDirty = false;  // a zero-copy read wrote '\0' into the buffer: hash must be recomputed
    uint8_t _ringUsed = 0;        // rotating string buffers used by the current read()
    bool _writable = false;       // parse(ini, true): zero-copy strings allowed
    bool _verifyBuffer = true;    // setBufferCheck()
    bool _cs = false;             // caseSensitive of the parsed document (fixed by parse())

    // parse state
    uint16_t _line = 0;
    const char *_lineStart = nullptr;

    // read state: full name of the current section ("" = global)
    char _sec[INI_MAX_SECTION_LEN + 1] = {0};
    uint8_t _secLen = 0;

    IniConfig _config;
    IniReaderStatus _code = IniReaderStatus::INI_READER_OK;
    char _message[INI_ERR_MAX_MSG] = {0};
    char _errPath[INI_ERR_MAX_PATH_LEN] = {0};
    uint16_t _errLine = 0;
    uint32_t _errCol = 0;
    bool _parsed = false;
    bool _hasParseError = false;
    bool _hasReadError = false;
    bool _createErrorInfo = true;
  ENDREGION()

  REGION(parse)
    // p points at '['. Afterwards p points at the end of the line.
    bool parseSection(const char *&p, uint8_t &cur){
      const char *q = p + 1;
      while(INI_IS_BLANK(*q)) q++;
      const char *ns = q;
      while(!INI_IS_EOL(*q) && *q != ']') q++;
      if(*q != ']') return parseError(IniReaderStatus::INI_ERR_MISSING_BRACKET, q, "%s", "missing ']'");

      const char *ne = q;
      while(ne > ns && INI_IS_BLANK(ne[-1])) ne--;
      const size_t nl = size_t(ne - ns);
      if(nl == 0) return parseError(IniReaderStatus::INI_ERR_EMPTY_SECTION_NAME, ns, "%s", "empty section name");
      if(nl > INI_MAX_SECTION_LEN) return parseError(IniReaderStatus::INI_ERR_SECTION_LEN_LIMIT, ns, "section name too long (limit=%u)", unsigned(INI_MAX_SECTION_LEN));
      if(!iniIsValidSectionName(ns, nl)){
        const char *bad = ns;
        while(bad < ne && (unsigned char)*bad >= 0x20 && *bad != 0x7F && *bad != '[') bad++;
        return parseError(IniReaderStatus::INI_ERR_INVALID_SECTION_NAME, bad, "invalid section name '%.*s' (must not contain [ or control characters)", int(nl), ns);
      }

      q++;
      while(INI_IS_BLANK(*q)) q++;
      if(!INI_IS_EOL(*q) && *q != ';' && *q != '#') { char cd[8]; return parseError(IniReaderStatus::INI_ERR_UNEXPECTED_CHAR, q, "unexpected character %s after ']'", charDesc(*q, cd)); }
      while(!INI_IS_EOL(*q)) q++;

      // the same section may appear more than once: its keys are merged
      int16_t id = findSection(ns, nl);
      if(id < 0){
        if(_sectionCount >= _maxSections) return parseError(IniReaderStatus::INI_ERR_SECTION_LIMIT, p, "maximum section count exceeded (limit=%u)", unsigned(_maxSections - 1));
        id = _sectionCount++;
        _sections[id] = IniSection{uint16_t(ns - _text), _line, uint8_t(nl), 0};
      }
      cur = uint8_t(id);
      p = q;
      return true;
    }

    // p points at the first character of the key. Afterwards p points at the end of the line.
    bool parseEntry(const char *&p, const uint8_t cur){
      const char *ks = p;
      const char *q = p;
      while(!INI_IS_EOL(*q) && *q != '=') q++;
      if(*q != '=') return parseError(IniReaderStatus::INI_ERR_MISSING_EQUALS, p, "%s", "missing '=' (expected key = value)");

      const char *ke = q;
      while(ke > ks && INI_IS_BLANK(ke[-1])) ke--;
      const size_t kl = size_t(ke - ks);
      if(kl == 0) return parseError(IniReaderStatus::INI_ERR_EMPTY_KEY, ks, "%s", "empty key");
      if(kl > INI_MAX_KEY_LEN) return parseError(IniReaderStatus::INI_ERR_KEY_LEN_LIMIT, ks, "key too long (limit=%u)", unsigned(INI_MAX_KEY_LEN));
      if(!iniIsValidKey(ks, kl)){
        const char *bad = ks;
        while(bad < ke && *bad != '[' && *bad != ']' && *bad != ';' && *bad != '#' &&
              !(((unsigned char)*bad < 0x20 && *bad != '\t') || *bad == 0x7F)) bad++;
        return parseError(IniReaderStatus::INI_ERR_INVALID_KEY_NAME, bad, "invalid key '%.*s' (must not contain [ ] ; # or control characters)", int(kl), ks);
      }

      q++;
      while(INI_IS_BLANK(*q)) q++;
      const char *vs = q;
      const char *ve = q;       // behind the last character that belongs to the value
      bool elemStart = true;    // a quote only starts a quoted element at the start of the value / after ','

      for(;;){
        const char ch = *q;
        if(INI_IS_EOL(ch)) break;
        if(INI_IS_BLANK(ch)){ q++; continue; }

        if(elemStart && (ch == '"' || ch == '\'')){
          const char *open = q;
          q++;
          while(*q != ch){
            if(INI_IS_EOL(*q)) return parseError(IniReaderStatus::INI_ERR_MISSING_QUOTE, open, "missing closing quote %s", ch == '"' ? "'\"'" : "\"'\"");
            if(ch == '"' && *q == '\\'){
              q++;
              if(INI_IS_EOL(*q)) return parseError(IniReaderStatus::INI_ERR_MISSING_QUOTE, open, "missing closing quote %s", ch == '"' ? "'\"'" : "\"'\"");
            }
            q++;
          }
          q++;
          ve = q;
          while(INI_IS_BLANK(*q)) q++;
          if(*q == ','){
            q++;
            ve = q;
            continue;           // elemStart stays true
          }
          if(*q == ';' || *q == '#'){
            while(!INI_IS_EOL(*q)) q++;
            break;
          }
          if(INI_IS_EOL(*q)) break;
          { char cd[8]; return parseError(IniReaderStatus::INI_ERR_UNEXPECTED_CHAR, q, "unexpected character %s after closing quote", charDesc(*q, cd)); }
        }

        // inline comment: ';' or '#' at the start of the value or after a blank
        if((ch == ';' || ch == '#') && (q == vs || INI_IS_BLANK(q[-1]))){
          while(!INI_IS_EOL(*q)) q++;
          break;
        }

        elemStart = (ch == ',');
        q++;
        ve = q;
      }

      for(uint16_t i = 0; i < _entryCount; i++){
        const IniEntry &o = _entries[i];
        if(o.section == cur && iniNameEquals(_text + o.keyStart, o.keyLen, ks, kl, _cs)){
          return parseError(IniReaderStatus::INI_ERR_DUPLICATE_KEY, ks, "duplicate key '%.*s' (first defined on line %u)", int(kl), ks, unsigned(o.line));
        }
      }
      if(_entryCount >= _maxEntries) return parseError(IniReaderStatus::INI_ERR_ENTRY_LIMIT, ks, "maximum key count exceeded (limit=%u)", unsigned(_maxEntries));

      // Quote type of the whole value is determined once here: read() must not rescan the text,
      // a zero-copy read may have replaced the closing quote with '\0'.
      uint8_t vflags = 0;
      const size_t vn = size_t(ve - vs);
      if(vn >= 2 && (vs[0] == '"' || vs[0] == '\'') && findClosingQuote(vs, ve) == ve - 1){
        vflags = vs[0] == '"' ? FLAG_DQ : FLAG_SQ;
        if(vs[0] == '"' && memchr(vs + 1, '\\', vn - 2)) vflags |= FLAG_ESC;
      }
      _entries[_entryCount++] = IniEntry{uint16_t(ks - _text), uint16_t(vs - _text), uint16_t(vn), _line, uint8_t(kl), cur, vflags};
      p = q;
      return true;
    }
  ENDREGION()

  REGION(read)
    inline bool beginRead(const char *key){
      if(_hasParseError) return false;
      _hasReadError = false;
      _code = IniReaderStatus::INI_READER_OK;
      _message[0] = '\0';
      _errPath[0] = '\0';
      _errLine = 0;
      _errCol = 0;
      _secLen = 0;
      _sec[0] = '\0';

      if(!_parsed) return fail(IniReaderStatus::INI_ERR_INI_NOT_PARSED, 0, nullptr, 0, nullptr, 0, -1, "%s", "INI has not been parsed");
      _ringUsed = 0;
      if(_verifyBuffer && iniHash(_text, _len) != _checksum){
        return fail(IniReaderStatus::INI_ERR_INI_CHANGED, 0, nullptr, 0, nullptr, 0, -1, "%s", "INI buffer was modified after parsing");
      }
      if(!key) return fail(IniReaderStatus::INI_ERR_INVALID_KEY, 0, nullptr, 0, nullptr, 0, -1, "%s", "key must not be nullptr");

      for(uint16_t i = 0; i < _entryCount; i++) _entries[i].flags &= uint8_t(~FLAG_USED);
      for(uint8_t i = 0; i < _sectionCount; i++) _sections[i].flags &= uint8_t(~FLAG_USED);
      return true;
    }

    // Appends ".name" (or "name" at the top level) to the current section name
    inline bool pushName(const char *name, const size_t len, const char *key){
      const size_t need = (_secLen ? 1u : 0u) + len;
      if(_secLen + need > INI_MAX_SECTION_LEN){
        return fail(IniReaderStatus::INI_ERR_SECTION_LEN_LIMIT, 0, _sec, _secLen, key, key ? strlen(key) : 0, -1,
                    "section name too long (limit=%u)", unsigned(INI_MAX_SECTION_LEN));
      }
      if(_secLen) _sec[_secLen++] = '.';
      memcpy(_sec + _secLen, name, len);
      _secLen = uint8_t(_secLen + len);
      _sec[_secLen] = '\0';
      return true;
    }
    inline bool pushIndex(unsigned i, const char *key){
      char tmp[11];
      uint8_t k = 0;
      do{ tmp[k++] = char('0' + i % 10); i /= 10; }while(i);
      if(_secLen + 1u + k > INI_MAX_SECTION_LEN){
        return fail(IniReaderStatus::INI_ERR_SECTION_LEN_LIMIT, 0, _sec, _secLen, key, strlen(key), -1,
                    "section name too long (limit=%u)", unsigned(INI_MAX_SECTION_LEN));
      }
      _sec[_secLen++] = '.';
      while(k) _sec[_secLen++] = tmp[--k];
      _sec[_secLen] = '\0';
      return true;
    }
    inline void popName(const uint8_t oldLen){
      _secLen = oldLen;
      _sec[_secLen] = '\0';
    }

    // secId < 0: the section does not exist (members keep their defaults)
    template<typename T>
    bool readStruct(T &out, const int16_t secId){
      bool ok = true;
      out.reflect([&](auto&&... fields){
        ok = (... && readField(fields, secId));
      });
      if(!ok) return false;

      if(secId < 0) return true;
      _sections[secId].flags |= FLAG_USED;
      if(_config.ignoreUnknownKeys) return true;

      for(uint16_t i = 0; i < _entryCount; i++){
        const IniEntry &e = _entries[i];
        if(e.section != uint8_t(secId) || (e.flags & FLAG_USED)) continue;
        return fail(IniReaderStatus::INI_ERR_EXTRA_KEY, e.line, _sec, _secLen, _text + e.keyStart, e.keyLen, -1, "%s", "unknown key");
      }
      return true;
    }

    template<typename FR>
    bool readField(FR &f, const int16_t secId){
      if constexpr (is_reflect_comment_v<FR>){
        (void)f; (void)secId;
        return true;                    // COMMENT(...) is only written, never read
      }else{
        using M = remove_cvref_t<decltype(f.value)>;

        if constexpr (isIniSection<M>()){
          const uint8_t old = _secLen;
          if constexpr (std::is_array_v<M>){
            constexpr size_t N = std::extent_v<M>;
            for(size_t i = 0; i < N; i++){
              if(!pushName(f.key, f.keyLen, f.key) || !pushIndex(unsigned(i), f.key)) return false;
              const bool ok = readStruct(f.value[i], findSection(_sec, _secLen));
              popName(old);
              if(!ok) return false;
            }
            return true;
          }else{
            if(!pushName(f.key, f.keyLen, f.key)) return false;
            const bool ok = readStruct(f.value, findSection(_sec, _secLen));
            popName(old);
            return ok;
          }
        }else{
          const int16_t e = secId >= 0 ? findEntry(uint8_t(secId), f.key, f.keyLen) : -1;
          if(e < 0){
            // missing key: the member keeps its default
            if(!_config.requireAllKeys) return true;
            // requireAllKeys: a missing nullable member (std::optional, const char*, char*) means "null".
            // INIwriter omits exactly these values (empty optional / nullptr), so strict writer -> strict reader roundtrips.
            if constexpr (isIniNullable<M>()){
              iniSetNull(f.value);
              return true;
            }
            return fail(IniReaderStatus::INI_ERR_KEY_MISSING, secId >= 0 ? _sections[secId].line : 0, _sec, _secLen,
                        f.key, f.keyLen, -1, "missing key (expected %s)", iniTypeName<M>());
          }
          _entries[e].flags |= FLAG_USED;
          return readValue(f.value, e, f.key);
        }
      }
    }

    // Reads the value of entry e into out (scalar, array of scalars or optional)
    template<typename M>
    bool readValue(M &out, const int16_t e, const char *key){
      const IniEntry &en = _entries[e];
      const char *v = _text + en.valStart;
      const size_t n = en.valLen;

      if constexpr (is_json_optional_v<M>){
        using V = typename json_optional_value<M>::type;
        if(n == 0){
          out.reset();
          return true;
        }
        V tmp = out.has_value() ? *out : V{};
        if(!readValue(tmp, e, key)) return false;
        out = tmp;
        return true;
      }else{
        constexpr IniTypeKind k = deduceIniType<M>();
        if constexpr (k == IniTypeKind::Array){
          using E = std::remove_extent_t<M>;
          constexpr size_t N = std::extent_v<M>;
          static_assert(!std::is_array_v<E> || is_ini_char_array_v<E>, "nested arrays are not supported in INI");
          static_assert(!is_json_optional_v<E>, "arrays of std::optional are not supported in INI");

          ElementIter it{v, v + n};
          const char *es; size_t el; char eq;
          size_t count = 0;
          while(it.next(es, el, eq)){
            if(count < N && !convertScalar(out[count], es, el, eq, key, int(count), en.line)) return false;
            count++;
          }
          if(count < N){
            return fail(IniReaderStatus::INI_ERR_ARRAY_TOO_FEW_ELEMENTS, en.line, _sec, _secLen, key, strlen(key), -1,
                        "array has too few elements (got %u, expected %u)", unsigned(count), unsigned(N));
          }
          if(count > N){
            return fail(IniReaderStatus::INI_ERR_ARRAY_TOO_MANY_ELEMENTS, en.line, _sec, _secLen, key, strlen(key), -1,
                        "array has too many elements (got %u, expected %u)", unsigned(count), unsigned(N));
          }
          return true;
        }else{
          // scalar: the whole value; quotes are removed if they enclose the complete value (flags from parse())
          char quote = 0;
          const char *s = v;
          size_t sl = n;
          if(en.flags & (FLAG_DQ | FLAG_SQ)){
            quote = (en.flags & FLAG_DQ) ? '"' : '\'';
            s = v + 1;
            sl = n - 2;
          }
          return convertScalar(out, s, sl, quote, key, -1, en.line, _writable && !(en.flags & FLAG_ESC));
        }
      }
    }

    // Iterates over the comma separated elements of a value
    struct ElementIter{
      const char *p;
      const char *end;
      bool done = false;
      bool first = true;

      ElementIter(const char *b, const char *e) : p(b), end(e){}
      bool next(const char *&s, size_t &n, char &quote){
        if(done) return false;
        while(p < end && INI_IS_BLANK(*p)) p++;
        if(first && p >= end){ done = true; return false; }   // empty value: no elements
        first = false;

        quote = 0;
        if(p < end && (*p == '"' || *p == '\'')){
          quote = *p;
          const char *close = findClosingQuote(p, end);
          s = p + 1;
          n = size_t(close - s);
          p = close < end ? close + 1 : end;
          while(p < end && INI_IS_BLANK(*p)) p++;
        }else{
          s = p;
          while(p < end && *p != ',') p++;
          const char *e = p;
          while(e > s && INI_IS_BLANK(e[-1])) e--;
          n = size_t(e - s);
        }
        if(p < end && *p == ','){
          p++;
        }else{
          done = true;
        }
        return true;
      }
    };

    // open points at the opening quote. Returns the position of the closing quote (or end).
    static inline const char *findClosingQuote(const char *open, const char *end){
      const char q = *open;
      const char *p = open + 1;
      while(p < end && *p != q){
        if(q == '"' && *p == '\\' && p + 1 < end) p++;
        p++;
      }
      return p;
    }

    // Converts the text s (n bytes, quotes already removed) into out.
    // quote: 0 (unquoted), '"' (with escapes) or '\'' (raw). idx >= 0: array element.
    // zeroCopy: const char* / char* may point into the (writable) buffer: s[n] is set to '\0'.
    // Only for whole values without escapes (never for array elements: s[n] would be the ',' separator).
    template<typename T>
    bool convertScalar(T &out, const char *s, const size_t n, const char quote, const char *key, const int idx,
                       const uint16_t line, const bool zeroCopy = false){
      constexpr IniTypeKind k = deduceIniType<T>();
      const size_t keyLen = strlen(key);

      auto mismatch = [&]() -> bool {
        return fail(IniReaderStatus::INI_ERR_TYPE_MISMATCH, line, _sec, _secLen, key, keyLen, idx,
                    "cannot convert '%.*s' to %s", int(n > 24 ? 24 : n), s, iniTypeName<T>());
      };
      auto range = [&](const int8_t rc) -> bool {
        return fail(rc < 0 ? IniReaderStatus::INI_ERR_TYPE_UNDERFLOW : IniReaderStatus::INI_ERR_TYPE_OVERFLOW, line, _sec, _secLen, key, keyLen, idx,
                    rc < 0 ? "value '%.*s' is below range of %s" : "value '%.*s' exceeds range of %s", int(n > 24 ? 24 : n), s, iniTypeName<T>());
      };
      auto tooLong = [&](const size_t limit) -> bool {
        return fail(IniReaderStatus::INI_ERR_STRING_LEN_LIMIT, line, _sec, _secLen, key, keyLen, idx,
                    "string too long (limit=%u)", unsigned(limit));
      };

      if constexpr (k == IniTypeKind::String){
        if(n > INI_MAX_STRING_LEN) return tooLong(INI_MAX_STRING_LEN);
        if(zeroCopy){
          // FAST PATH: pointer directly into the INI buffer. s[n] is the character behind the value
          // (closing quote, blank, ';', '#', line break or the terminating '\0'), never part of a key or value.
          char *p = const_cast<char*>(s);
          if(p[n] != '\0'){
            p[n] = '\0';
            _zeroCopyDirty = true;      // hash is recomputed once at the end of read()
          }
          out = p;
        }else{
          // SLOW PATH: escapes / read-only buffer / array element -> copy into a rotating buffer.
          // The pointers of one read() must stay valid together: more strings than ring slots is an error
          // (instead of silently returning pointers to overwritten buffers).
          if(_ringUsed >= INI_ROTATING_STR_BUFFER_COUNT){
            return fail(IniReaderStatus::INI_ERR_STRING_BUFFER_LIMIT, line, _sec, _secLen, key, keyLen, idx,
                        "too many const char* strings in one read() (limit=%u, use char[N], read() in parts or parse(ini, true))",
                        unsigned(INI_ROTATING_STR_BUFFER_COUNT));
          }
          _ringUsed++;
          char *dest = nextStringBuffer();
          size_t outLen = 0;
          if(!decodeValue(s, n, quote, dest, INI_MAX_STRING_LEN + 1, outLen)) return tooLong(INI_MAX_STRING_LEN);
          out = dest;
        }
      }else if constexpr (k == IniTypeKind::CharArray){
        constexpr size_t N = std::extent_v<T>;
        static_assert(N >= 1, "char[0] not supported");
        size_t outLen = 0;
        if(!decodeValue(s, n, quote, out, N, outLen)) return tooLong(N - 1);
      #ifdef ARDUINO
      }else if constexpr (k == IniTypeKind::StringArduino){
        char *dest = scratchBuffer();     // copied into the String at once: must not use (overwrite) a ring slot
        size_t outLen = 0;
        if(!decodeValue(s, n, quote, dest, INI_MAX_STRING_LEN + 1, outLen)) return tooLong(INI_MAX_STRING_LEN);
        out = dest;
      #endif
      }else if constexpr (k == IniTypeKind::Char){
        char tmp[8];
        size_t outLen = 0;
        if(!decodeValue(s, n, quote, tmp, sizeof(tmp), outLen) || outLen > 1) return mismatch();
        out = outLen ? tmp[0] : '\0';
      }else{
        // numbers / bool: an empty value keeps the default (coerceInputValues)
        if(n == 0){
          if(_config.coerceInputValues) return true;
          return mismatch();
        }

        if constexpr (k == IniTypeKind::Bool){
          bool b;
          if(!parseBool(s, n, b)) return mismatch();
          out = b;
        }else if constexpr (k == IniTypeKind::Enum){
          using U = std::underlying_type_t<T>;
          U tmp = 0;
          const int8_t rc = parseInteger(s, n, tmp);
          if(rc == 2) return mismatch();
          if(rc != 0) return range(rc);
          out = static_cast<T>(tmp);
        }else if constexpr (k == IniTypeKind::Int){
          const int8_t rc = parseInteger(s, n, out);
          if(rc == 2) return mismatch();
          if(rc != 0) return range(rc);
        }else if constexpr (k == IniTypeKind::FloatFixedPoint){
          // fixed-point is stored as an integer (value * 10^precision), same as ReflectJSON
          int64_t raw = 0;
          const int8_t rc = parseInteger(s, n, raw);
          if(rc == 2) return mismatch();
          if(rc != 0) return range(rc);
          using F = remove_cvref_t<decltype(out.value)>;
          out.value = static_cast<F>((double)raw / JSON_POW10[jsonClampPrecision(out.precision)]);
        }else if constexpr (k == IniTypeKind::FloatDefault || k == IniTypeKind::Float){
          double v;
          if(!parseDouble(s, n, v)) return mismatch();
          using F = std::conditional_t<k == IniTypeKind::Float, remove_cvref_t<decltype(floatTarget(out))>, T>;
          if(v > (double)(std::numeric_limits<F>::max)()) return range(1);
          if(v < (double)std::numeric_limits<F>::lowest()) return range(-1);
          if constexpr (k == IniTypeKind::Float){
            // round to the requested number of decimal places
            const double scale = JSON_POW10[jsonClampPrecision(out.precision)];
            const double sc = v * scale;
            if(sc > -9.0e18 && sc < 9.0e18) v = (double)(int64_t)(sc + (sc >= 0 ? 0.5 : -0.5)) / scale;
            out.value = static_cast<F>(v);
          }else{
            out = static_cast<T>(v);
          }
        }else{
          static_assert(always_false<T>, "convertScalar: unsupported type");
        }
      }
      return true;
    }
    template<typename F>
    static F &floatTarget(JsonNumber<F> &n){ return n.value; }
    template<typename T>
    static T &floatTarget(T &v){ return v; }

    template<typename X>
    static inline void copyValue(X &dst, const X &src){ dst = src; }
    template<typename E, size_t N>
    static inline void copyValue(E (&dst)[N], const E (&src)[N]){
      if constexpr (std::is_trivially_copyable_v<E>){
        memcpy(dst, src, sizeof(E) * N);
      }else{
        for(size_t i = 0; i < N; ++i) copyValue(dst[i], src[i]);
      }
    }
  ENDREGION()

  REGION(convert)
    // 64-bit FNV-1a over the buffer (setBufferCheck). Non-linear and position-dependent: unlike a weighted sum,
    // changes at several positions do not cancel each other out ("a=15" -> "a=52").
    static inline uint64_t iniHash(const char *s, const size_t n){
      uint64_t h = 14695981039346656037ull;
      for(size_t i = 0; i < n; i++){
        h ^= (uint8_t)s[i];
        h *= 1099511628211ull;
      }
      return h;
    }
    // Temporary buffer for Arduino String targets (copied into the String immediately)
    static char *scratchBuffer(){
      static char buf[INI_MAX_STRING_LEN + 1];
      return buf;
    }

    // Shared static buffers for const char* / Arduino String targets (one set for all INIreader instances)
    static char *nextStringBuffer(){
      static char buffers[INI_ROTATING_STR_BUFFER_COUNT][INI_MAX_STRING_LEN + 1];
      static size_t idx = 0;
      char *b = buffers[idx];
      idx = (idx + 1 >= INI_ROTATING_STR_BUFFER_COUNT) ? 0 : idx + 1;
      return b;
    }

    static inline int8_t hexVal(const char c){
      if(c >= '0' && c <= '9') return int8_t(c - '0');
      const char l = iniLower(c);
      if(l >= 'a' && l <= 'f') return int8_t(l - 'a' + 10);
      return -1;
    }

    // Copies / decodes a value into dst (cap bytes incl. '\0'). Returns false if it does not fit.
    static bool decodeValue(const char *s, const size_t n, const char quote, char *dst, const size_t cap, size_t &outLen){
      size_t o = 0;
      if(quote != '"'){
        if(n + 1 > cap) return false;
        memcpy(dst, s, n);
        dst[n] = '\0';
        outLen = n;
        return true;
      }
      for(size_t i = 0; i < n; i++){
        char c = s[i];
        if(c == '\\' && i + 1 < n){
          const char e = s[++i];
          switch(e){
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case '0':  c = '\0'; break;
            case '\\': c = '\\'; break;
            case '"':  c = '"';  break;
            case '\'': c = '\''; break;
            case 'x': {
              const int8_t h = (i + 1 < n) ? hexVal(s[i + 1]) : int8_t(-1);
              const int8_t l = (i + 2 < n) ? hexVal(s[i + 2]) : int8_t(-1);
              if(h >= 0 && l >= 0){ c = char((h << 4) | l); i += 2; }
              else { if(o + 1 >= cap) return false; dst[o++] = '\\'; c = 'x'; }
              break;
            }
            default:   // unknown escape: keep it as it is
              if(o + 1 >= cap) return false;
              dst[o++] = '\\';
              c = e;
          }
        }
        if(o + 1 >= cap) return false;
        dst[o++] = c;
      }
      dst[o] = '\0';
      outLen = o;
      return true;
    }

    static bool parseBool(const char *s, const size_t n, bool &out){
      char l[6];
      if(n > 5) return false;
      for(size_t i = 0; i < n; i++) l[i] = iniLower(s[i]);
      l[n] = '\0';
      if(!strcmp(l, "true") || !strcmp(l, "yes") || !strcmp(l, "on") || !strcmp(l, "1")){ out = true; return true; }
      if(!strcmp(l, "false") || !strcmp(l, "no") || !strcmp(l, "off") || !strcmp(l, "0")){ out = false; return true; }
      return false;
    }

    // Returns: 0 = ok, -1 = underflow, 1 = overflow, 2 = not an integer. out stays unchanged on error.
    template<typename T>
    static int8_t parseInteger(const char *s, const size_t n, T &out){
      size_t i = 0;
      bool neg = false;
      if(s[0] == '+' || s[0] == '-'){ neg = (s[0] == '-'); i++; }
      if(i >= n) return 2;

      uint64_t mag = 0;
      bool over = false;
      if(n - i > 2 && s[i] == '0' && (s[i + 1] | 0x20) == 'x'){
        for(i += 2; i < n; i++){
          const int8_t d = hexVal(s[i]);
          if(d < 0) return 2;
          if(mag >> 60) over = true;
          mag = (mag << 4) | uint64_t(d);
        }
      }else{
        for(; i < n; i++){
          if(!JSON_IS_DIGIT(s[i])) return 2;
          const uint64_t d = uint64_t(s[i] - '0');
          if(mag > (UINT64_MAX - d) / 10u) over = true; else mag = mag * 10u + d;
        }
      }
      if(over) return neg ? -1 : 1;

      constexpr uint64_t maxv = (uint64_t)(std::numeric_limits<T>::max)();
      if constexpr (std::is_unsigned_v<T>){
        if(neg && mag != 0) return -1;      // "-0" is allowed
        if(mag > maxv) return 1;
        out = static_cast<T>(mag);
      }else{
        if(neg){
          if(mag > maxv + 1) return -1;
          out = (mag == maxv + 1) ? (std::numeric_limits<T>::min)() : static_cast<T>(-(int64_t)mag);
        }else{
          if(mag > maxv) return 1;
          out = static_cast<T>(mag);
        }
      }
      return 0;
    }

    // [+-]digits[.digits][e[+-]digits], also ".5" and "5." The whole text must be a number.
    static bool parseDouble(const char *s, const size_t n, double &out){
      const char *p = s;
      const char *const end = s + n;
      bool neg = false;
      if(p < end && (*p == '+' || *p == '-')){ neg = (*p == '-'); p++; }

      uint64_t mantissa = 0;
      int scale = 0;
      bool any = false;
      while(p < end && JSON_IS_DIGIT(*p)){
        any = true;
        if(mantissa < 1000000000000000000ull) mantissa = mantissa * 10u + uint64_t(*p - '0');
        else scale++;
        p++;
      }
      if(p < end && *p == '.'){
        p++;
        while(p < end && JSON_IS_DIGIT(*p)){
          any = true;
          if(mantissa < 1000000000000000000ull){ mantissa = mantissa * 10u + uint64_t(*p - '0'); scale--; }
          p++;
        }
      }
      if(!any) return false;
      if(p < end && (*p | 0x20) == 'e'){
        p++;
        int expSign = 1;
        if(p < end && (*p == '+' || *p == '-')){ if(*p == '-') expSign = -1; p++; }
        if(p >= end || !JSON_IS_DIGIT(*p)) return false;
        int ex = 0;
        while(p < end && JSON_IS_DIGIT(*p)){ if(ex < 100000) ex = ex * 10 + (*p - '0'); p++; }
        scale += expSign * ex;
      }
      if(p != end) return false;

      double v = (double)mantissa;
      if(mantissa != 0){
        if(scale > 0)      v *= (scale <= JSON_POW10_MAX_EXACT) ? JSON_POW10[scale] : pow(10.0, scale);
        else if(scale < 0) v /= (-scale <= JSON_POW10_MAX_EXACT) ? JSON_POW10[-scale] : pow(10.0, -scale);
      }
      out = neg ? -v : v;
      return true;
    }
  ENDREGION()

  REGION(helper)
    // Section id or -1. An empty name is the global section (0).
    inline int16_t findSection(const char *name, const size_t len) const{
      if(len == 0) return 0;
      for(uint8_t i = 1; i < _sectionCount; i++){
        const IniSection &s = _sections[i];
        if(iniNameEquals(_text + s.nameStart, s.nameLen, name, len, _cs)) return i;
      }
      return -1;
    }
    // Is there a section "name.xxx"? (nested sections or array elements without a header of their own)
    inline bool hasSectionPrefix(const char *name, const size_t len) const{
      for(uint8_t i = 1; i < _sectionCount; i++){
        const IniSection &s = _sections[i];
        if(s.nameLen > len && _text[s.nameStart + len] == '.' &&
           iniNameEquals(_text + s.nameStart, len, name, len, _cs)) return true;
      }
      return false;
    }
    inline int16_t findEntry(const uint8_t sec, const char *key, const size_t keyLen) const{
      for(uint16_t i = 0; i < _entryCount; i++){
        const IniEntry &e = _entries[i];
        if(e.section == sec && iniNameEquals(_text + e.keyStart, e.keyLen, key, keyLen, _cs)) return int16_t(i);
      }
      return -1;
    }
  ENDREGION()

  REGION(Error)
    // Character for error messages: 'x' if printable ASCII, otherwise 0x01 / 0xC3 (control characters and
    // single UTF-8 bytes would print garbage)
    static const char *charDesc(const char c, char (&buf)[8]){
      const unsigned char u = (unsigned char)c;
      if(u >= 0x20 && u < 0x7F) snprintf(buf, sizeof(buf), "'%c'", c);
      else snprintf(buf, sizeof(buf), "0x%02X", unsigned(u));
      return buf;
    }

    void resetAll(){
      _text = nullptr;
      _len = 0;
      _checksum = 0;
      _zeroCopyDirty = false;
      _ringUsed = 0;
      _writable = false;
      _entryCount = 0;
      _sectionCount = 0;
      _parsed = false;
      _hasParseError = false;
      _hasReadError = false;
      _code = IniReaderStatus::INI_READER_OK;
      _message[0] = '\0';
      _errPath[0] = '\0';
      _errLine = 0;
      _errCol = 0;
      _secLen = 0;
      _sec[0] = '\0';
    }

    // Parse error at position p. The location goes to getErrorLine() / getErrorColumn(), not into the text.
    template<typename... A>
    bool parseError(const IniReaderStatus code, const char *p, const char *fmt, A... args){
      _code = code;
      _hasParseError = true;
      _parsed = false;
      _errLine = p ? _line : 0;
      _errCol = p ? uint32_t(p - _lineStart + 1) : 0;
      if(_createErrorInfo) snprintf(_message, sizeof(_message), fmt, args...);
      return false;
    }

    // Read error. The path is stored for getErrorPath(), the line for getErrorLine(); neither is added to
    // the text. The first error wins.
    template<typename... A>
    bool fail(const IniReaderStatus code, const uint16_t line, const char *sec, const size_t secLen,
              const char *key, const size_t keyLen, const int idx, const char *fmt, A... args){
      if(_hasReadError) return false;
      _code = code;
      _hasReadError = true;
      _errLine = line;
      if(!_createErrorInfo) return false;

      IniPathOut path(_errPath, sizeof(_errPath));
      if(sec) path.put(sec, secLen);
      if(key && keyLen){
        if(sec && secLen) path.put('.');
        path.put(key, keyLen);
      }
      if(idx >= 0) path.putIndex(unsigned(idx));
      path.end();

      // the location is not part of the text: getErrorPath() / getErrorLine()
      snprintf(_message, sizeof(_message), fmt, args...);
      return false;
    }
  ENDREGION()
};
INI_MSG_TRUNCATION_END

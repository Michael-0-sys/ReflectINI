/*
 ReflectINI - INI writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once
#include "ReflectINI_tools.h"
#include <tuple>

// Error and status codes returned by INIwriter.
// INI_WRITER_OK (0) indicates successful writing with no errors.
enum class IniWriterStatus {
  INI_WRITER_OK = 0,                // No error

  // limits
  INI_ERR_NO_SPACE,                 // Output buffer has insufficient space
  INI_ERR_KEY_LEN_LIMIT,            // Key length exceeds INI_MAX_KEY_LEN
  INI_ERR_SECTION_LEN_LIMIT,        // Full section name exceeds INI_MAX_SECTION_LEN
  INI_ERR_STRING_LEN_LIMIT,         // String length exceeds INI_MAX_STRING_LEN
  INI_ERR_MAX_KEYS,                 // More keys than configured (constructor maxKeys)

  // usage errors
  INI_ERR_INVALID_KEY,              // nullptr / empty key or key with = [ ] ; # or line breaks
  INI_ERR_INVALID_SECTION,          // nullptr / empty section name or name with [ ] or line breaks
  INI_ERR_DUPLICATE_KEY,            // Same key twice in the same section
  INI_ERR_GLOBAL_AFTER_SECTION,     // A global key (no section) after a [section] was written
  INI_ERR_INVALID_NUMBER,           // NaN, Infinity or a value that does not fit the requested number format

  // memory
  INI_ERR_OUT_OF_MEMORY,            // The internal buffers could not be allocated (constructor)

  // --- added later (appended so existing numeric values stay unchanged) ---
  INI_ERR_STRING_NOT_TERMINATED     // char[N] without '\0' (N used characters): INIreader could not read it back
};


// INIwriter builds an INI document incrementally into an output buffer.
//
//   INIwriter ini(1024);
//   ini.write(config);                  // whole REFLECT struct: global keys, then [sections]
//   const char *text = ini.getINI();
//
// Mapping (same as INIreader):
//   scalar member              key = value               (in the current section)
//   array of scalars T[N]      key = 1, 2, 3
//   char[N] / const char*      key = text                (quoted with "..." only if necessary)
//                                                        char[N]: max. N-1 characters + '\0'
//   REFLECT struct member      [name]                    nested: [name.sub]
//   array of structs S[N]      [name.0], [name.1], ...
//   std::optional<T>           omitted if empty
//   const char* == nullptr     omitted
//
// Section names are built from the reflection keys. For section-producing members (structs, struct
// arrays) '.' is the path separator: do not use a MEMBER_AS() name that aliases another reflected
// section path (e.g. MEMBER(a) with a member b plus MEMBER_AS("a.b", other) both end up in [a.b];
// the same applies to the .0, .1, ... of struct arrays). The keys of both structs would mix in one section.
//
// The output buffer size is defined at construction. resetINI() reuses the instance.
INI_MSG_TRUNCATION_BEGIN   // error messages are cut off on purpose (see ReflectINI_tools.h)
class INIwriter {
public:
    // INIwriter(256, false) means "no error info" (default maxKeys); without these overloads the bool would
    // silently be taken as maxKeys.
    template<typename B, typename = std::enable_if_t<std::is_same_v<B, bool>>>
    explicit INIwriter(size_t bufferSize, B createErrorInfo)
      : INIwriter(bufferSize, uint16_t(64), createErrorInfo){}
    template<typename B, typename = std::enable_if_t<std::is_same_v<B, bool>>>
    explicit INIwriter(size_t bufferSize, B createErrorInfo, const IniConfig &config)
      : INIwriter(bufferSize, uint16_t(64), config, createErrorInfo){}
    explicit INIwriter(size_t bufferSize, uint16_t maxKeys, const IniConfig &config, bool createErrorInfo = true)
      : INIwriter(bufferSize, maxKeys, createErrorInfo){
      setConfig(config);
    }
    // Constructs an INIwriter.
    //
    // bufferSize defines the size of the internal output buffer in bytes (minimum 32).
    // maxKeys defines how many keys the whole document may contain (needed for the duplicate check,
    // 8 bytes RAM per key (64-bit hash), minimum 1).
    //
    // If the allocation fails the writer is put into the error state INI_ERR_OUT_OF_MEMORY
    // and every further operation is ignored.
    explicit INIwriter(size_t bufferSize, uint16_t maxKeys = 64, bool createErrorInfo = true){
      _bufferMax = bufferSize < 32 ? 32 : bufferSize;
      _maxKeys = maxKeys == 0 ? 1 : maxKeys;
      _createErrorInfo = createErrorInfo;
      _limit = _bufferMax - 1;    // one position for '\0' always stays free

      _out = new (std::nothrow) char[_bufferMax];
      _hashes = new (std::nothrow) uint64_t[_maxKeys];

      if(!_out || !_hashes){
        makeError(IniWriterStatus::INI_ERR_OUT_OF_MEMORY);
        return;
      }
      _ready = true;
      resetINI();
    }
    ~INIwriter(){
      delete[] _out;
      delete[] _hashes;
    }
    INIwriter(const INIwriter&) = delete;
    INIwriter &operator=(const INIwriter&) = delete;

    // Enables or disables creation of extended error information (message, path).
    void setCreateErrorInfo(bool on){ _createErrorInfo = on; }
    bool isCreateErrorInfoEnabled() const{ return _createErrorInfo; }

    // Settings shared with INIreader (see IniConfig). Applies to all following write() calls.
    // Exception: caseSensitive (duplicate key check) is fixed per document: a change only takes effect
    // while no key has been written yet, otherwise with the next resetINI(). Already stored key hashes were
    // computed with the old mode and would no longer match.
    INIwriter &setConfig(const IniConfig &config){
      _config = config;
      if(_keyCount == 0) _hashCaseSensitive = config.caseSensitive;
      return *this;
    }
    const IniConfig &getConfig() const{ return _config; }

    // Returns true if the writer is in an error state.
    bool hasError() const{ return _code != IniWriterStatus::INI_WRITER_OK; }
    // Returns the error code of the last error, or INI_WRITER_OK (0).
    IniWriterStatus getErrorCode() const{ return _code; }
    // Returns the error message, or nullptr if there is no error or createErrorInfo is disabled.
    const char *getErrorMsg() const{
      if(!_createErrorInfo || !hasError()) return nullptr;
      return _message;
    }
    // Returns the location of the error as "section.key" (global keys: "key"), for array elements
    // with the index "section.key[2]" (same format as INIreader::getErrorPath()),
    // or nullptr if there is no error, no location or createErrorInfo is disabled.
    // A name rejected by startSection() has no location (nullptr): it is not a valid path.
    const char *getErrorPath() const{
      if(!_createErrorInfo || !hasError() || !_errPath[0]) return nullptr;
      return _errPath;
    }

    // Returns the configured maximum number of keys.
    uint16_t getMaxKeys() const{ return _maxKeys; }
    // Returns the number of keys written so far.
    uint16_t getKeyCount() const{ return _keyCount; }
    // Returns the size of the internal output buffer in bytes.
    size_t getBufferSize() const{ return _bufferMax; }

    // Returns the INI text (always zero-terminated), or nullptr if an error occurred.
    // INI needs no finish(): the text is complete after every write().
    const char *getINI() const{
      if(!_ready || hasError()) return nullptr;
      return _out;
    }
    // Returns the length of the INI text in bytes (without '\0'), or 0 on error.
    size_t getINISize() const{
      if(!_ready || hasError()) return 0;
      return _pos;
    }

    // Resets the writer to an empty document (global section, no keys, no error).
    // Has no effect if the allocation in the constructor failed.
    INIwriter &resetINI(){
      if(!_ready) return *this;
      _pos = 0;
      _out[0] = '\0';
      _keyCount = 0;
      _hashCaseSensitive = _config.caseSensitive;
      _secLen = 0;
      _sec[0] = '\0';
      _emittedLen = 0;
      _headerEmitted = false;
      _commentBeforeHeader = false;
      _pendingKey = nullptr;
      _pendingIdx = -1;
      _code = IniWriterStatus::INI_WRITER_OK;
      _message[0] = '\0';
      _errPath[0] = '\0';
      return *this;
    }

  #ifdef ARDUINO
    inline INIwriter &startSection(const String &name){ return startSection(name.c_str()); }
    inline INIwriter &comment(const String &text){ return comment(text.c_str()); }
    template<typename T>
    inline INIwriter &write(const T &value, const String &key){ return write(value, key.c_str()); }
  #endif

    // Starts a section: all following keys are written below [name].
    // Nested names are allowed ("network.wifi"). The header is written immediately.
    // There is no way back to the global section (INI has no end marker for a section).
    INIwriter &startSection(const char *name){
      if(!checkReady()) return *this;

      const size_t len = name ? iniBoundedLen(name, INI_MAX_SECTION_LEN) : 0;
      if(!name || !iniIsValidSectionName(name, len)){
        // no error path: _sec still holds the PREVIOUS section, and the rejected name is not a valid path
        makeError(len > INI_MAX_SECTION_LEN ? IniWriterStatus::INI_ERR_SECTION_LEN_LIMIT : IniWriterStatus::INI_ERR_INVALID_SECTION,
                  nullptr, false);
        return *this;
      }
      memcpy(_sec, name, len);
      _secLen = uint8_t(len);
      _sec[_secLen] = '\0';
      ensureHeader();
      return *this;
    }

    // Writes a comment line "; text". Line breaks in text start a new comment line.
    INIwriter &comment(const char *text){
      if(!checkReady()) return *this;
      appendLit("; ");
      if(text){
        for(const char *p = text; *p; p++){
          if(*p == '\r') continue;
          if(*p == '\n') appendLit("\n; ");
          else appendChar(*p);
        }
      }
      appendChar('\n');
      return *this;
    }

    // Writes an empty line.
    INIwriter &blankLine(){
      if(!checkReady()) return *this;
      appendChar('\n');
      return *this;
    }

    // Writes a value.
    //
    // With key:    scalar / array    -> "key = value" in the current section
    //              REFLECT struct    -> section [current.key] (or [key] in the global section)
    //              array of structs  -> sections [key.0], [key.1], ...
    // Without key: REFLECT struct    -> the members are written into the current section
    //              (the root struct of a document: global keys, then its sections)
    //
    // Fails on an invalid key, duplicate key, full buffer, NaN/Infinity, string too long,
    // or a global key after a section was written.
    template<typename T>
    INIwriter &write(const T &value, const char *key = nullptr){
      if(!checkReady()) return *this;
      setData(value, key);
      return *this;
    }

private:
  REGION(deklaration)
    char *_out = nullptr;
    size_t _bufferMax = 0;
    size_t _limit = 0;              // bufferMax - 1: last position for content
    size_t _pos = 0;

    uint64_t *_hashes = nullptr;    // 64-bit FNV-1a of (section, key) for the duplicate check
    uint16_t _maxKeys = 0;
    uint16_t _keyCount = 0;
    bool _hashCaseSensitive = false;   // caseSensitive of the current document (fixed at the first key)

    char _sec[INI_MAX_SECTION_LEN + 1] = {0};      // logical current section ("" = global)
    uint8_t _secLen = 0;
    char _emitted[INI_MAX_SECTION_LEN + 1] = {0};  // last [header] written to the text
    uint8_t _emittedLen = 0;
    bool _headerEmitted = false;
    bool _commentBeforeHeader = false;   // a section COMMENT was written: the next [header] follows directly

    IniConfig _config;
    // Key of the value currently being written (only valid during the current write() call).
    // Used for the error path of errors that happen after beginKey() (e.g. INI_ERR_NO_SPACE).
    const char *_pendingKey = nullptr;
    // Index of the array element currently being written (-1: no array). Error path: "key[i]", same as INIreader.
    int _pendingIdx = -1;
    IniWriterStatus _code = IniWriterStatus::INI_WRITER_OK;
    char _message[INI_ERR_MAX_MSG] = {0};
    char _errPath[INI_ERR_MAX_PATH_LEN] = {0};
    bool _createErrorInfo = true;
    bool _ready = false;
  ENDREGION()

  REGION(write)
    inline bool checkReady(){
      if(hasError()) return false;
      if(!_ready) return makeError(IniWriterStatus::INI_ERR_OUT_OF_MEMORY);
      return true;
    }

    // Appends ".key" (or "key" in the global section) to the logical section name
    inline bool pushName(const char *key, const size_t keyLen){
      if(!iniIsValidKey(key, keyLen)) return makeError(IniWriterStatus::INI_ERR_INVALID_KEY, key);
      const size_t need = (_secLen ? 1u : 0u) + keyLen;
      if(_secLen + need > INI_MAX_SECTION_LEN) return makeError(IniWriterStatus::INI_ERR_SECTION_LEN_LIMIT, key);
      if(_secLen) _sec[_secLen++] = '.';
      memcpy(_sec + _secLen, key, keyLen);
      _secLen = uint8_t(_secLen + keyLen);
      _sec[_secLen] = '\0';
      return true;
    }
    inline bool pushIndex(unsigned i){
      char tmp[11];
      uint8_t k = 0;
      do{ tmp[k++] = char('0' + i % 10); i /= 10; }while(i);
      if(_secLen + 1u + k > INI_MAX_SECTION_LEN) return makeError(IniWriterStatus::INI_ERR_SECTION_LEN_LIMIT);
      _sec[_secLen++] = '.';
      while(k) _sec[_secLen++] = tmp[--k];
      _sec[_secLen] = '\0';
      return true;
    }
    inline void popName(const uint8_t oldLen){
      _secLen = oldLen;
      _sec[_secLen] = '\0';
    }

    // Writes the [header] of the current section if the text is not already in it
    inline bool ensureHeader(const char *key = nullptr){
      if(_secLen == 0){
        if(_headerEmitted) return makeError(IniWriterStatus::INI_ERR_GLOBAL_AFTER_SECTION, key);
        return true;
      }
      if(_headerEmitted && _emittedLen == _secLen && memcmp(_emitted, _sec, _secLen) == 0) return true;

      if(_pos > 0 && _config.blankLineBeforeSection && !_commentBeforeHeader) appendChar('\n');
      _commentBeforeHeader = false;
      appendChar('[');
      appendRaw(_sec, _secLen);
      appendLit("]\n");
      if(hasError()) return false;

      memcpy(_emitted, _sec, size_t(_secLen) + 1);
      _emittedLen = _secLen;
      _headerEmitted = true;
      return true;
    }

    // Checks the key, writes the header if necessary and "key = "
    inline bool beginKey(const char *key){
      const size_t keyLen = iniBoundedLen(key, INI_MAX_KEY_LEN);
      if(keyLen > INI_MAX_KEY_LEN) return makeError(IniWriterStatus::INI_ERR_KEY_LEN_LIMIT, key);
      if(!iniIsValidKey(key, keyLen)) return makeError(IniWriterStatus::INI_ERR_INVALID_KEY, key);

      // FNV-1a over section + separator + key (lower case unless caseSensitive)
      // 64 bit: an accidental collision (= false INI_ERR_DUPLICATE_KEY) is practically impossible
      constexpr uint64_t FNV_PRIME = 1099511628211ull;
      uint64_t h = 14695981039346656037ull;
      const bool cs = _hashCaseSensitive;
      for(uint8_t i = 0; i < _secLen; i++){ h ^= (uint8_t)(cs ? _sec[i] : iniLower(_sec[i])); h *= FNV_PRIME; }
      h ^= 0x1Fu; h *= FNV_PRIME;     // separator: section "a" + key "bc" != section "ab" + key "c"
      for(size_t i = 0; i < keyLen; i++){ h ^= (uint8_t)(cs ? key[i] : iniLower(key[i])); h *= FNV_PRIME; }

      for(uint16_t i = 0; i < _keyCount; i++){
        if(_hashes[i] == h) return makeError(IniWriterStatus::INI_ERR_DUPLICATE_KEY, key);
      }
      if(_keyCount >= _maxKeys) return makeError(IniWriterStatus::INI_ERR_MAX_KEYS, key);

      if(!ensureHeader(key)) return false;

      _hashes[_keyCount++] = h;
      _pendingKey = key;
      appendRaw(key, keyLen);
      if(_config.spaceAroundEquals) appendLit(" = "); else appendChar('=');
      return !hasError();
    }

    template<typename T>
    bool setData(const T &value, const char *key){
      if(hasError()) return false;

      if constexpr (is_json_optional_v<T>){
        if(!value.has_value()) return true;     // empty optional: omitted
        return setData(*value, key);
      }else if constexpr (isIniSection<T>()){
        if constexpr (std::is_array_v<T>){
          constexpr size_t N = std::extent_v<T>;
          if(!key) return makeError(IniWriterStatus::INI_ERR_INVALID_KEY);
          const uint8_t old = _secLen;
          for(size_t i = 0; i < N; i++){
            if(!pushName(key, strlen(key)) || !pushIndex(unsigned(i))) return false;
            if(_commentBeforeHeader && !ensureHeader()) return false;   // COMMENT belongs to [key.0]
            const bool ok = writeStruct(value[i]);
            popName(old);
            if(!ok) return false;
          }
          return true;
        }else{
          if(!key) return writeStruct(value);   // flatten into the current section
          const uint8_t old = _secLen;
          if(!pushName(key, iniBoundedLen(key, INI_MAX_SECTION_LEN))) return false;
          if(_commentBeforeHeader && !ensureHeader()) return false;     // COMMENT belongs to [key]
          const bool ok = writeStruct(value);
          popName(old);
          return ok;
        }
      }else{
        constexpr IniTypeKind k = deduceIniType<T>();
        if(!key) return makeError(IniWriterStatus::INI_ERR_INVALID_KEY);

        if constexpr (k == IniTypeKind::String){
          if(value == nullptr) return true;     // nullptr: omitted
        }

        if(!beginKey(key)) return false;

        if constexpr (k == IniTypeKind::Array){
          using E = std::remove_extent_t<T>;
          constexpr size_t N = std::extent_v<T>;
          static_assert(!std::is_array_v<E> || is_ini_char_array_v<E>, "nested arrays are not supported in INI");
          static_assert(!is_json_optional_v<E>, "arrays of std::optional are not supported in INI");
          for(size_t i = 0; i < N; i++){
            _pendingIdx = int(i);       // error path: key[i]
            if(i) appendLit(", ");
            if(!writeScalar(value[i], key, true)) return false;
          }
          _pendingIdx = -1;
        }else{
          if(!writeScalar(value, key, false)) return false;
        }
        appendChar('\n');
        _pendingKey = nullptr;
        return !hasError();
      }
    }

    // All scalar members first (they belong to the current section), then the sub-sections.
    // reflect() is called only ONCE: the member expressions of MEMBER(x, expr) / MEMBER_AS(key, expr)
    // are evaluated once per write, the pack is just walked twice.
    // COMMENT(...) belongs to the next member: it is written in the same pass as that member (pass 1: keys,
    // pass 2: sections) and only if that member is written (not for an empty std::optional / nullptr).
    // A COMMENT without a following member belongs to pass 1 and is always written.
    template<typename T>
    bool writeStruct(const T &value){
      bool ok = true;
      value.reflect([&](auto&&... fields){
        auto tup = std::forward_as_tuple(fields...);
        constexpr auto seq = std::make_index_sequence<sizeof...(fields)>{};
        ok = writePass<false>(tup, seq);
        if(ok) ok = writePass<true>(tup, seq);
      });
      return ok && !hasError();
    }
    template<bool Sections, typename Tup, size_t... I>
    FORCE_INLINE bool writePass(const Tup &tup, std::index_sequence<I...>){
      (void)tup;
      return (true && ... && writeEntry<Sections, I>(tup));
    }
    // Index of the first member (not COMMENT) at or after I, or the pack size
    template<size_t I, typename Tup>
    static constexpr size_t nextMemberIndex(){
      if constexpr (I >= std::tuple_size_v<Tup>) return I;
      else if constexpr (is_reflect_comment_v<std::tuple_element_t<I, Tup>>) return nextMemberIndex<I + 1, Tup>();
      else return I;
    }
    // false if setData() would omit the value (empty std::optional, nullptr string)
    template<typename V>
    static bool iniWillWrite(const V &v){
      if constexpr (is_json_optional_v<V>){
        return v.has_value() && iniWillWrite(*v);
      }else if constexpr (!isIniSection<V>() && deduceIniType<V>() == IniTypeKind::String){
        return v != nullptr;
      }else{
        (void)v;
        return true;
      }
    }
    template<bool Sections, size_t I, typename Tup>
    FORCE_INLINE bool writeEntry(const Tup &tup){
      using F = remove_cvref_t<std::tuple_element_t<I, Tup>>;
      const F &f = std::get<I>(tup);
      if constexpr (is_reflect_comment_v<F>){
        constexpr size_t J = nextMemberIndex<I + 1, Tup>();
        if constexpr (J == std::tuple_size_v<Tup>){
          // trailing COMMENT: after the last key
          if constexpr (Sections) return true;
          else return writeReflectComment(f.text, false);
        }else{
          using G = remove_cvref_t<std::tuple_element_t<J, Tup>>;
          if constexpr (isIniSection<remove_cvref_t<decltype(G::value)>>() != Sections){
            return true;
          }else{
            if(!iniWillWrite(std::get<J>(tup).value)) return true;   // member omitted: comment omitted too
            return writeReflectComment(f.text, Sections);
          }
        }
      }else{
        using M = remove_cvref_t<decltype(f.value)>;
        if constexpr (isIniSection<M>() == Sections){
          return setData(f.value, f.key);
        }else{
          return true;
        }
      }
    }
    // Key comment: below the [header] of the current section, in front of the key.
    // Section comment: blank line (blankLineBeforeSection), comment, then directly the [header] of the next member.
    bool writeReflectComment(const char *text, const bool beforeSection){
      if(beforeSection){
        if(!_commentBeforeHeader){
          if(_pos > 0 && _config.blankLineBeforeSection) appendChar('\n');
          _commentBeforeHeader = true;
        }
      }else{
        if(_secLen && !ensureHeader()) return false;
      }
      comment(text);
      return !hasError();
    }

    template<typename T>
    bool writeScalar(const T &value, const char *key, const bool inArray){
      constexpr IniTypeKind k = deduceIniType<T>();
      if constexpr (k == IniTypeKind::Bool){
        if(static_cast<bool>(value)) appendLit("true"); else appendLit("false");
      }else if constexpr (k == IniTypeKind::Char){
        const char c = value;
        appendString(&c, c == '\0' ? 0 : 1, inArray, key);
      }else if constexpr (k == IniTypeKind::String){
        if(value == nullptr){
          appendLit("\"\"");    // nullptr inside an array: empty string
        }else{
          const size_t len = iniBoundedLen(value, INI_MAX_STRING_LEN);
          if(len > INI_MAX_STRING_LEN) return makeError(IniWriterStatus::INI_ERR_STRING_LEN_LIMIT, key);
          appendString(value, len, inArray, key);
        }
      }else if constexpr (k == IniTypeKind::CharArray){
        constexpr size_t N = std::extent_v<T>;
        size_t len = 0;
        while(len < N && value[len]) len++;
        // char[N] holds at most N-1 characters + '\0' (that is what INIreader requires to read it back)
        if(len == N) return makeError(IniWriterStatus::INI_ERR_STRING_NOT_TERMINATED, key);
        if(len > INI_MAX_STRING_LEN) return makeError(IniWriterStatus::INI_ERR_STRING_LEN_LIMIT, key);
        appendString(value, len, inArray, key);
      #ifdef ARDUINO
      }else if constexpr (k == IniTypeKind::StringArduino){
        if(value.length() > INI_MAX_STRING_LEN) return makeError(IniWriterStatus::INI_ERR_STRING_LEN_LIMIT, key);
        appendString(value.c_str(), value.length(), inArray, key);
      #endif
      }else if constexpr (k == IniTypeKind::Enum){
        appendInt(static_cast<std::underlying_type_t<T>>(value));
      }else if constexpr (k == IniTypeKind::Int){
        appendInt(value);
      }else if constexpr (k == IniTypeKind::FloatFixedPoint){
        // JsonFixed<T> -> integer (value * 10^precision), same as ReflectJSON
        const double scaled = (double)value.value * JSON_POW10[jsonClampPrecision(value.precision)];
        if(!(scaled > -9.0e18 && scaled < 9.0e18)) return makeError(IniWriterStatus::INI_ERR_INVALID_NUMBER, key);
        appendInt((int64_t)(scaled + (scaled >= 0 ? 0.5 : -0.5)));
      }else if constexpr (k == IniTypeKind::FloatDefault){
        if(!std::isfinite(value)) return makeError(IniWriterStatus::INI_ERR_INVALID_NUMBER, key);
        appendReal(value, defaultPrecision<T>());
      }else if constexpr (k == IniTypeKind::Float){
        if(!std::isfinite(value.value)) return makeError(IniWriterStatus::INI_ERR_INVALID_NUMBER, key);
        appendReal(value.value, jsonClampPrecision(value.precision));
      }else{
        static_assert(always_false<T>, "writeScalar: unsupported type");
      }
      return !hasError();
    }
  ENDREGION()

  REGION(append)
    FORCE_INLINE void appendChar(const char c){
      if(_pos >= _limit){
        makeError(IniWriterStatus::INI_ERR_NO_SPACE);
      }else{
        _out[_pos++] = c;
        _out[_pos] = '\0';
      }
    }
    inline void appendRaw(const char *s, const size_t n){
      if(hasError()) return;
      if(n > _limit - _pos){
        makeError(IniWriterStatus::INI_ERR_NO_SPACE);
        return;
      }
      memcpy(_out + _pos, s, n);
      _pos += n;
      _out[_pos] = '\0';
    }
    template<size_t N>
    FORCE_INLINE void appendLit(const char (&s)[N]){
      appendRaw(s, N - 1);
    }

    // Quotes are needed if the reader would otherwise see something different.
    // Must follow the same rules as INIreader::parseEntry(): a quote at the start of the value
    // or after a ',' (blanks in between) starts a quoted element - also in plain string values.
    static inline bool needsQuotes(const char *s, const size_t len, const bool inArray){
      if(len == 0) return true;                                        // "" (empty string)
      if(INI_IS_BLANK(s[0]) || INI_IS_BLANK(s[len - 1])) return true;  // blanks would be trimmed
      bool elemStart = true;
      for(size_t i = 0; i < len; i++){
        const unsigned char c = (unsigned char)s[i];
        if(c < 0x20 || c == 0x7F) return true;                         // line breaks, tabs, control characters
        if(c == ';' || c == '#') return true;                          // could start a comment
        if(c == ',' && inArray) return true;                           // element separator
        if(elemStart && (c == '"' || c == '\'')) return true;          // would start a quoted element
        if(c == ',') elemStart = true;
        else if(!INI_IS_BLANK(c)) elemStart = false;
      }
      return false;
    }
    inline void appendString(const char *s, const size_t len, const bool inArray, const char *key){
      if(!needsQuotes(s, len, inArray)){
        appendRaw(s, len);
        return;
      }
      static constexpr char hex[] = "0123456789abcdef";
      appendChar('"');
      for(size_t i = 0; i < len && !hasError(); i++){
        const unsigned char c = (unsigned char)s[i];
        switch(c){
          case '"':  appendLit("\\\""); break;
          case '\\': appendLit("\\\\"); break;
          case '\n': appendLit("\\n");  break;
          case '\r': appendLit("\\r");  break;
          case '\t': appendLit("\\t");  break;
          default:
            if(c < 0x20 || c == 0x7F){
              const char esc[4] = {'\\', 'x', hex[c >> 4], hex[c & 0x0F]};
              appendRaw(esc, 4);
            }else{
              appendChar((char)c);
            }
        }
      }
      appendChar('"');
      (void)key;
    }

    // Numbers: shared formatting from ReflectCore.h (identical output in ReflectJSON).
    // Written directly into the output buffer if there is enough room, otherwise through a small
    // temporary buffer (appendRaw() then reports INI_ERR_NO_SPACE).
    FORCE_INLINE char *numberTarget(){
      return (!hasError() && _limit - _pos >= REFLECT_NUM_BUF_SIZE) ? _out + _pos : nullptr;
    }
    template<typename I>
    inline void appendInt(const I v){
      static_assert(std::is_integral_v<I>, "appendInt: integral type expected");
      if(char *const p = numberTarget()){
        _pos += reflectFormatInt(v, p);
        _out[_pos] = '\0';
      }else{
        char tmp[REFLECT_NUM_BUF_SIZE];
        appendRaw(tmp, reflectFormatInt(v, tmp));
      }
    }
    // Writes a number with a fixed number of decimal places (value must be finite)
    template<typename F>
    inline void appendReal(const F value, const uint8_t decimals){
      char tmp[REFLECT_NUM_BUF_SIZE];
      char *const direct = numberTarget();
      const size_t n = reflectFormatReal(value, decimals, direct ? direct : tmp);
      if(n == 0){
        makeError(IniWriterStatus::INI_ERR_INVALID_NUMBER);
      }else if(direct){
        _pos += n;
        _out[_pos] = '\0';
      }else{
        appendRaw(tmp, n);
      }
    }
  ENDREGION()

  REGION(status)
    // withPath = false: no error path (getErrorPath() returns nullptr), for errors whose location is not
    // the current section, e.g. a rejected startSection() name.
    bool makeError(IniWriterStatus code, const char *key = nullptr, const bool withPath = true){
      if(hasError()) return false;
      _code = code;
      if(!_createErrorInfo) return false;

      // errors raised while a value is being appended know the key from beginKey()
      if(!key && (code == IniWriterStatus::INI_ERR_NO_SPACE || code == IniWriterStatus::INI_ERR_INVALID_NUMBER)) key = _pendingKey;

      // path: section.key
      IniPathOut path(_errPath, sizeof(_errPath));
      if(withPath && code != IniWriterStatus::INI_ERR_OUT_OF_MEMORY){
        path.put(_sec, _secLen);
        if(key){
          if(_secLen) path.put('.');
          size_t n = 0;
          while(n < INI_MAX_KEY_LEN && key[n]) path.put(key[n++]);
          if(_pendingIdx >= 0) path.putIndex(unsigned(_pendingIdx));
        }
      }
      path.end();

      const char *k = key ? key : "";
      using S = IniWriterStatus;
      switch(code){
      case S::INI_ERR_NO_SPACE:
        snprintf(_message, sizeof(_message), "output buffer full (capacity=%u)", unsigned(_bufferMax));
        break;
      case S::INI_ERR_KEY_LEN_LIMIT:
        snprintf(_message, sizeof(_message), "key too long (limit=%u)", unsigned(INI_MAX_KEY_LEN));
        break;
      case S::INI_ERR_SECTION_LEN_LIMIT:
        snprintf(_message, sizeof(_message), "section name too long (limit=%u)", unsigned(INI_MAX_SECTION_LEN));
        break;
      case S::INI_ERR_STRING_LEN_LIMIT:
        snprintf(_message, sizeof(_message), "string too long (limit=%u)", unsigned(INI_MAX_STRING_LEN));
        break;
      case S::INI_ERR_MAX_KEYS:
        snprintf(_message, sizeof(_message), "maximum key count exceeded (limit=%u)", unsigned(_maxKeys));
        break;
      case S::INI_ERR_INVALID_KEY:
        if(key) snprintf(_message, sizeof(_message), "invalid key '%s'", k);
        else    snprintf(_message, sizeof(_message), "a key is required for this value");
        break;
      case S::INI_ERR_INVALID_SECTION:
        snprintf(_message, sizeof(_message), "invalid section name");
        break;
      case S::INI_ERR_DUPLICATE_KEY:
        snprintf(_message, sizeof(_message), "duplicate key '%s'", k);
        break;
      case S::INI_ERR_GLOBAL_AFTER_SECTION:
        snprintf(_message, sizeof(_message), "global key '%s' cannot be written after a section", k);
        break;
      case S::INI_ERR_INVALID_NUMBER:
        snprintf(_message, sizeof(_message), "invalid number for key '%s'", k);
        break;
      case S::INI_ERR_OUT_OF_MEMORY:
        snprintf(_message, sizeof(_message), "out of memory");
        break;
      case S::INI_ERR_STRING_NOT_TERMINATED:
        snprintf(_message, sizeof(_message), "char[] for key '%s' is not null-terminated", k);
        break;
      default:
        snprintf(_message, sizeof(_message), "undefined error code: %d", (int)code);
        break;
      }
      return false;
    }
  ENDREGION()
};
INI_MSG_TRUNCATION_END

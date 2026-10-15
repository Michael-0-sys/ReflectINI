/*
 ReflectINI - INI writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/

#pragma once

// Shared reflection core (REFLECT, MEMBER, MEMBER_AS, fn(), fx(), std::optional detection, type names).
// The same file ships with ReflectJSON: a struct with REFLECT(...) works with JSONreader/JSONwriter AND
// INIreader/INIwriter. ReflectINI itself does not need ReflectJSON.
#include "ReflectCore.h"
#if !defined(REFLECTCORE_VERSION) || REFLECTCORE_VERSION != 5
  #error "ReflectCore.h version mismatch: ReflectJSON and ReflectINI must ship the same ReflectCore.h"
#endif

// Maximum length of the human-readable error message (incl. '\0')
constexpr size_t INI_ERR_MAX_MSG = 128;
// Maximum key length. MEMBER() checks the key length at compile time against REFLECT_MAX_KEY_LEN (ReflectCore.h).
constexpr size_t INI_MAX_KEY_LEN = REFLECT_MAX_KEY_LEN;
// Maximum length of a full section name, nested sections included ("network.wifi.ap")
constexpr size_t INI_MAX_SECTION_LEN = 64;
// Size of the getErrorPath() buffer (incl. '\0'): always large enough for the longest possible path
// "section.key[65535]" (full section name + '.' + key + array index), so the location is never cut off.
constexpr size_t INI_ERR_MAX_PATH_LEN = INI_MAX_SECTION_LEN + 1 + INI_MAX_KEY_LEN + 7 + 1;
// Maximum length of a string value (decoded, without '\0')
constexpr size_t INI_MAX_STRING_LEN = 254;
// Maximum length of an INI document in bytes (offsets are stored as uint16_t)
constexpr size_t INI_MAX_LEN = 65535;
// Number of shared static buffers for strings read into const char* / Arduino String
constexpr size_t INI_ROTATING_STR_BUFFER_COUNT = 8;

static_assert(INI_MAX_KEY_LEN < 255, "INI_MAX_KEY_LEN must be below 255 (8-bit key length)");
static_assert(INI_MAX_SECTION_LEN < 255, "INI_MAX_SECTION_LEN must be below 255 (8-bit name length)");
static_assert(INI_MAX_STRING_LEN < 60000, "INI_MAX_STRING_LEN too large");
static_assert(INI_ROTATING_STR_BUFFER_COUNT >= 1, "INI_ROTATING_STR_BUFFER_COUNT must be >= 1");

/**
 * Settings for INIreader AND INIwriter.
 *
 *   IniConfig cfg;                       // tolerant default (good for config files edited by hand)
 *   INIreader reader(64, 16, cfg);       // or reader.setConfig(cfg)
 *   INIwriter writer(1024, 64, cfg);     // or writer.setConfig(cfg)
 *   IniConfig::strict()                  // every key required, no unknown keys/sections, no empty values
 *
 * The "default" of a struct variable is the value the object had BEFORE read()
 * (default member initializer or a previously assigned value).
 *
 *   ignoreUnknownKeys      Reader: keys in the INI that do not exist in the struct are ignored (otherwise error).
 *   ignoreUnknownSections  Reader: sections in the INI that do not exist in the struct are ignored (otherwise error).
 *   requireAllKeys         Reader: every (non-optional) member must be present in the INI (otherwise the
 *                          member keeps its default).
 *   coerceInputValues      Reader: an empty value ("key =") for a number/bool keeps the default (otherwise error).
 *                          std::optional<T> becomes empty for an empty value, strings become "".
 *   caseSensitive          Reader: keys and section names are compared case-sensitive (INI default: not).
 *                          Writer: affects the duplicate key check.
 *   spaceAroundEquals      Writer: "key = value" instead of "key=value".
 *   blankLineBeforeSection Writer: empty line in front of every [section] header.
 */
struct IniConfig {
  bool ignoreUnknownKeys = true;
  bool ignoreUnknownSections = true;
  bool requireAllKeys = false;
  bool coerceInputValues = true;
  bool caseSensitive = false;

  bool spaceAroundEquals = true;
  bool blankLineBeforeSection = true;

  static constexpr IniConfig strict(){
    IniConfig c;
    c.ignoreUnknownKeys = false;
    c.ignoreUnknownSections = false;
    c.requireAllKeys = true;
    c.coerceInputValues = false;
    return c;
  }
};

REGION(initypes)
  // char[N] is a string in INI (in ReflectJSON it is an array of chars)
  template<typename T>
  inline constexpr bool is_ini_char_array_v = std::is_array_v<T> && std::rank_v<T> == 1 &&
                                              std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>;

  enum class IniTypeKind : uint8_t {
    Bool,
    Char,
    Enum,
    Int,
    FloatFixedPoint,
    Float,
    FloatDefault,
    String,          // const char* / char*
    CharArray,       // char[N]
    StringArduino,   // Arduino String
    Optional,        // std::optional<T>
    Section,         // REFLECT struct -> [section]
    Array,           // T[N] -> "a, b, c"  or (struct elements) [name.0], [name.1], ...

    Unknown
  };

  // Only float and double: long double has a different range/format per platform
  // (x86: 80 bit, many MCUs: = double) and could not be written/read back consistently.
  template<typename F>
  constexpr bool iniIsSupportedFloat() {
    using U = remove_cvref_t<F>;
    return std::is_same_v<U, float> || std::is_same_v<U, double>;
  }

  template<typename T>
  constexpr IniTypeKind deduceIniType() {
    using U = remove_cvref_t<T>;

    if constexpr (is_json_optional_v<U>){
      return IniTypeKind::Optional;
    }else if constexpr (std::is_same_v<U, bool>){
      return IniTypeKind::Bool;
    }else if constexpr (std::is_same_v<U, char>){
      return IniTypeKind::Char;
    }else if constexpr (std::is_enum_v<U>){
      return IniTypeKind::Enum;
    }else if constexpr (std::is_integral_v<U>){
      return IniTypeKind::Int;
    }else if constexpr (is_json_fixed_v<U>){
      static_assert(iniIsSupportedFloat<unwrap_numeric_t<U>>(), "fx(): long double is not supported by ReflectINI (use float or double)");
      return IniTypeKind::FloatFixedPoint;
    }else if constexpr (is_json_number_v<U>){
      static_assert(iniIsSupportedFloat<unwrap_numeric_t<U>>(), "fn(): long double is not supported by ReflectINI (use float or double)");
      return IniTypeKind::Float;
    }else if constexpr (std::is_floating_point_v<U>){
      static_assert(iniIsSupportedFloat<U>(), "long double is not supported by ReflectINI (use float or double)");
      return IniTypeKind::FloatDefault;
    }else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>){
      return IniTypeKind::String;
    }else if constexpr (is_ini_char_array_v<U>){
      return IniTypeKind::CharArray;
    #ifdef ARDUINO
    }else if constexpr (std::is_same_v<U, String>){
      return IniTypeKind::StringArduino;
    #endif
    }else if constexpr (is_reflectable_v<U>){
      return IniTypeKind::Section;
    }else if constexpr (std::is_array_v<U>){
      return IniTypeKind::Array;
    }else if constexpr (std::is_class_v<U> || std::is_union_v<U>){
      static_assert(always_false<U>, "struct/class has no REFLECT(...) macro");
      return IniTypeKind::Unknown;
    }else{
      static_assert(always_false<U>, "unsupported type for INI");
      return IniTypeKind::Unknown;
    }
  }

  // true for members that become their own [section] (struct or array of structs)
  template<typename T>
  constexpr bool isIniSection() {
    using U = remove_cvref_t<T>;
    if constexpr (is_json_optional_v<U>){
      static_assert(!is_reflectable_v<typename json_optional_value<U>::type>,
                    "std::optional<Struct> is not supported in INI (a section is simply missing or present)");
      return false;
    }else if constexpr (is_reflectable_v<U>){
      return true;
    }else if constexpr (std::is_array_v<U> && !is_ini_char_array_v<U>){
      return is_reflectable_v<std::remove_extent_t<U>>;
    }else{
      return false;
    }
  }

  // Nullable targets: std::optional<T>, const char*, char*.
  // INIwriter omits their "null" value (empty optional / nullptr), so INIreader maps a missing key back to null.
  template<typename T>
  constexpr bool isIniNullable() {
    using U = remove_cvref_t<T>;
    return is_json_optional_v<U> || std::is_same_v<U, const char*> || std::is_same_v<U, char*>;
  }
  // Sets a nullable target to null (no-op for other types)
  template<typename T>
  inline void iniSetNull(T &v) {
    using U = remove_cvref_t<T>;
    if constexpr (is_json_optional_v<U>) v.reset();
    else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>) v = nullptr;
  }

  // Type names (for error messages only)
  template<typename T>
  constexpr const char *iniTypeName() {
    using U = remove_cvref_t<T>;
    if constexpr (is_json_optional_v<U>){
      return iniTypeName<typename json_optional_value<U>::type>();
    }else if constexpr (is_ini_char_array_v<U> || std::is_same_v<U, const char*> || std::is_same_v<U, char*>){
      return "string";
    #ifdef ARDUINO
    }else if constexpr (std::is_same_v<U, String>){
      return "string";
    #endif
    }else if constexpr (std::is_array_v<U>){
      return "array";
    }else if constexpr (is_reflectable_v<U>){
      return "section";
    }else{
      return cppTypeName<U>();
    }
  }
ENDREGION()

REGION(inihelper)
  // Internal helper macros (prefixed so they do not collide with other libraries)
  #define INI_IS_BLANK(c) ((c) == ' ' || (c) == '\t')
  #define INI_IS_EOL(c)   ((c) == '\0' || (c) == '\n' || (c) == '\r')

  FORCE_INLINE char iniLower(const char c){
    return (c >= 'A' && c <= 'Z') ? char(c | 0x20) : c;
  }

  // Compares two names (keys or section names)
  FORCE_INLINE bool iniNameEquals(const char *a, const size_t al, const char *b, const size_t bl, const bool caseSensitive){
    if(al != bl) return false;
    if(caseSensitive){
      for(size_t i = 0; i < al; i++) if(a[i] != b[i]) return false;
    }else{
      for(size_t i = 0; i < al; i++) if(iniLower(a[i]) != iniLower(b[i])) return false;
    }
    return true;
  }

  // Length of s, but never scan more than maxLen + 1 characters.
  // Return value > maxLen means "too long".
  FORCE_INLINE size_t iniBoundedLen(const char *s, const size_t maxLen){
    size_t n = 0;
    while(n <= maxLen && s[n]) n++;
    return n;
  }

  // Valid key: 1..INI_MAX_KEY_LEN characters, no leading/trailing blanks,
  // none of = [ ] ; # and no line breaks / control characters (tab inside the key is allowed).
  // Same rule for INIreader (parse) and INIwriter (write).
  inline bool iniIsValidKey(const char *k, const size_t len){
    if(len == 0 || len > INI_MAX_KEY_LEN) return false;
    if(INI_IS_BLANK(k[0]) || INI_IS_BLANK(k[len - 1])) return false;
    for(size_t i = 0; i < len; i++){
      const unsigned char c = (unsigned char)k[i];
      if((c < 0x20 && c != '\t') || c == 0x7F) return false;
      if(c == '=' || c == '[' || c == ']' || c == ';' || c == '#') return false;
    }
    return true;
  }

  // Valid section name: 1..INI_MAX_SECTION_LEN characters, no leading/trailing blanks,
  // no [ ] and no line breaks / control characters. Dots separate nested sections ("network.wifi").
  // Same rule for INIreader (parse) and INIwriter (startSection).
  inline bool iniIsValidSectionName(const char *s, const size_t len){
    if(len == 0 || len > INI_MAX_SECTION_LEN) return false;
    if(INI_IS_BLANK(s[0]) || INI_IS_BLANK(s[len - 1])) return false;
    for(size_t i = 0; i < len; i++){
      const unsigned char c = (unsigned char)s[i];
      if(c < 0x20 || c == 0x7F || c == '[' || c == ']') return false;
    }
    return true;
  }

  // Error messages are cut off on purpose when they do not fit into the message buffer (snprintf is safe).
  // GCC's -Wformat-truncation would warn about such calls (depending on inlining, e.g. on ESP32). GCC reports
  // the warning at the format string, i.e. at the CALLER of the message function: it is switched off for the
  // INIreader / INIwriter class bodies (only this one warning, only library code - not the user's sketch).
  #if defined(__GNUC__) && !defined(__clang__)
    #define INI_MSG_TRUNCATION_BEGIN _Pragma("GCC diagnostic push") \
                                     _Pragma("GCC diagnostic ignored \"-Wpragmas\"") \
                                     _Pragma("GCC diagnostic ignored \"-Wformat-truncation\"")
    #define INI_MSG_TRUNCATION_END   _Pragma("GCC diagnostic pop")
  #else
    #define INI_MSG_TRUNCATION_BEGIN
    #define INI_MSG_TRUNCATION_END
  #endif

  // Shared path writer: "section.sub.key[3]"
  struct IniPathOut{
    char *b;
    size_t cap;
    size_t n = 0;
    bool full = false;

    IniPathOut(char *buf, size_t size) : b(buf), cap(size){}
    void put(const char c){
      if(n + 1 < cap) b[n++] = c; else full = true;
    }
    void put(const char *s, size_t len){ while(len--) put(*s++); }
    void putIndex(unsigned i){
      char tmp[11];
      uint8_t k = 0;
      do{ tmp[k++] = char('0' + i % 10); i /= 10; }while(i);
      put('[');
      while(k) put(tmp[--k]);
      put(']');
    }
    const char *end(){
      if(full && cap >= 4){
        n = cap - 1;
        b[n - 1] = b[n - 2] = b[n - 3] = '.';
      }
      b[n] = '\0';
      return b;
    }
  };
ENDREGION()

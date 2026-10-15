// Number formatting of ReflectCore.h (shared by INIwriter and JSONwriter):
// reflectFormatReal() must be exactly rounded (round half up of the binary value) for float and double.
// Also counts how often the old JSONwriter float arithmetic gave a different (wrong) result.
#include "ReflectCore.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <string>
#include <cstring>

// ---- old algorithm (copied 1:1 from JSONwriter::appendReal / appendRealLarge, output to a string) ----
static void oldU32(std::string &o, uint32_t x){ char b[12]; int n = snprintf(b, sizeof b, "%u", x); o.append(b, n); }
static void oldU64(std::string &o, uint64_t x){ char b[24]; int n = snprintf(b, sizeof b, "%llu", (unsigned long long)x); o.append(b, n); }
static void oldFrac(std::string &o, uint32_t frac, uint8_t d){ char b[12]; b[0]='.'; for(uint8_t i=d;i>0;i--){ b[i]=char('0'+frac%10); frac/=10; } o.append(b, d+1); }
template<typename F> static std::string oldReal(F value, uint8_t decimals){
  std::string o;
  if(decimals > MAX_FLOAT_PRECISION) decimals = MAX_FLOAT_PRECISION;
  const bool neg = value < 0; if(neg) value = -value;
  const uint32_t p10 = (uint32_t)JSON_POW10[decimals];
  if(value < (F)4.0e9){
    uint32_t ip = (uint32_t)value; const F frac = value - (F)ip; uint32_t fp = (uint32_t)(frac * (F)p10 + (F)0.5);
    if(fp >= p10){ fp -= p10; ip++; }
    if(neg && (ip != 0 || fp != 0)) o += '-';
    oldU32(o, ip); if(decimals) oldFrac(o, fp, decimals);
  }else{
    const F scaled = value * (F)p10 + (F)0.5;
    if(scaled < (F)1.8e19){ const uint64_t total = (uint64_t)scaled; if(neg) o += '-'; const uint64_t ip = decimals ? total / p10 : total; oldU64(o, ip); if(decimals) oldFrac(o, (uint32_t)(total - ip * p10), decimals); }
    else { char buf[32]; int n = snprintf(buf, sizeof(buf), "%s%.*g", neg ? "-" : "", (int)(sizeof(F) <= 4 ? 9 : 17), (double)value); o.append(buf, n); }
  }
  return o;
}
template<typename F> static std::string newReal(F v, uint8_t d){ char b[REFLECT_NUM_BUF_SIZE]; size_t n = reflectFormatReal(v, d, b); return std::string(b, n); }

// ---- exact reference: round half up of |value| * 10^d, computed with 128-bit integers ----
template<typename F> static std::string exact(F value, uint8_t d){
  const uint32_t p10 = (uint32_t)JSON_POW10[d];
  bool neg = std::signbit(value); F a = neg ? -value : value;
  if(a >= (F)18446744073709551616.0){                     // >= 2^64: exponential notation
    char buf[40]; int n = snprintf(buf, sizeof buf, "%s%.*g", neg ? "-" : "", std::is_same_v<F,float> ? 9 : 17, (double)a);
    return std::string(buf, n);
  }
  int e; F mf = std::frexp(a, &e);                         // a = mf * 2^e, mf in [0.5,1)
  const int bitsM = std::is_same_v<F,float> ? 24 : 53;
  unsigned __int128 m = (unsigned __int128)std::ldexp(mf, bitsM);   // integer mantissa
  int sh = bitsM - e;                                      // a = m / 2^sh
  unsigned __int128 total;
  if(a == 0) total = 0;
  else if(sh <= 0) total = (m << -sh) * p10;              // < 2^64 * 10^8: fits 128 bit
  else if(sh >= 127) total = 0;
  else total = (m * p10 + ((unsigned __int128)1 << (sh - 1))) >> sh;
  uint64_t ip = (uint64_t)(total / p10), fp = (uint64_t)(total % p10);
  std::string o; if(neg && total != 0) o += '-';
  oldU64(o, ip); if(d) oldFrac(o, (uint32_t)fp, d);
  return o;
}

int main(int argc, char **argv){
  std::mt19937_64 rng(12345);
  auto r2 = [](std::mt19937_64 &g){ return g(); };
  long n = 0, diffOld = 0, wrongNew = 0, wrongOld = 0;
  FILE *diffs = nullptr;
  auto check = [&](auto v, uint8_t d){
    using F = decltype(v);
    if(!std::isfinite(v)) return;
    const std::string o = oldReal<F>(v, d), nw = newReal<F>(v, d), ex = exact<F>(v, d);
    n++;
    if(nw != ex){ if(wrongNew < 12) printf("NEW WRONG %s %.17g d=%u old=%s new=%s exact=%s\n", sizeof(F)==4?"f":"d", (double)v, d, o.c_str(), nw.c_str(), ex.c_str()); wrongNew++; }
    if(o != ex) wrongOld++;
    if(o != nw){ diffOld++; if(diffs && diffOld <= 2000) fprintf(diffs, "%s %.25g %u %s %s\n", sizeof(F)==4?"f":"d", (double)v, d, o.c_str(), nw.c_str()); }
  };
  const long N = argc > 1 ? atol(argv[1]) : 300000;
  for(long i = 0; i < N; i++){
    const uint8_t d = uint8_t(i % 9);
    uint64_t r = rng();
    // random bit patterns (all magnitudes) and "typical" values (decimal-ish numbers)
    float f; uint32_t fb = (uint32_t)r; memcpy(&f, &fb, 4); check(f, d);
    double db; memcpy(&db, &r, 8); check(db, d);
    const double typical = (double)(int64_t)(r % 2000000001 - 1000000000) / JSON_POW10[(r >> 40) % 9];
    check((float)typical, d); check(typical, d);
    check((float)(typical * 1000.0), d); check(typical / 1000.0, d);
    const double big = std::ldexp(1.0 + (double)(r2(rng) & 0xFFFFFFFFFFFFFull) / 4503599627370496.0, 31 + int(i % 40));   // 2^31 .. 2^71
    check(big, d); check(-big, d); check((float)big, d); check((float)-big, d);
  }
  // edge cases
  const double edges[] = {0.0, -0.0, 0.5, 1.5, 2.5, 0.125, 0.005, 2.675, 1e-9, 4e9, -4e9, 3999999999.5, 4294967295.0, 1e-45, 1.17549435e-38,
                          0.995, 0.9999999999, 99.995, 123456.789, 1e18, 1.8e19, 1e300, -1e-300, 2.2250738585072014e-308, 5e-324,
                          4000000000.0, 73995574643673616.0, 4294967296.5, 9007199254740993.0, 4503599627370495.5, 1.8446744073709552e19,
                          18446744073709549568.0, 1e17, 123456789012.345, 3999999999.99999, 4000000000.00001, 1e15 + 0.125};
  for(double e : edges) for(uint8_t d = 0; d <= 8; d++){ check((float)e, d); check(e, d); check((float)-e, d); check(-e, d); }
  printf("values checked: %ld\nold JSONwriter float arithmetic != exact: %ld\nnew != exact: %ld\n", n, wrongOld, wrongNew);
  printf("%s\n", wrongNew ? "FAILED" : "ALL OK (exactly rounded)");
  if(diffs) fclose(diffs);
  return wrongNew ? 1 : 0;
}

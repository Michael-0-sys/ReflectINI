// Integer / fraction digit output of ReflectCore.h against snprintf
#include "ReflectCore.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <limits>
template<typename I> static bool chk(I v){ char a[40], b[32]; int n = std::is_signed_v<I> ? snprintf(a, sizeof a, "%lld", (long long)v) : snprintf(a, sizeof a, "%llu", (unsigned long long)v);
  size_t m = reflectFormatInt(v, b); if(size_t(n) != m || memcmp(a, b, m)){ printf("INT FAIL %s vs %.*s\n", a, (int)m, b); return false; } return true; }
int main(){
  long bad = 0; std::mt19937_64 rng(3);
  for(uint64_t p = 1, k = 0; k < 20; k++, p *= 10) for(int64_t d = -2; d <= 2; d++){ bad += !chk<uint64_t>(p + d); bad += !chk<int64_t>((int64_t)(p + d)); bad += !chk<int64_t>(-(int64_t)(p + d)); bad += !chk<uint32_t>((uint32_t)(p + d)); bad += !chk<int32_t>((int32_t)(p+d)); }
  bad += !chk(std::numeric_limits<int64_t>::min()) + !chk(std::numeric_limits<int64_t>::max()) + !chk(UINT64_MAX) + !chk(INT32_MIN) + !chk(INT32_MAX) + !chk(UINT32_MAX) + !chk((int8_t)-128) + !chk((uint16_t)65535) + !chk(0);
  for(long i = 0; i < 2000000; i++){ uint64_t r = rng() >> (rng() % 64); bad += !chk<uint64_t>(r) + !chk<int64_t>((int64_t)r) + !chk<uint32_t>((uint32_t)r) + !chk<int32_t>((int32_t)r) + !chk<int16_t>((int16_t)r); }
  for(uint32_t d = 0; d <= 8; d++) for(uint32_t f = 0; f < (d ? (uint32_t)JSON_POW10[d] : 1); f += 1 + f / 997){ char b[16], e[16]; reflectWriteDigits32(b, f, d); snprintf(e, sizeof e, "%0*u", (int)d, f); if(d && memcmp(b, e, d)){ printf("FRAC FAIL %u %u\n", d, f); bad++; } }
  printf("integer/fraction check: %ld failures\n", bad); return bad != 0;
}

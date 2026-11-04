// Link the native hooks with the original generated definitions.
#include "minininjas_pch.h"
#include "../../src/minininjas_native.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>

DECLARE_REX_FUNC(sub_82417FF0);
DECLARE_REX_FUNC(sub_82501EF0);
bool TestWorkerBarrier();

namespace {
alignas(64) std::array<uint8_t, 0x12000> original, native;
uint32_t seed = 0x76d51c3a;
uint32_t Random() {
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  return seed;
}

bool Compare(PPCFunc* reference, PPCFunc* replacement, PPCContext& before, unsigned n) {
  PPCContext a{}, b{};
  std::memcpy(&a, &before, sizeof(a));
  std::memcpy(&b, &before, sizeof(b));
  native = original;
  before.fpscr.setcsr(before.fpscr.csr);
  reference(a, original.data());
  before.fpscr.setcsr(before.fpscr.csr);
  replacement(b, native.data());
  if (!std::memcmp(&a, &b, sizeof(a)) && original == native) return true;
  for (size_t k = 0; k < sizeof(a); ++k) {
    if (reinterpret_cast<uint8_t*>(&a)[k] != reinterpret_cast<uint8_t*>(&b)[k]) {
      std::printf("case %u: PPC context differs at byte %zu\n", n, k); break;
    }
  }
  for (size_t k = 0; k < native.size(); ++k) if (original[k] != native[k]) {
    std::printf("case %u: memory differs at byte %zu\n", n, k); break;
  }
  return false;
}

bool Test() {
  constexpr uint32_t special[] = {0, 0x80000000, 1, 0x80000001, 0x7f800000, 0xff800000,
      0x7fc12345, 0xffc54321, 0x7f812345, 0x7f7fffff, 0xff7fffff, 0x3f800000};
  for (unsigned n = 0; n < 768; ++n) {
    PPCContext ctx{};
    for (auto* reg : {&ctx.r0,&ctx.r3,&ctx.r4,&ctx.r5,&ctx.r6,&ctx.r7,&ctx.r8,&ctx.r9,
                      &ctx.r10,&ctx.r11,&ctx.ctr}) reg->u64 = uint64_t(Random()) << 32 | Random();
    ctx.lr = Random();
    ctx.xer.ca = n & 1; ctx.xer.so = (n >> 1) & 1;
    ctx.fpscr.InitHost();
    ctx.fpscr.storeFromGuest(n % 4);
    for (unsigned i = 0xf000; i < original.size(); i += 4) {
      uint32_t bits = n % 3 == 0 ? special[Random() % std::size(special)]
                                 : std::bit_cast<uint32_t>(static_cast<float>(int32_t(Random())) *
                                                         (n % 3 == 1 ? 0x1p-31f : 0.00001f));
      bits = __builtin_bswap32(bits);
      std::memcpy(&original[i], &bits, 4);
    }
    ctx.r3.u32 = 0x10800 + n % 16;
    const int delta[] = {-80, -48, -16, 0, 16, 48, 512};
    ctx.r4.u32 = ctx.r3.u32 + delta[n % 7] + n % 3;
    ctx.r5.u32 = n % 5 == 0 ? ctx.r3.u32 : 0x10120u + n % 16;
    ctx.r6.u32 = n % 6 == 0 ? ctx.r4.u32 : 0x10140u + n % 16;
    ctx.r7.u32 = n % 130;
    if (!Compare(__imp__sub_82501EF0, sub_82501EF0, ctx, n)) return false;
    ctx.r3.u32 = (n % 31 ? 0x10700 : 0xf104) + n % 4;
    ctx.r4.u64 = uint64_t(Random()) << 32 | Random();
    ctx.r5.u32 = n % 262;
    if (!Compare(__imp__sub_82417FF0, sub_82417FF0, ctx, n)) return false;
  }
  return true;
}

#ifdef MINI_NATIVE_BENCH
long long Time(PPCFunc* f, bool vector) {
  PPCContext ctx{};
  ctx.fpscr.InitHost();
  for (unsigned i = 0x10000; i < original.size(); i += 4) {
    const uint32_t bits = __builtin_bswap32(std::bit_cast<uint32_t>(0.0001f));
    std::memcpy(original.data() + i, &bits, 4);
  }
  const auto start = std::chrono::steady_clock::now();
  for (unsigned n = 0; n < 6000; ++n) {
    ctx.r3.u64 = 0x10800;
    ctx.r4.u64 = vector ? 0x10400 : (n & 255);
    ctx.r5.u64 = vector ? 0x10000 : 1024;
    ctx.r6.u64 = 0x10010;
    ctx.r7.u64 = 256;
    f(ctx, original.data());
  }
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
}

void Benchmark() {
  for (bool vector : {false, true}) {
    auto* ref = vector ? __imp__sub_82501EF0 : __imp__sub_82417FF0;
    auto* fast = vector ? sub_82501EF0 : sub_82417FF0;
    for (unsigned rep = 0; rep < 5; ++rep) {
      const auto first = Time(rep % 2 ? fast : ref, vector);
      const auto second = Time(rep % 2 ? ref : fast, vector);
      std::printf("%s original=%lld native=%lld us\n", vector ? "vector" : "fill",
                  rep % 2 ? second : first, rep % 2 ? first : second);
    }
  }
}
#endif
}  // namespace

int main() {
#ifdef __SWITCH__
  if (!std::freopen("sdmc:/switch/mini-ninjas/native-test.txt", "w", stdout)) return 2;
#endif
  minininjas::cpu::EnableNativeRamp(true);
  const bool ok = Test() && TestWorkerBarrier();
  std::puts(ok ? "PASS: 768 vector and 768 fill cases" : "FAIL");
#ifdef MINI_NATIVE_BENCH
  if (ok) Benchmark();
#endif
  std::fflush(stdout);
  return ok ? 0 : 1;
}

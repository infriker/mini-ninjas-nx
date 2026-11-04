#include "minininjas_pch.h"
#include "../../src/minininjas_native.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#if defined(__linux__) && !defined(__SWITCH__)
#include <pthread.h>
#endif

REX_EXTERN(sub_824F9340);
REX_EXTERN(__imp__sub_824F9340);

namespace {
using Memory = std::array<uint8_t, 0x10000>;
constexpr uint32_t kOwner = 0x4000;

void PutWord(Memory& mem, uint32_t at, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(mem.data() + at, &value, sizeof(value));
}

void Configure(Memory& mem, uint32_t owner, unsigned workers) {
  PutWord(mem, owner + 304, __builtin_popcount(workers));
  for (unsigned i = 0; i < 6; ++i) {
    PutWord(mem, owner + 308 + 4 * i, (workers & (1u << i)) ? 0x80001000u + i : 0);
  }
}

void SetArgs(PPCContext& ctx, uint32_t owner, uint32_t flags, unsigned worker) {
  ctx.r3.u64 = owner;
  ctx.r4.u64 = flags;
  ctx.r13.u64 = 0x1000 + 512 * worker;
}

bool Compare(unsigned mask, unsigned last, bool disable, bool fast) {
  alignas(64) Memory original{}, candidate{};
  constexpr uint32_t flags = 0x6500;
  Configure(original, kOwner, mask);
  if (disable || mask == 0) PutWord(original, kOwner + 304, disable ? 0 : 1);
  for (unsigned i = 0; i < 6; ++i) {
    original[flags + i] = (i != last && (mask & (1u << i))) ? 1 : 0;
  }
  original[0x1000 + 512 * last + 268] = last;
  original[flags - 1] = original[flags + 8] = 0x5a;
  candidate = original;
  PPCContext old{}, actual{};
  SetArgs(old, kOwner, flags, last);
  old.r8.u64 = old.r9.u64 = old.r10.u64 = old.r11.u64 = UINT64_C(0x123456789abcdef0);
  old.xer.ca = 1;
  old.xer.so = mask & 1;
  old.cr6.compare<int32_t>(-1, 0, old.xer);
  std::memcpy(&actual, &old, sizeof(old));
  minininjas::cpu::EnableWorkerWait(fast);
  __imp__sub_824F9340(old, original.data());
  sub_824F9340(actual, candidate.data());
  return !std::memcmp(&old, &actual, sizeof(old)) && candidate == original;
}

bool ConcurrentRounds(Memory& mem, uint32_t owner, unsigned mask, bool alternate) {
  constexpr unsigned rounds = 320;
  std::array<std::atomic<unsigned>, 6> arrived{};
  std::atomic<bool> correct{true};
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 6; ++i) {
    if (!(mask & (1u << i))) continue;
    threads.emplace_back([&, i] {
      PPCContext ctx{};
      for (unsigned round = 1; round <= rounds; ++round) {
        const uint32_t word = owner + 356 + (alternate ? 8 * (round & 1) : 0);
        SetArgs(ctx, owner, word, i);
        if (round % 29 == 0 && i == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        arrived[i].store(round, std::memory_order_release);
        sub_824F9340(ctx, mem.data());
        for (unsigned j = 0; j < 6; ++j) {
          if ((mask & (1u << j)) && arrived[j].load(std::memory_order_acquire) < round) {
            correct.store(false, std::memory_order_relaxed);
          }
        }
      }
    });
  }
  for (auto& thread : threads) thread.join();
  for (unsigned i = 0; i < 16; ++i) {
    if (mem[owner + 356 + i]) return false;
  }
  return correct.load();
}

#if defined(__linux__) && !defined(__SWITCH__)
bool CancelWaitingWorker() {
  alignas(64) Memory mem{};
  Configure(mem, kOwner, 3);
  mem[0x1000 + 512 + 268] = 1;
  std::atomic<bool> started{false};
  std::thread first([&] {
    PPCContext ctx{};
    SetArgs(ctx, kOwner, kOwner + 356, 0);
    started.store(true, std::memory_order_release);
    sub_824F9340(ctx, mem.data());
  });
  while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
  // Deferred cancellation is delivered at the blocking wait, after arrival.
  if (pthread_cancel(first.native_handle()) != 0) std::abort();
  first.join();
  if (mem[kOwner + 356] != 1) return false;
  PPCContext last{};
  SetArgs(last, kOwner, kOwner + 356, 1);
  sub_824F9340(last, mem.data());
  return mem[kOwner + 356] == 0 && mem[kOwner + 357] == 0;
}
#endif
}  // namespace

bool TestWorkerBarrier() {
  for (unsigned mask = 1; mask < 64; ++mask) {
    for (unsigned i = 0; i < 6; ++i) {
      if ((mask & (1u << i)) && !Compare(mask, i, false, true)) return false;
    }
  }
  // A disabled group, the original NRO path, and a cleared word on first read.
  if (!Compare(3, 1, true, true) || !Compare(3, 1, false, false) ||
      !Compare(0, 8, false, true)) return false;
  minininjas::cpu::EnableWorkerWait(true);
#if defined(__linux__) && !defined(__SWITCH__)
  if (!CancelWaitingWorker()) return false;
#endif
  alignas(64) Memory mem{};
  for (unsigned i = 0; i < 6; ++i) mem[0x1000 + 512 * i + 268] = i;
  // A pair of unaligned words per mixer. These two different mixers hash to
  // the same wait queues and complete independent, overlapping generations.
  constexpr uint32_t second = kOwner + 0x800;
  Configure(mem, kOwner, 0x3f);
  Configure(mem, second, 0x25);
  bool first_ok = false;
  std::thread other([&] { first_ok = ConcurrentRounds(mem, kOwner, 0x3f, false); });
  const bool second_ok = ConcurrentRounds(mem, second, 0x25, true);
  other.join();
  if (!(first_ok && second_ok)) return false;
  std::puts("PASS: worker masks and PPC state, independent barriers, 640 repeated/alternating rounds");
  return true;
}

#include "minininjas_pch.h"
#include "minininjas_native.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

REX_EXTERN(__imp__sub_824F9340);

namespace {
bool worker_wait_enabled = true;

struct Waiter {
  const uint8_t* flags;
  uint64_t expected;
  std::atomic<bool> done{false};
  bool sleeping = false;  // protected by the queue mutex
};

struct WaitQueue {
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<Waiter*> pending;

  // Called with the mutex held. The two guest words can be only four-byte
  // aligned, so an atomic_ref<uint64_t> would not be valid here.
  bool FinishIfReady(uint8_t* flags, uint64_t expected) {
    uint64_t value;
    std::memcpy(&value, flags, sizeof(value));
    value = __builtin_bswap64(value);
    if (value != 0 && value != expected &&
        std::none_of(pending.begin(), pending.end(), [&](const Waiter* waiter) {
          return waiter->flags == flags && waiter->expected == value;
        })) {
      return false;
    }
    if (value != 0) {
      const uint64_t zero = 0;
      std::memcpy(flags, &zero, sizeof(zero));
    }
    // Complete exactly these tickets. A thread can re-enter the same guest
    // barrier before a participant from this round has been scheduled again.
    bool wake = false;
    std::erase_if(pending, [&](Waiter* waiter) {
      if (waiter->flags != flags) return false;
      wake |= waiter->sleeping;
      waiter->done.store(true, std::memory_order_release);
      return true;
    });
    if (wake) changed.notify_all();
    return true;
  }
};

std::array<WaitQueue, 16> queues;
constexpr unsigned kShortSpins = 32;
constexpr auto kGuestRecheck = std::chrono::milliseconds(1);
}  // namespace

void minininjas::cpu::EnableWorkerWait(bool enabled) {
  worker_wait_enabled = enabled;
}

REX_EXTERN(sub_824F9340) {
  if (!worker_wait_enabled) return __imp__sub_824F9340(ctx, base);

  const uint32_t owner = ctx.r3.u32;
  ctx.r11.u64 = REX_LOAD_U32(owner + 304);
  ctx.cr6.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
  if (ctx.cr6.eq) return;

  const uint32_t worker = REX_LOAD_U8(ctx.r13.u32 + 268);
  uint8_t* flags = REX_RAW_ADDR(ctx.r4.u32);
  auto& queue = queues[(reinterpret_cast<uintptr_t>(flags) >> 3) % queues.size()];
  std::unique_lock lock(queue.mutex);
  REX_STORE_U8(ctx.r4.u32 + worker, 1);

  // The guest reads six worker handles in descending order, constructing a
  // big-endian mask. r8, r9, r10 and XER/CR6 are visible after the return.
  uint64_t upper = 0;
  for (uint32_t offset = 328; offset >= 312; offset -= 4) {
    upper = (upper >> 8) | (uint64_t(REX_LOAD_U32(owner + offset) != 0) << 56);
  }
  ctx.r8.u64 = upper;
  ctx.r9.u64 = upper >> 8;
  ctx.r10.u64 = ctx.r9.u64 | (uint64_t(REX_LOAD_U32(owner + 308) != 0) << 56);
  ctx.r11.u64 = 0;
  ctx.xer.ca = 0;
  ctx.cr6.compare<int64_t>(0, 0, ctx.xer);
  if (queue.FinishIfReady(flags, ctx.r10.u64)) return;

  Waiter ticket{flags, ctx.r10.u64};
  queue.pending.push_back(&ticket);
  // A cancelled pthread can unwind a condition-variable wait. Unlink its
  // stack ticket even if the guest round hasn't completed yet.
  auto unlink = [&](Waiter* waiter) {
    if (waiter->done.load(std::memory_order_acquire)) return;
    if (!lock.owns_lock()) lock.lock();
    std::erase(queue.pending, waiter);
  };
  std::unique_ptr<Waiter, decltype(unlink)> registration(&ticket, unlink);
  lock.unlock();
  for (unsigned i = 0; i < kShortSpins; ++i) {
    if (ticket.done.load(std::memory_order_acquire)) return;
#if defined(__aarch64__)
    asm volatile("yield");
#else
    simde_mm_pause();
#endif
  }
  lock.lock();
  ticket.sleeping = true;
  while (!ticket.done.load(std::memory_order_relaxed)) {
    if (queue.FinishIfReady(flags, ticket.expected)) break;
    // A guest-side reset can also change the word. A timeout only rechecks
    // the predicate. It never lets a worker past an incomplete barrier.
    queue.changed.wait_for(lock, kGuestRecheck, [&] {
      return ticket.done.load(std::memory_order_relaxed);
    });
  }
}

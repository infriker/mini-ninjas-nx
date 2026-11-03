#include "minininjas_coroutines.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#include <rex/ppc/context.h>
#include <rex/thread/fiber.h>

namespace minininjas::coroutines {
using rex::thread::Fiber;

struct JumpPoint {
  std::jmp_buf env;
  Fiber* fiber = nullptr;
  uint64_t scope = 0;
  uint32_t guest_buffer = 0;
  uint32_t guest_lr = 0;
};

namespace {
constexpr uint32_t kSchedulerBuffer = 0x82E5C8A0;
constexpr uint32_t kGuestYieldReturn = 0x824026E4;
constexpr uint32_t kGuestResumeReturn = 0x824023D8;
constexpr uint32_t kGuestFinishReturn = 0x82402868;
constexpr uint32_t kGuestLaunchReturn = 0x82402430;
constexpr size_t kHostFiberStackBytes = 2 * 1024 * 1024;

struct Task {
  uint32_t key = 0;
  Fiber* fiber = nullptr;
  Fiber* parent = nullptr;
  bool finished = false;
  PPCContext* ctx = nullptr;
  uint8_t* base = nullptr;
  PPCFunc* guest_entry = nullptr;
};
struct Pending {
  JumpPoint* point = nullptr;
  int value = 0;
};
struct State {
  uint64_t next_scope = 1;
  bool restoring_guest = false;
  std::unordered_map<uint32_t, std::unique_ptr<JumpPoint>> points;
  std::unordered_map<Fiber*, std::unique_ptr<Task>> tasks;
  std::unordered_map<Fiber*, Pending> pending;
};
thread_local State state;

[[noreturn]] void Fail(const char* reason, uint32_t value = 0) {
  std::fprintf(stderr, "Mini Ninjas coroutine bridge: %s (0x%08X)\n", reason, value);
  std::fflush(stderr);
  std::abort();
}

Fiber* Current() {
  Fiber* fiber = Fiber::Current();
  if (!fiber) Fail("current thread is not a host fiber");
  return fiber;
}

Task* CurrentTask() {
  auto it = state.tasks.find(Current());
  return it == state.tasks.end() ? nullptr : it->second.get();
}

JumpPoint* FindPoint(uint32_t guest_buffer) {
  auto it = state.points.find(guest_buffer);
  if (it == state.points.end()) Fail("unknown or expired guest jump buffer", guest_buffer);
  return it->second.get();
}

void EraseFiberPoints(Fiber* fiber) {
  for (auto it = state.points.begin(); it != state.points.end();) {
    if (it->second->fiber == fiber) it = state.points.erase(it);
    else ++it;
  }
  state.pending.erase(fiber);
}

void ReapFinished() {
  Fiber* current = Current();
  for (auto it = state.tasks.begin(); it != state.tasks.end();) {
    Task* task = it->second.get();
    if (!task->finished) { ++it; continue; }
    if (task->fiber == current || task->parent != current)
      Fail("invalid finished-fiber owner", task->key);
    EraseFiberPoints(task->fiber);
    task->fiber->Destroy();
    it = state.tasks.erase(it);
  }
}

[[noreturn]] void DeliverPending() {
  Fiber* current = Current();
  auto it = state.pending.find(current);
  if (it == state.pending.end() || !it->second.point) Fail("fiber resumed without a jump");
  JumpPoint* point = it->second.point;
  state.pending.erase(it);
  if (point->fiber != current) Fail("jump landed on the wrong fiber", point->guest_buffer);
  std::longjmp(point->env, 1);
}

[[noreturn]] void Transfer(JumpPoint* target, int value, bool finishing) {
  Fiber* from = Current();
  if (finishing) {
    Task* task = CurrentTask();
    if (!task || task->parent != target->fiber || task->finished)
      Fail("invalid task completion");
    task->finished = true;
  }
  if (state.pending.contains(target->fiber)) Fail("duplicate pending jump", target->guest_buffer);
  state.pending.emplace(target->fiber, Pending{target, value ? value : 1});
  if (target->fiber != from) Fiber::SwitchTo(target->fiber);
  // A switched-away fiber returns here only when another jump resumes it.
  DeliverPending();
}

void TaskEntry(void* argument) {
  Task* task = static_cast<Task*>(argument);
  task->guest_entry(*task->ctx, task->base);
  Fail("task entry returned without a completion jump", task->key);
}

void LaunchTask(uint32_t key, PPCContext* ctx, uint8_t* base, PPCFunc* guest_entry) {
  Fiber* parent = Current();
  ReapFinished();
  for (const auto& [fiber, task] : state.tasks) {
    if (task->key == key) Fail("task address already has a live fiber", key);
  }
  Fiber* child;
  {
    auto task = std::make_unique<Task>();
    task->key = key; task->parent = parent; task->ctx = ctx; task->base = base;
    task->guest_entry = guest_entry;
    Task* raw = task.get();
    raw->fiber = Fiber::Create(kHostFiberStackBytes, TaskEntry, raw);
    if (!raw->fiber) Fail("could not allocate host task fiber", key);
    child = raw->fiber;
    state.tasks.emplace(child, std::move(task));
  }
  // No nontrivial locals may remain live across the following host longjmp.
  Fiber::SwitchTo(child);
  DeliverPending();
}
}  // namespace

CaptureScope::CaptureScope() : id_(state.next_scope++) { (void)Current(); }
CaptureScope::~CaptureScope() {
  for (auto it = state.points.begin(); it != state.points.end();) {
    if (it->second->scope == id_) it = state.points.erase(it);
    else ++it;
  }
}

JumpPoint* Capture(CaptureScope& scope, uint32_t guest_buffer, uint32_t guest_lr) {
  if (!guest_buffer) Fail("zero guest jump buffer");
  Fiber* current = Current();
  auto it = state.points.find(guest_buffer);
  if (it != state.points.end()) {
    if (it->second->scope != scope.id() || it->second->fiber != current)
      Fail("guest jump buffer already owned by another live frame", guest_buffer);
    it->second->guest_lr = guest_lr;
    return it->second.get();
  }
  auto point = std::make_unique<JumpPoint>();
  point->fiber = current; point->scope = scope.id(); point->guest_buffer = guest_buffer;
  point->guest_lr = guest_lr;
  JumpPoint* raw = point.get();
  state.points.emplace(guest_buffer, std::move(point));
  return raw;
}

std::jmp_buf& Environment(JumpPoint* point) { return point->env; }
void Landed(JumpPoint* point) {
  if (!point || point->fiber != Current()) Fail("invalid jump landing");
  ReapFinished();
}

bool GuestRestoreAllowed() { return state.restoring_guest; }
void CheckGuestRestoreAllowed() {
  if (!GuestRestoreAllowed()) Fail("unsupported direct guest longjmp");
}

[[noreturn]] void Jump(PPCContext& ctx, uint8_t* base, PPCFunc* restore) {
  const uint32_t guest_buffer = ctx.r3.u32;
  const int value = ctx.r4.s32;
  const uint32_t site = static_cast<uint32_t>(ctx.lr);
  const bool finishing = site == kGuestFinishReturn;
  Task* task = CurrentTask();
  if (site == kGuestYieldReturn || finishing) {
    if (!task || guest_buffer != kSchedulerBuffer) Fail("unexpected yield or finish target", guest_buffer);
  } else if (site == kGuestResumeReturn) {
    if (task || guest_buffer == kSchedulerBuffer) Fail("unexpected resume target", guest_buffer);
  } else {
    Fail("unsupported guest longjmp call site", site);
  }
  JumpPoint* target = FindPoint(guest_buffer);
  if (!restore || state.restoring_guest) Fail("invalid nested guest context restore", guest_buffer);
  state.restoring_guest = true;
  restore(ctx, base);
  state.restoring_guest = false;
  if (target->guest_lr && static_cast<uint32_t>(ctx.lr) != target->guest_lr)
    Fail("guest restore returned to an unexpected LR", static_cast<uint32_t>(ctx.lr));
  if (ctx.r3.u32 != static_cast<uint32_t>(value ? value : 1))
    Fail("guest restore returned an unexpected value", ctx.r3.u32);
  Transfer(target, value, finishing);
}

void Launch(PPCContext& ctx, uint8_t* base, PPCFunc* entry) {
  if (static_cast<uint32_t>(ctx.lr) != kGuestLaunchReturn || CurrentTask())
    Fail("unsupported guest task launch", static_cast<uint32_t>(ctx.lr));
  if (!entry || !ctx.r3.u32) Fail("invalid guest task entry", ctx.r3.u32);
  LaunchTask(ctx.r3.u32, &ctx, base, entry);
}
}  // namespace minininjas::coroutines

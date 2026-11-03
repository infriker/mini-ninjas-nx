// Mini Ninjas' stackful coroutine bridge.
#pragma once

#include <csetjmp>
#include <cstdint>
#include <rex/ppc/func.h>

namespace minininjas::coroutines {

// Place in the guest function that owns an instrumented setjmp. Its frame
// must remain alive while the corresponding host jump buffer can be used.
class CaptureScope {
 public:
  CaptureScope();
  ~CaptureScope();
  CaptureScope(const CaptureScope&) = delete;
  CaptureScope& operator=(const CaptureScope&) = delete;
  uint64_t id() const { return id_; }
 private:
  uint64_t id_;
};

struct JumpPoint;
JumpPoint* Capture(CaptureScope& scope, uint32_t guest_buffer, uint32_t guest_lr = 0);
std::jmp_buf& Environment(JumpPoint* point);
void Landed(JumpPoint* point);

// The actual original guest restore is executed before switching host fibers.
// Unsupported targets fail loudly rather than returning into the wrong frame.
[[noreturn]] void Jump(PPCContext& ctx, uint8_t* base, PPCFunc* restore);
bool GuestRestoreAllowed();
void CheckGuestRestoreAllowed();

// Launch the specific guest task entry on its own stackful host fiber.
void Launch(PPCContext& ctx, uint8_t* base, PPCFunc* entry);

}  // namespace minininjas::coroutines

// std::setjmp must execute in the owning generated function's frame, not in a
// helper that has already returned. The original guest function still saves the
// guest register image; longjmp resumes with guest registers already restored.
#define MINININJAS_CAPTURE(scope, ctx, base, original)                  \
  do {                                                                 \
    const uint32_t mini_guest_buffer = (ctx).r3.u32;                   \
    (original)((ctx), (base));                                         \
    auto* mini_point = ::minininjas::coroutines::Capture(              \
        (scope), mini_guest_buffer, static_cast<uint32_t>((ctx).lr));   \
    if (setjmp(::minininjas::coroutines::Environment(mini_point)))     \
      ::minininjas::coroutines::Landed(mini_point);                    \
  } while (false)

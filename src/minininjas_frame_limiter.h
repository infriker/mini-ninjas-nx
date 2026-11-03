#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace minininjas {

// Called once per guest Swap, outside renderer and guest locks. The GPU ring
// waits; guest time and the independent 60 Hz vblank thread keep running.
class FrameLimiter {
 public:
  bool Start(int32_t fps) {
    std::lock_guard lock(mutex_);
    const bool valid = fps >= 0 && fps <= 240;
    interval_ = valid && fps ? std::chrono::nanoseconds((1'000'000'000LL + fps - 1) / fps)
                            : std::chrono::nanoseconds::zero();
    stopped_ = false;
    have_frame_ = false;
    return valid;
  }

  // False means shutdown interrupted the wait: do not present another frame.
  bool Wait() {
    std::unique_lock lock(mutex_);
    if (stopped_) return false;
    if (interval_ == std::chrono::nanoseconds::zero()) return true;
    if (have_frame_ && cv_.wait_until(lock, last_frame_ + interval_, [this] { return stopped_; }))
      return false;
    // Base the next deadline on the actual frame, not an old deadline. Loading
    // stalls and late wakeups must not cause a burst of catch-up frames.
    last_frame_ = Clock::now();
    have_frame_ = true;
    return true;
  }

  void Stop() {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
    }
    cv_.notify_all();
  }

 private:
  using Clock = std::chrono::steady_clock;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::chrono::nanoseconds interval_{};
  Clock::time_point last_frame_{};
  bool have_frame_ = false;
  bool stopped_ = true;
};

}  // namespace minininjas

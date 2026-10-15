// The PS5's frame counters: what the title's results file reports every five
// seconds (ReXApp's reporter on the PS5), so a slow run says where frames stop.
// Times are host ticks (rex::chrono::Clock, the TSC on the console).
#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>

#include <rex/chrono/clock.h>

namespace rex::ps5 {

// Frames the renderer submitted to the GPU
inline std::atomic<uint64_t> renderer_frames{0};
// Frames the presenter put on the screen
inline std::atomic<uint64_t> screen_frames{0};

// The game's main thread (XThread::SetActiveCpu records it), and the time it
// spent blocked in the guest's waits and sleeps (XObject waits, XThread::Delay)
inline std::atomic<uintptr_t> main_guest_thread{0};
inline std::atomic<uint64_t> main_wait_ticks{0};
// The time the game's threads waited for room in the renderer's queue, and for
// texture locks the renderer completes (synchronous), with the locks' count
inline std::atomic<uint64_t> queue_wait_ticks{0};
inline std::atomic<uint64_t> lock_wait_ticks{0};
inline std::atomic<uint64_t> lock_waits{0};
// The time the renderer's worker waited for the game's commands, and the time
// the host waited for the GPU to finish a submission (VulkanSubmissionTracker)
inline std::atomic<uint64_t> render_idle_ticks{0};
inline std::atomic<uint64_t> gpu_wait_ticks{0};

// Adds the time from construction to destruction to a counter
class TickScope {
 public:
  explicit TickScope(std::atomic<uint64_t>& counter, bool active = true)
      : counter_(counter), begin_(active ? rex::chrono::Clock::QueryHostTickCount() : 0) {}
  ~TickScope() {
    if (begin_) {
      counter_.fetch_add(rex::chrono::Clock::QueryHostTickCount() - begin_,
                         std::memory_order_relaxed);
    }
  }
  TickScope(const TickScope&) = delete;
  TickScope& operator=(const TickScope&) = delete;

 private:
  std::atomic<uint64_t>& counter_;
  uint64_t begin_;
};

inline bool OnMainGuestThread() {
  return reinterpret_cast<uintptr_t>(pthread_self()) ==
         main_guest_thread.load(std::memory_order_relaxed);
}

// For the guest's waits: counts only on the main thread
class MainWaitScope : public TickScope {
 public:
  MainWaitScope() : TickScope(main_wait_ticks, OnMainGuestThread()) {}
};

}  // namespace rex::ps5

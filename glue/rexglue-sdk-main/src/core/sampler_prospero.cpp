/**
 * @file        core/sampler_prospero.cpp
 * @brief       PS5: a sampling profiler of one thread, for the results file
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/platform/sampler_prospero.h>

#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/thread.h>
#include <rex/ui/startup_prospero.h>

REXCVAR_DEFINE_BOOL(ps5_sampler, true, "PS5",
                    "Sample the renderer's worker thread and report where its time goes in the "
                    "results file");

namespace rex::ps5 {
namespace {

// SIGUSR1 and SIGUSR2 are the runtime's own (threading_posix.cpp)
constexpr int kSignal = SIGPROF;
constexpr int kDepth = 8;
constexpr size_t kRingSize = 16384;
constexpr auto kWindow = std::chrono::seconds(30);
// Addresses are counted in blocks this size: any address in a block names its
// function, and a function's blocks are added back together by its name
constexpr uint64_t kBlock = 64;

struct Sample {
  uint64_t pc[kDepth];  // [0] where the thread was, then its callers; 0 ends
};

Sample ring[kRingSize];
std::atomic<uint64_t> written{0};
std::atomic<uint64_t> consumed{0};
std::atomic<uint64_t> dropped{0};
uintptr_t target_stack_top = 0;

// On the sampled thread: async-signal-safe, no locks, no allocation. Reads only
// the thread's own stack, between its stack pointer and stack_top; code is
// never read (the console ends a title that reads its execute-only code)
void OnSignal(int, siginfo_t*, void* context) {
  const uint64_t index = written.load(std::memory_order_relaxed);
  if (index - consumed.load(std::memory_order_acquire) >= kRingSize) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const mcontext_t& machine = static_cast<ucontext_t*>(context)->uc_mcontext;
  Sample& sample = ring[index % kRingSize];
  sample.pc[0] = uint64_t(machine.mc_rip);
  uintptr_t frame = uintptr_t(machine.mc_rbp);
  uintptr_t floor = uintptr_t(machine.mc_rsp);
  int depth = 1;
  while (depth < kDepth && frame >= floor && frame + 16 <= target_stack_top && !(frame & 7)) {
    const uintptr_t* words = reinterpret_cast<const uintptr_t*>(frame);
    if (!words[1]) {
      break;
    }
    sample.pc[depth++] = words[1];
    if (words[0] <= frame) {
      break;
    }
    floor = frame;
    frame = words[0];
  }
  for (; depth < kDepth; ++depth) {
    sample.pc[depth] = 0;
  }
  written.store(index + 1, std::memory_order_release);
}

using Counts = std::unordered_map<uint64_t, uint32_t>;

std::vector<std::pair<uint64_t, uint32_t>> Sorted(const Counts& counts) {
  std::vector<std::pair<uint64_t, uint32_t>> sorted(counts.begin(), counts.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  return sorted;
}

void Report(const char* name, int window, uint64_t samples, const Counts& self,
            const Counts& total) {
  char line[160];
  std::snprintf(line, sizeof(line),
                "sampler: %s window %d, %llu samples (%llu dropped), anchor %#llx", name, window,
                static_cast<unsigned long long>(samples),
                static_cast<unsigned long long>(dropped.load()),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(&StartSamplingCurrentThread)));
  prospero_startup_log(line);
  if (!samples) {
    return;
  }
  const auto top = [&](const Counts& counts, const char* kind, size_t limit) {
    const auto sorted = Sorted(counts);
    for (size_t i = 0; i < sorted.size() && i < limit; ++i) {
      std::snprintf(line, sizeof(line), "sampler:   %s %.1f%% %#llx", kind,
                    100.0 * sorted[i].second / double(samples),
                    static_cast<unsigned long long>(sorted[i].first));
      prospero_startup_log(line);
    }
  };
  top(self, "self", 25);
  top(total, "total", 25);
  // Everything, for the symbols: logs/sampler.txt in the title's state folder
  const std::string path = std::string(prospero_state_root()) + "/logs/sampler.txt";
  if (std::FILE* file = std::fopen(path.c_str(), "w")) {
    std::fprintf(file, "%s window %d samples %llu anchor %#llx\n", name, window,
                 static_cast<unsigned long long>(samples),
                 static_cast<unsigned long long>(
                     reinterpret_cast<uintptr_t>(&StartSamplingCurrentThread)));
    for (const auto& [block, count] : Sorted(self)) {
      std::fprintf(file, "self %u %#llx\n", count, static_cast<unsigned long long>(block));
    }
    for (const auto& [block, count] : Sorted(total)) {
      std::fprintf(file, "total %u %#llx\n", count, static_cast<unsigned long long>(block));
    }
    std::fclose(file);
  }
}

void SamplerMain(pthread_t target, std::string name) {
  rex::thread::Ps5PlaceCurrentThread(rex::thread::Ps5CpuRole::kHost);
  Counts self;
  Counts total;
  uint64_t samples = 0;
  int window = 1;
  auto window_end = std::chrono::steady_clock::now() + kWindow;
  const timespec period = {0, 1000 * 1000};
  for (;;) {
    for (int i = 0; i < 100; ++i) {
      if (pthread_kill(target, kSignal) != 0) {
        prospero_startup_log("sampler: the thread is gone");
        return;
      }
      nanosleep(&period, nullptr);
    }
    const uint64_t end = written.load(std::memory_order_acquire);
    for (uint64_t index = consumed.load(std::memory_order_relaxed); index < end; ++index) {
      const Sample& sample = ring[index % kRingSize];
      ++samples;
      ++self[sample.pc[0] / kBlock * kBlock];
      uint64_t seen[kDepth];
      int seen_count = 0;
      for (int depth = 0; depth < kDepth && sample.pc[depth]; ++depth) {
        const uint64_t block = sample.pc[depth] / kBlock * kBlock;
        if (std::find(seen, seen + seen_count, block) == seen + seen_count) {
          seen[seen_count++] = block;
          ++total[block];
        }
      }
    }
    consumed.store(end, std::memory_order_release);
    if (std::chrono::steady_clock::now() >= window_end) {
      Report(name.c_str(), window++, samples, self, total);
      self.clear();
      total.clear();
      samples = 0;
      window_end = std::chrono::steady_clock::now() + kWindow;
    }
  }
}

}  // namespace

void StartSamplingCurrentThread(const char* name, uintptr_t stack_top) {
  if (!REXCVAR_GET(ps5_sampler)) {
    return;
  }
  static std::once_flag started;
  std::call_once(started, [&] {
    target_stack_top = stack_top;
    struct sigaction action = {};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(kSignal, &action, nullptr) != 0) {
      prospero_startup_log("sampler: no signal handler");
      return;
    }
    std::thread(SamplerMain, pthread_self(), std::string(name)).detach();
  });
}

}  // namespace rex::ps5

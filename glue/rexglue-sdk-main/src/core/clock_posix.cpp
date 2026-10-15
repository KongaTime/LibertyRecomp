/**
 * @file        core/clock_posix.cpp
 * @brief       POSIX platform clock implementations
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/assert.h>
#include <rex/chrono/clock.h>
#include <rex/platform.h>

static_assert(REX_PLATFORM_LINUX || REX_PLATFORM_MAC || REX_PLATFORM_PS5, "This file is POSIX-only");

#include <sys/time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#if REX_PLATFORM_PS5 && !defined(CLOCK_MONOTONIC_RAW)
// FreeBSD (the PS5) has no CLOCK_MONOTONIC_RAW; CLOCK_MONOTONIC_PRECISE is its
// full-resolution monotonic clock.
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC_PRECISE
#endif

#if REX_PLATFORM_PS5
#include <time.h>

#include <ps5platform/kernel.h>
#endif

namespace rex::chrono {

#if REX_PLATFORM_PS5
namespace {
// The host ticks are the CPU's TSC, not clock_gettime: on the console that is a
// system call of about 1 us (R-comp's include/rcomp/fast_clock.h), and the game
// reads its timebase (mftb, under the guest clock's lock) thousands of times a
// frame. The TSC's rate, measured once against CLOCK_MONOTONIC; the kernel's
// figure when it agrees, as it is exact.
uint64_t Ps5TscFrequency() {
  static const uint64_t frequency = [] {
    const auto monotonic_ns = [] {
      timespec tp;
      clock_gettime(CLOCK_MONOTONIC, &tp);
      return uint64_t(tp.tv_sec) * 1000000000ull + uint64_t(tp.tv_nsec);
    };
    const uint64_t ns_begin = monotonic_ns();
    const uint64_t tsc_begin = __builtin_ia32_rdtsc();
    const timespec delay = {0, 20 * 1000 * 1000};
    nanosleep(&delay, nullptr);
    const uint64_t ns_end = monotonic_ns();
    const uint64_t tsc_end = __builtin_ia32_rdtsc();
    const double measured = double(tsc_end - tsc_begin) * 1e9 / double(ns_end - ns_begin);
    const double reported = double(sceKernelGetTscFrequency());
    const double hz = reported > measured * 0.99 && reported < measured * 1.01 ? reported : measured;
    // To the kHz (finer than the measurement): the guest clock's ratio to the
    // 50 MHz timebase then reduces to small terms, which its tick deltas are
    // multiplied by (clock.cpp, UpdateGuestClock)
    return (uint64_t(hz) + 500) / 1000 * 1000;
  }();
  return frequency;
}
}  // namespace
#endif  // REX_PLATFORM_PS5

uint64_t Clock::host_tick_frequency_platform() {
#ifdef __APPLE__
  mach_timebase_info_data_t info;
  mach_timebase_info(&info);
  return (uint64_t)((1000000000ull * (uint64_t)info.denom) / (uint64_t)info.numer);
#elif REX_PLATFORM_PS5
  // The TSC's rate (host_tick_count_platform), not what the console's
  // clock_getres reports: a figure other than the ticks' own would make every
  // guest clock and timer run that many times too fast or slow (as the Simpsons
  // port found with nanosecond ticks)
  return Ps5TscFrequency();
#else
  timespec res;
  int error = clock_getres(CLOCK_MONOTONIC_RAW, &res);
  assert_zero(error);
  assert_zero(res.tv_sec);  // Sub second resolution is required.

  // Convert nano seconds to hertz. Resolution is 1ns on most systems.
  return 1000000000ull / res.tv_nsec;
#endif
}

uint64_t Clock::host_tick_count_platform() {
#ifdef __APPLE__
  // mach_absolute_time() returns raw hardware ticks; host_tick_frequency_platform()
  // expresses the rate of these same ticks via mach_timebase_info.
  // clock_gettime(CLOCK_MONOTONIC_RAW) returns nanoseconds, which would be
  // inconsistent with the 24 MHz mach frequency and cause ~41x time acceleration.
  return mach_absolute_time();
#elif REX_PLATFORM_PS5
  return __builtin_ia32_rdtsc();
#else
  timespec tp;
  int error = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
  assert_zero(error);

  return tp.tv_nsec + tp.tv_sec * 1000000000ull;
#endif
}

uint64_t Clock::QueryHostSystemTime() {
  // https://docs.microsoft.com/en-us/windows/win32/sysinfo/converting-a-time-t-value-to-a-file-time
  constexpr uint64_t seconds_per_day = 3600 * 24;
  // Don't forget the 89 leap days.
  constexpr uint64_t seconds_1601_to_1970 = ((369 * 365 + 89) * seconds_per_day);

  timeval now;
  int error = gettimeofday(&now, nullptr);
  assert_zero(error);

  // NT systems use 100ns intervals.
  return static_cast<uint64_t>(
      (static_cast<int64_t>(now.tv_sec) + seconds_1601_to_1970) * 10000000ull + now.tv_usec * 10);
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}

}  // namespace rex::chrono

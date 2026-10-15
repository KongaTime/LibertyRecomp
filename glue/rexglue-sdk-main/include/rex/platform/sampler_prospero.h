/**
 * @file        rex/platform/sampler_prospero.h
 * @brief       PS5: a sampling profiler of one thread, for the results file
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstdint>

namespace rex::ps5 {

// Samples the calling thread about a thousand times a second: the code
// address it is at and its callers' (its frame-pointer chain, below
// stack_top: the caller's __builtin_frame_address(0)). Every 30 seconds the
// results file gets the addresses most samples were at (self) and under
// (total), and logs/sampler.txt all of them; with the anchor line, a build's
// symbols name them. Once per process; off with ps5_sampler = false.
void StartSamplingCurrentThread(const char* name, uintptr_t stack_top);

}  // namespace rex::ps5

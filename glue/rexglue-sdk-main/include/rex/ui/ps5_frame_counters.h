// The PS5's frame counters: what the title's results file reports every five
// seconds (ReXApp's reporter on the PS5), so a slow run says where frames stop.
#pragma once

#include <atomic>
#include <cstdint>

namespace rex::ps5 {

// Frames the game finished (VdSwap)
inline std::atomic<uint64_t> game_frames{0};
// Frames the renderer submitted to the GPU
inline std::atomic<uint64_t> renderer_frames{0};
// Frames the presenter put on the screen
inline std::atomic<uint64_t> screen_frames{0};

}  // namespace rex::ps5

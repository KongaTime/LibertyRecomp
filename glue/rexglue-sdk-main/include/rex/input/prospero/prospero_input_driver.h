// The PS5 controller driver: the DualSense through scePad, for this SDK's
// player-slot InputDriver (KongaTime/rexglue-sdk has it for v0.10.0's
// device-based one; the pad handling is the same).
//
// Its pad handling (the user service, opening each logged-in user's pad or
// reusing a handle the title already holds, reading state, the compatible
// vibration mode) is TheSimpsonsGame-PS5-Builder's (LoZazaMastro and
// contributors), tools/rexglue-sdk/src/input/prospero/prospero_input_driver.cpp
// at 59e7086, BSD 3-Clause License (see LICENSE), without its trace
// checkpoints and frame-generation shortcut. The device model and keystrokes
// follow v0.10.0's SDL driver (src/input/sdl/sdl_input_driver.cpp).
#pragma once

#include <vector>

#include <rex/input/input_driver.h>
#include <rex/input/prospero/pad_abi.h>

namespace rex::input::prospero {

// Controller state for the full-screen UI (the window's settings shortcut),
// for player slot user_index (0 is the initial user), whatever the overlays.
X_RESULT PollPadState(uint32_t user_index, X_INPUT_STATE* out_state);

class ProsperoInputDriver final : public InputDriver {
 public:
  explicit ProsperoInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~ProsperoInputDriver() override;

  X_STATUS Setup() override;
  const char* trace_name() const override { return "prospero"; }
  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;
};

}  // namespace rex::input::prospero

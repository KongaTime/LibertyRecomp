#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include <rex/ui/imgui_dialog.h>

#include "gta4_installer.h"

namespace gta4::install {

// The console's install: no file pickers, the sources are where the title's
// folder keeps them. The disc's files, extracted (extract-xiso), or the disc
// image itself, and the v8 title update (the Xbox 360's TU package or a raw
// default.xexp), with the episodes optional.
struct ConsoleSources {
  std::filesystem::path game;
  std::filesystem::path update;
  std::vector<DlcSelection> episodes;
};

// Looks in title_root: gamedata/ (an extracted disc) or the first *.iso in it,
// title-update/ (default.xexp, or the one file there), episodes/TLAD and
// episodes/TBOGT. Sets error to what is missing when there is no game or update.
std::optional<ConsoleSources> FindConsoleSources(const std::filesystem::path& title_root,
                                                 std::string& error);

// Installs into install_root (its game/ and dlc/) on a worker thread and shows
// its progress. complete runs on the UI thread once the install succeeded; a
// failure stays on screen with its reason.
class ConsoleInstallDialog final : public rex::ui::ImGuiDialog {
 public:
  ConsoleInstallDialog(rex::ui::ImGuiDrawer* drawer, ConsoleSources sources,
                       std::filesystem::path install_root, std::function<void()> complete);
  ~ConsoleInstallDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::filesystem::path install_root_;
  std::function<void()> complete_;
  Progress progress_;
  Result result_;
  std::thread worker_;
  std::atomic<bool> done_{false};
  uint64_t last_logged_percent_ = ~uint64_t(0);
};

}  // namespace gta4::install

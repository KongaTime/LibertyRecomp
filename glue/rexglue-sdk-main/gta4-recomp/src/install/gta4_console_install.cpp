#include "gta4_console_install.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>
#include <vector>

#include <imgui.h>

#include <rex/logging.h>

namespace gta4::install {
namespace {

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// The regular files directly in a folder, by name, but for the title's own
// README.txt (ps5/gamedata-note.txt)
std::vector<std::filesystem::path> FilesIn(const std::filesystem::path& folder) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end;
       it.increment(error)) {
    std::error_code type_error;
    if (it->is_regular_file(type_error) &&
        Lowercase(it->path().filename().string()) != "readme.txt") {
      files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace

std::optional<ConsoleSources> FindConsoleSources(const std::filesystem::path& title_root,
                                                 std::string& error) {
  ConsoleSources sources;
  std::error_code fs_error;

  // The game: the disc's files extracted into gamedata/, or a disc image there
  const auto gamedata = title_root / "gamedata";
  if (std::filesystem::is_regular_file(gamedata / "default.xex", fs_error)) {
    sources.game = gamedata;
  } else {
    for (const auto& file : FilesIn(gamedata)) {
      if (Lowercase(file.extension().string()) == ".iso") {
        sources.game = file;
        break;
      }
    }
  }
  if (sources.game.empty()) {
    error = "No GTA IV disc in " + gamedata.string() +
            ": copy the files extract-xiso extracted from your disc there (default.xex, "
            "common.rpf, xbox360.rpf, audio.rpf and the rest), or the disc image (.iso)";
    return std::nullopt;
  }

  // The v8 title update: a raw default.xexp, or the one file there (the TU package)
  const auto update_folder = title_root / "title-update";
  const auto updates = FilesIn(update_folder);
  for (const auto& file : updates) {
    if (Lowercase(file.filename().string()) == "default.xexp") {
      sources.update = file;
    }
  }
  if (sources.update.empty() && updates.size() == 1) {
    sources.update = updates.front();
  }
  if (sources.update.empty()) {
    error = updates.empty()
                ? "No GTA IV title update in " + update_folder.string() +
                      ": copy the v8 title update there (the TU file from your Xbox 360, or "
                      "its default.xexp)"
                : "More than one file in " + update_folder.string() +
                      ": leave only the v8 title update there";
    return std::nullopt;
  }

  // The episodes, when they are there
  for (Episode episode : {Episode::kTlad, Episode::kTbogt}) {
    const auto folder = title_root / "episodes" / EpisodeDirectory(episode);
    if (std::filesystem::is_directory(folder, fs_error)) {
      sources.episodes.push_back({episode, folder});
    }
  }
  return sources;
}

ConsoleInstallDialog::ConsoleInstallDialog(rex::ui::ImGuiDrawer* drawer, ConsoleSources sources,
                                           std::filesystem::path install_root,
                                           std::function<void()> complete)
    : rex::ui::ImGuiDialog(drawer),
      install_root_(std::move(install_root)),
      complete_(std::move(complete)) {
  REXLOG_INFO("GTA IV console install: {} with the update {}, {} episode(s), into {}",
              sources.game.string(), sources.update.string(), sources.episodes.size(),
              install_root_.string());
  Selection selection;
  selection.game_source = std::move(sources.game);
  selection.update_source = std::move(sources.update);
  selection.dlc_sources = std::move(sources.episodes);
  worker_ = std::thread([this, selection = std::move(selection)] {
    result_ = Install(selection, install_root_, progress_);
    done_.store(true, std::memory_order_release);
  });
}

ConsoleInstallDialog::~ConsoleInstallDialog() {
  if (worker_.joinable()) {
    progress_.cancel_requested.store(true);
    worker_.join();
  }
}

void ConsoleInstallDialog::OnDraw(ImGuiIO& io) {
  const bool done = done_.load(std::memory_order_acquire);
  if (done && worker_.joinable()) {
    worker_.join();
    if (result_.success) {
      REXLOG_INFO("GTA IV console install: done");
      // The installed game sits beside the sources it came from, and its name
      // looks like theirs: say which one the title plays
      if (std::FILE* note = std::fopen((install_root_ / "game" / "README.txt").c_str(), "w")) {
        std::fputs(
            "This folder is the installed game: Liberty Recompiled plays from it.\n"
            "Do not delete it. Deleting it means installing again.\n\n"
            "gamedata/ and title-update/ beside it are what it was installed from:\n"
            "they can be emptied once the game starts.\n",
            note);
        std::fclose(note);
      }
      auto complete = std::move(complete_);
      Close();
      if (complete) {
        complete();
      }
      return;
    }
    REXLOG_ERROR("GTA IV console install failed: {}", result_.error);
  }

  const uint64_t total = progress_.total_bytes.load();
  const uint64_t copied = progress_.copied_bytes.load();
  const float fraction = total ? std::min(1.0f, float(double(copied) / double(total))) : 0.0f;
  const uint64_t percent = uint64_t(fraction * 100.0f);
  if (!done && percent / 5 != last_logged_percent_ / 5) {
    last_logged_percent_ = percent;
    REXLOG_INFO("GTA IV console install: {}% ({} of {} MB)", percent, copied >> 20, total >> 20);
  }

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.15f, io.DisplaySize.y * 0.30f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.70f, io.DisplaySize.y * 0.40f),
                           ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.98f);
  constexpr ImGuiWindowFlags kWindowFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (!ImGui::Begin("Liberty Recompiled Setup##console-installer", nullptr, kWindowFlags)) {
    ImGui::End();
    return;
  }
  ImGui::SetWindowFontScale(1.45f);
  ImGui::TextUnformatted("INSTALLING GTA IV");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::Separator();
  ImGui::Spacing();
  if (done) {
    ImGui::TextWrapped("The install failed: %s", result_.error.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Fix the files in the title's folder, then close the title and launch it "
                       "again. logs/ in the title's folder has the details.");
  } else {
    ImGui::TextWrapped("Unpacking your copy of the game into the title's folder. This happens "
                       "once and takes several minutes.");
    ImGui::Spacing();
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
    ImGui::Text("%llu of %llu MB", static_cast<unsigned long long>(copied >> 20),
                static_cast<unsigned long long>(total >> 20));
  }
  ImGui::End();
}

}  // namespace gta4::install

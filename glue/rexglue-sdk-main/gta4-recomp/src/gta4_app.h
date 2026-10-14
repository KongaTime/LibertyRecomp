#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/system/achievement_manager.h>

#include "gta4_init.h"

namespace rex::system::xam {
class IAchievementService;
class IEntitlementService;
class ITitleProfileService;
}

namespace gta4::input {
class TextChatDialog;
class ContextTouchOverlay;
class UserMusicPlayer;
}

#ifndef GTA4_RECOMP_ASSET_XEX
#error "GTA4_RECOMP_ASSET_XEX must point to the preserved GTA IV XEX"
#endif

class GTA4App final : public rex::ReXApp {
 public:
  ~GTA4App() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<GTA4App>(new GTA4App(context));
  }

 private:
  explicit GTA4App(rex::ui::WindowedAppContext& context);

  void OnPreSetup(rex::RuntimeConfig& config) override;

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override;

  void OnPostSetup() override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  bool RequiresSynchronizedInitialThreadResume() const override;
  void OnShutdown() override;
  bool OnWindowCloseRequested() override;
  void QueueAchievementUpload(uint32_t achievement_id);
  void AchievementSyncWorkerMain();
  void TitleProfileSyncWorkerMain();

  struct AchievementSyncState;
  struct TitleProfileSyncState;

  void OnConfigurePaths(rex::PathConfig& paths) override {
    std::error_code error;
    const auto default_user_root = rex::filesystem::GetUserFolder() / std::string(GetName());
    const auto liberty_root = rex::filesystem::GetUserFolder() / "LibertyRecomp";
    liberty_root_ = liberty_root;
    const bool uses_default_user_root = paths.user_data_root == default_user_root;
    const bool uses_default_cache_root = paths.cache_root == default_user_root / "cache";

    if (paths.game_data_root.empty()) {
#if REX_PLATFORM_PS5
      // The title folder's gamedata/: the PC install's LibertyRecomp/game
      // folder, copied there by the player (ps5/README.md)
      paths.game_data_root = kPs5GameDataRoot;
#else
      const auto installed_game_root = liberty_root / "game";
      paths.game_data_root = installed_game_root;
#endif
    }

    if (uses_default_user_root) {
      paths.user_data_root = liberty_root / "saves";
    }
    if (paths.saved_game_root.empty()) {
      paths.saved_game_root = liberty_root / "saves";
    }
    if (uses_default_cache_root) {
      paths.cache_root = liberty_root / "shader_cache";
    }
    native_config_path_ = liberty_root / "native.toml";
    paths.config_path = native_config_path_;
#if REX_PLATFORM_PS5
    paths.marketplace_content_root = kPs5GameDataRoot / "dlc";
#else
    paths.marketplace_content_root = liberty_root / "dlc";
#endif

    if (paths.update_data_root.empty() && !paths.game_data_root.empty()) {
      error.clear();
      const auto installed_update_root = paths.game_data_root / "update";
      if (std::filesystem::is_directory(installed_update_root, error)) {
        paths.update_data_root = installed_update_root;
      }
    }
  }

  rex::system::AchievementListenerHandle achievement_listener_ = 0;
  rex::system::xam::IAchievementService* achievement_service_ = nullptr;
  rex::system::xam::IEntitlementService* entitlement_service_ = nullptr;
  std::shared_ptr<AchievementSyncState> achievement_sync_state_;
  std::thread achievement_sync_worker_;
  rex::system::xam::ITitleProfileService* title_profile_service_ = nullptr;
  std::shared_ptr<TitleProfileSyncState> title_profile_sync_state_;
  std::thread title_profile_sync_worker_;
#if REX_PLATFORM_PS5
  // The game's files, read-only beside eboot.bin (/app0 is the title folder)
  static inline const std::filesystem::path kPs5GameDataRoot{"/app0/gamedata"};
#endif
  std::filesystem::path liberty_root_;
  std::filesystem::path native_config_path_;
  std::unique_ptr<gta4::input::UserMusicPlayer> user_music_player_;
  std::unique_ptr<gta4::input::TextChatDialog> text_chat_dialog_;
  std::unique_ptr<gta4::input::ContextTouchOverlay> context_touch_overlay_;
};

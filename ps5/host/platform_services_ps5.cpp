/*
 * Liberty Recompiled on the PS5 - the desktop services the console build goes
 * without, in place of the files that implement them on the PC and Mac:
 *
 *   achievement_bridge_gc.mm         Game Center achievements: none
 *   input/user_music_player.mm       User Music (AVFoundation): no tracks
 *   network/gta4_microphone_permission.mm
 *                                    the microphone: not available
 *   network/gta4_voice_audio.cpp     voice chat audio (SDL): no device
 *   network/community_multiplayer.cpp
 *                                    the community backend (curl, OpenSSL):
 *                                    offline, which empty services mean
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <filesystem>
#include <functional>
#include <memory>

#include "achievement_bridge_gc.h"
#include "input/user_music_player.h"
#include "network/community_multiplayer.h"
#include "network/gta4_microphone_permission.h"
#include "network/gta4_voice_audio.h"

namespace gta4::game_center {
void Initialize() {}
void SubmitAchievement(uint32_t) {}
}  // namespace gta4::game_center

namespace gta4::input {
struct UserMusicPlayer::Impl {};
UserMusicPlayer::UserMusicPlayer(std::filesystem::path) {}
UserMusicPlayer::~UserMusicPlayer() = default;
bool UserMusicPlayer::Next() { return false; }
bool UserMusicPlayer::Previous() { return false; }
bool UserMusicPlayer::Stop() { return false; }
bool UserMusicPlayer::HasTracks() const { return false; }
void PublishUserMusicPlayer(UserMusicPlayer*) {}
bool IsUserMusicAvailable() { return false; }
bool RequestUserMusicNext() { return false; }
bool RequestUserMusicPrevious() { return false; }
bool RequestUserMusicStop() { return false; }
}  // namespace gta4::input

namespace gta4::voice {
MicrophonePermissionStatus GetMicrophonePermissionStatus() noexcept {
  return MicrophonePermissionStatus::kRestricted;
}
void RequestMicrophonePermission(std::function<void()> completion) {
  if (completion) {
    completion();
  }
}
std::shared_ptr<rex::system::xam::IVoiceAudioDevice> CreateAudioDevice() { return nullptr; }
std::shared_ptr<rex::system::xam::IVoiceSampleCodec> CreateSampleCodec() { return nullptr; }
}  // namespace gta4::voice

namespace LibertyRecomp::Network {
rex::system::xam::LiveBackendServices CreateCommunityMultiplayerBackend(
    const rex::system::xam::LiveConfig&, const rex::system::xam::LiveIdentity&) {
  return {};
}
}  // namespace LibertyRecomp::Network

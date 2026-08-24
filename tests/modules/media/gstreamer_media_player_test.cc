#include <openssl/evp.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "cockpit/modules/media/media_player.h"

namespace {

void AppendLe16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value));
  bytes->push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendLe32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes->push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::vector<std::uint8_t> MakeWav() {
  constexpr std::uint32_t kSampleRate = 16000;
  constexpr std::uint32_t kSamples = 32000;
  constexpr std::uint32_t kDataBytes = kSamples * 2U;
  std::vector<std::uint8_t> bytes;
  bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
  AppendLe32(&bytes, 36U + kDataBytes);
  bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  AppendLe32(&bytes, 16);
  AppendLe16(&bytes, 1);
  AppendLe16(&bytes, 1);
  AppendLe32(&bytes, kSampleRate);
  AppendLe32(&bytes, kSampleRate * 2U);
  AppendLe16(&bytes, 2);
  AppendLe16(&bytes, 16);
  bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
  AppendLe32(&bytes, kDataBytes);
  bytes.resize(bytes.size() + kDataBytes, 0);
  return bytes;
}

std::string Sha256(const std::vector<std::uint8_t>& bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
  EVP_DigestUpdate(context, bytes.data(), bytes.size());
  EVP_DigestFinal_ex(context, digest.data(), &length);
  EVP_MD_CTX_free(context);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < length; ++i) {
    output << std::setw(2) << static_cast<unsigned int>(digest[i]);
  }
  return output.str();
}

}  // namespace

int main() {
  const char* configured_sink = std::getenv("COCKPIT_MEDIA_TEST_SINK");
  const std::string sink = configured_sink == nullptr ? "fakesink" : configured_sink;
  const char* configured_manifest = std::getenv("COCKPIT_MEDIA_TEST_MANIFEST");
  if (configured_manifest != nullptr && *configured_manifest != '\0') {
    auto player = cockpit::media::CreateGstreamerMediaPlayer(configured_manifest, sink);
    if (player == nullptr) {
      std::cerr << "GStreamer external media manifest did not initialize\n";
      return 1;
    }
    std::string error;
    if (!player->Play("default_track", &error) ||
        player->status().state != cockpit::media::MediaPlaybackState::kPlaying) {
      std::cerr << "GStreamer external media play failed: " << error << '\n';
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!player->Pause(&error) || !player->Resume(&error) || !player->Stop(&error)) {
      std::cerr << "GStreamer external media lifecycle failed: " << error << '\n';
      return 1;
    }
    return 0;
  }
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("cockpit-gstreamer-media-" + std::to_string(getpid()));
  std::error_code error_code;
  std::filesystem::remove_all(root, error_code);
  std::filesystem::create_directories(root, error_code);
  const auto audio = root / "fixture.wav";
  const std::vector<std::uint8_t> wav = MakeWav();
  {
    std::ofstream output(audio, std::ios::binary);
    output.write(reinterpret_cast<const char*>(wav.data()),
                 static_cast<std::streamsize>(wav.size()));
  }
  {
    std::ofstream manifest(root / "manifest.yaml");
    manifest << "tracks:\n"
             << "  - id: fixture_track\n"
             << "    title: Fixture\n"
             << "    artist: Cockpit\n"
             << "    duration_ms: 2000\n"
             << "    path: fixture.wav\n"
             << "    sha256: " << Sha256(wav) << '\n';
  }
  auto player = cockpit::media::CreateGstreamerMediaPlayer((root / "manifest.yaml").string(), sink);
  if (player == nullptr) {
    std::cerr << "GStreamer media backend did not initialize\n";
    std::filesystem::remove_all(root, error_code);
    return 1;
  }
  std::string error;
  const bool played = player->Play("default_track", &error);
  const auto playing_status = player->status().state;
  const auto position_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (player->status().position_ms == 0U &&
         std::chrono::steady_clock::now() < position_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const bool paused = player->Pause(&error);
  const auto paused_snapshot = player->status();
  const bool resumed = player->Resume(&error);
  const auto resumed_snapshot = player->status();
  if (!played || playing_status != cockpit::media::MediaPlaybackState::kPlaying || !paused ||
      paused_snapshot.state != cockpit::media::MediaPlaybackState::kPaused ||
      paused_snapshot.position_ms == 0U || !resumed ||
      resumed_snapshot.state != cockpit::media::MediaPlaybackState::kPlaying ||
      resumed_snapshot.position_ms < paused_snapshot.position_ms) {
    std::cerr << "GStreamer media lifecycle failed: " << error << " played=" << played
              << " playing_status=" << static_cast<int>(playing_status) << " paused=" << paused
              << " paused_status=" << static_cast<int>(paused_snapshot.state)
              << " paused_position=" << paused_snapshot.position_ms << " resumed=" << resumed
              << " resumed_status=" << static_cast<int>(resumed_snapshot.state)
              << " resumed_position=" << resumed_snapshot.position_ms << '\n';
    std::filesystem::remove_all(root, error_code);
    return 1;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline &&
         player->status().state == cockpit::media::MediaPlaybackState::kPlaying) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (player->status().state != cockpit::media::MediaPlaybackState::kStopped ||
      player->status().position_ms != 2000) {
    std::cerr << "GStreamer media EOS was not reflected in status\n";
    std::filesystem::remove_all(root, error_code);
    return 1;
  }

  const auto invalid_audio = root / "invalid.wav";
  const std::vector<std::uint8_t> invalid_bytes{'n', 'o', 't', '-', 'a', '-', 'w', 'a', 'v'};
  {
    std::ofstream output(invalid_audio, std::ios::binary);
    output.write(reinterpret_cast<const char*>(invalid_bytes.data()),
                 static_cast<std::streamsize>(invalid_bytes.size()));
  }
  {
    std::ofstream manifest(root / "invalid-manifest.yaml");
    manifest << "tracks:\n"
             << "  - id: invalid_track\n"
             << "    title: Invalid\n"
             << "    artist: Cockpit\n"
             << "    duration_ms: 1000\n"
             << "    path: invalid.wav\n"
             << "    sha256: " << Sha256(invalid_bytes) << '\n';
  }
  auto invalid_player =
      cockpit::media::CreateGstreamerMediaPlayer((root / "invalid-manifest.yaml").string(), sink);
  const bool invalid_played =
      invalid_player != nullptr && invalid_player->Play("invalid_track", &error);
  if (invalid_player == nullptr ||
      (invalid_played &&
       invalid_player->status().state != cockpit::media::MediaPlaybackState::kPlaying &&
       invalid_player->status().state != cockpit::media::MediaPlaybackState::kFaulted)) {
    std::cerr << "GStreamer invalid-media setup failed: " << error << '\n';
    std::filesystem::remove_all(root, error_code);
    return 1;
  }
  const auto error_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (invalid_played && std::chrono::steady_clock::now() < error_deadline &&
         invalid_player->status().state == cockpit::media::MediaPlaybackState::kPlaying) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (invalid_player->status().state != cockpit::media::MediaPlaybackState::kFaulted ||
      invalid_player->status().last_error.empty()) {
    std::cerr << "GStreamer decode error was not reflected in status\n";
    std::filesystem::remove_all(root, error_code);
    return 1;
  }
  std::filesystem::remove_all(root, error_code);
  return 0;
}

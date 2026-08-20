#include "cockpit/modules/media/media_manifest.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("cockpit-media-manifest-" + std::to_string(getpid()));
  std::error_code error_code;
  std::filesystem::remove_all(root, error_code);
  std::filesystem::create_directories(root, error_code);
  const std::filesystem::path audio = root / "fixture.wav";
  const std::filesystem::path manifest_path = root / "manifest.yaml";
  {
    std::ofstream output(audio, std::ios::binary);
    output << "cockpit media fixture\n";
  }
  {
    std::ofstream output(manifest_path);
    output << "tracks:\n"
           << "  - id: fixture_track\n"
           << "    title: Fixture\n"
           << "    artist: Cockpit\n"
           << "    duration_ms: 1000\n"
           << "    path: fixture.wav\n"
           << "    sha256: f6e7ddd6f888f2f2c7d3bc5a7e661021a4608710b4ccfd98c8957ffaba89aa86\n";
  }
  cockpit::media::MediaManifest manifest;
  std::string error;
  if (!Check(cockpit::media::MediaManifest::Load(manifest_path, &manifest, &error),
             error.c_str()) ||
      !Check(manifest.tracks().size() == 1, "manifest track count mismatch") ||
      !Check(manifest.tracks().front().metadata.id == "fixture_track",
             "manifest track ID mismatch")) {
    std::filesystem::remove_all(root, error_code);
    return 1;
  }

  const auto symlink = root / "fixture-link.wav";
  std::filesystem::create_symlink(audio.filename(), symlink, error_code);
  {
    std::ofstream output(manifest_path);
    output << "tracks:\n"
           << "  - id: fixture_track\n"
           << "    title: Fixture\n"
           << "    artist: Cockpit\n"
           << "    duration_ms: 1000\n"
           << "    path: fixture-link.wav\n"
           << "    sha256: f6e7ddd6f888f2f2c7d3bc5a7e661021a4608710b4ccfd98c8957ffaba89aa86\n";
  }
  if (Check(cockpit::media::MediaManifest::Load(manifest_path, &manifest, &error),
            "manifest accepted a symlink media file")) {
    std::filesystem::remove_all(root, error_code);
    return 1;
  }

  {
    std::ofstream output(manifest_path);
    output << "tracks:\n"
           << "  - id: fixture_track\n"
           << "    title: Fixture\n"
           << "    artist: Cockpit\n"
           << "    duration_ms: 1000\n"
           << "    path: ../fixture.wav\n"
           << "    sha256: f6e7ddd6f888f2f2c7d3bc5a7e661021a4608710b4ccfd98c8957ffaba89aa86\n";
  }
  if (Check(cockpit::media::MediaManifest::Load(manifest_path, &manifest, &error),
            "manifest accepted directory traversal")) {
    std::filesystem::remove_all(root, error_code);
    return 1;
  }

  {
    std::ofstream output(manifest_path);
    output << "tracks:\n"
           << "  - id: fixture_track\n"
           << "    title: Fixture\n"
           << "    artist: Cockpit\n"
           << "    duration_ms: 1000\n"
           << "    path: fixture.wav\n"
           << "    sha256: f6e7ddd6f888f2f2c7d3bc5a7e661021a4608710b4ccfd98c8957ffaba89aa86\n";
  }

  std::ofstream output(audio, std::ios::binary | std::ios::app);
  output << 'x';
  output.close();
  if (Check(cockpit::media::MediaManifest::Load(manifest_path, &manifest, &error),
            "manifest accepted a SHA-256 mismatch")) {
    std::filesystem::remove_all(root, error_code);
    return 1;
  }

  std::filesystem::remove_all(root, error_code);
  return 0;
}

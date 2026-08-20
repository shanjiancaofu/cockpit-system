#include "cockpit/modules/media/media_manifest.h"

#include <openssl/evp.h>
#include <yaml-cpp/yaml.h>

#include <array>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace cockpit {
namespace media {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool ValidateKeys(const YAML::Node& node, std::initializer_list<std::string_view> allowed,
                  const std::string& path, std::string* error) {
  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      SetError(error, path + " keys must be scalars");
      return false;
    }
    const std::string key = entry.first.as<std::string>();
    if (std::find(allowed.begin(), allowed.end(), std::string_view(key)) == allowed.end()) {
      SetError(error, path + "." + key + " is not supported");
      return false;
    }
  }
  return true;
}

bool IsSha256(const std::string& value) {
  if (value.size() != 64) {
    return false;
  }
  return value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

bool IsSafeId(const std::string& value) {
  if (value.empty() || value == "default_track" || value.find('/') != std::string::npos ||
      value.find('\\') != std::string::npos || value.find("..") != std::string::npos) {
    return false;
  }
  return value.find_first_not_of(
             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
         std::string::npos;
}

bool Sha256(const std::filesystem::path& path, std::string* digest, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    SetError(error, "failed to open media file: " + path.string());
    return false;
  }
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(context);
    SetError(error, "failed to initialize SHA-256");
    return false;
  }
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0 &&
        EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1) {
      EVP_MD_CTX_free(context);
      SetError(error, "failed to calculate SHA-256: " + path.string());
      return false;
    }
  }
  if (!input.eof()) {
    EVP_MD_CTX_free(context);
    SetError(error, "failed to read media file: " + path.string());
    return false;
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int length = 0;
  const bool ok = EVP_DigestFinal_ex(context, bytes.data(), &length) == 1;
  EVP_MD_CTX_free(context);
  if (!ok) {
    SetError(error, "failed to finalize SHA-256: " + path.string());
    return false;
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < length; ++i) {
    output << std::setw(2) << static_cast<unsigned int>(bytes[i]);
  }
  *digest = output.str();
  return true;
}

}  // namespace

bool MediaManifest::Load(const std::filesystem::path& manifest_path, MediaManifest* manifest,
                         std::string* error) {
  if (manifest == nullptr) {
    SetError(error, "media manifest output is null");
    return false;
  }
  try {
    if (!std::filesystem::is_regular_file(manifest_path) ||
        std::filesystem::is_symlink(manifest_path)) {
      SetError(error, "media manifest is not a regular non-symlink file");
      return false;
    }
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(manifest_path).parent_path();
    const YAML::Node root_node = YAML::LoadFile(manifest_path.string());
    if (!root_node.IsMap() || !ValidateKeys(root_node, {"tracks"}, "media manifest", error)) {
      return false;
    }
    const YAML::Node tracks = root_node["tracks"];
    if (!tracks || !tracks.IsSequence() || tracks.size() == 0) {
      SetError(error, "media manifest tracks must be a non-empty sequence");
      return false;
    }
    std::vector<MediaManifestTrack> parsed;
    std::unordered_set<std::string> ids;
    for (const auto& node : tracks) {
      if (!node.IsMap()) {
        SetError(error, "media manifest track must be a map");
        return false;
      }
      if (!ValidateKeys(node, {"id", "title", "artist", "duration_ms", "path", "sha256"},
                        "media manifest track", error)) {
        return false;
      }
      const std::string id = node["id"].as<std::string>();
      const std::string title = node["title"].as<std::string>();
      const std::string artist = node["artist"].as<std::string>();
      const std::uint64_t duration_ms = node["duration_ms"].as<std::uint64_t>();
      const std::string relative = node["path"].as<std::string>();
      const std::string expected = node["sha256"].as<std::string>();
      if (!IsSafeId(id) || !ids.insert(id).second) {
        SetError(error, "media manifest contains invalid or duplicate track id");
        return false;
      }
      if (title.empty() || artist.empty() || duration_ms == 0) {
        SetError(error, "media manifest metadata must be non-empty for track " + id);
        return false;
      }
      if (!IsSha256(expected)) {
        SetError(error, "media manifest has invalid SHA-256 for track " + id);
        return false;
      }
      const std::filesystem::path relative_path(relative);
      if (relative_path.empty() || relative_path.is_absolute()) {
        SetError(error, "media manifest path must be relative for track " + id);
        return false;
      }
      for (const auto& component : relative_path) {
        if (component == ".." || component == "." || component.empty()) {
          SetError(error, "media manifest path contains unsafe components for track " + id);
          return false;
        }
      }
      const std::filesystem::path file = root / relative_path;
      std::filesystem::path current = root;
      for (const auto& component : relative_path) {
        current /= component;
        if (std::filesystem::is_symlink(current)) {
          SetError(error, "media manifest path contains a symlink for track " + id);
          return false;
        }
      }
      if (!std::filesystem::is_regular_file(file)) {
        SetError(error, "media file is not a regular non-symlink file for track " + id);
        return false;
      }
      const std::filesystem::path canonical = std::filesystem::weakly_canonical(file);
      const std::filesystem::path relative_canonical = canonical.lexically_relative(root);
      if (relative_canonical.empty() || relative_canonical.is_absolute() ||
          (relative_canonical.begin() != relative_canonical.end() &&
           *relative_canonical.begin() == "..")) {
        SetError(error, "media file escapes manifest directory for track " + id);
        return false;
      }
      std::string actual;
      if (!Sha256(canonical, &actual, error) || actual != expected) {
        SetError(error, "media file SHA-256 mismatch for track " + id);
        return false;
      }
      MediaManifestTrack track;
      track.metadata.id = id;
      track.metadata.title = title;
      track.metadata.artist = artist;
      track.metadata.duration_ms = duration_ms;
      track.file = canonical;
      track.sha256 = actual;
      parsed.push_back(std::move(track));
    }
    manifest->tracks_ = std::move(parsed);
    return true;
  } catch (const YAML::Exception& exception) {
    SetError(error, "invalid media manifest: " + std::string(exception.what()));
    return false;
  } catch (const std::filesystem::filesystem_error& exception) {
    SetError(error, "invalid media manifest file: " + std::string(exception.what()));
    return false;
  }
}

}  // namespace media
}  // namespace cockpit

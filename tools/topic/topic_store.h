#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cockpit {
namespace config {
class SystemConfig;
}  // namespace config
namespace topic {

using Registry = std::map<std::string, std::pair<std::int64_t, std::string>>;

class TopicStore {
 public:
  explicit TopicStore(const cockpit::config::SystemConfig& config);

  const std::filesystem::path& topic_dir() const {
    return topic_dir_;
  }
  std::filesystem::path TopicFile(const std::string& topic) const;
  Registry LoadRegistry() const;
  void SaveRegistry(const Registry& registry) const;
  std::size_t CountLines(const std::filesystem::path& path) const;
  std::vector<std::string> ReadLastLines(const std::filesystem::path& path, int tail) const;

 private:
  static std::string SanitizeTopicFileName(const std::string& topic);

  std::filesystem::path RegistryFile() const;

  std::filesystem::path topic_dir_;
};

}  // namespace topic
}  // namespace cockpit

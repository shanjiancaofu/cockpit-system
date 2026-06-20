#include "topic_store.h"

#include "core/config/system_config.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace cockpit {
namespace topic {

TopicStore::TopicStore(const cockpit::config::SystemConfig& config)
    : topic_dir_(config.tools().topic.dir) {}

std::string TopicStore::SanitizeTopicFileName(const std::string& topic) {
  std::string result;
  for (char ch : topic) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0 || ch == '-' || ch == '_') {
      result.push_back(ch);
    } else if (ch == '/') {
      result += "__";
    } else {
      result.push_back('_');
    }
  }
  if (result.empty() || result == "__") {
    return "root";
  }
  while (!result.empty() && result.front() == '_') {
    result.erase(result.begin());
  }
  return result.empty() ? "root" : result;
}

std::filesystem::path TopicStore::RegistryFile() const {
  return topic_dir_ / ".registry.tsv";
}

std::filesystem::path TopicStore::TopicFile(const std::string& topic) const {
  return topic_dir_ / (SanitizeTopicFileName(topic) + ".jsonl");
}

Registry TopicStore::LoadRegistry() const {
  Registry registry;
  std::ifstream input(RegistryFile());
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream parts(line);
    std::string topic;
    std::string timestamp;
    std::string file;
    if (std::getline(parts, topic, '\t') && std::getline(parts, timestamp, '\t') &&
        std::getline(parts, file, '\t')) {
      try {
        registry[topic] = {std::stoll(timestamp), file};
      } catch (...) {
      }
    }
  }
  return registry;
}

void TopicStore::SaveRegistry(const Registry& registry) const {
  std::filesystem::create_directories(topic_dir_);
  const auto target = RegistryFile();
  const auto tmp = topic_dir_ / ".registry.tsv.tmp";
  {
    std::ofstream output(tmp, std::ios::trunc);
    for (const auto& item : registry) {
      output << item.first << '\t' << item.second.first << '\t' << item.second.second << '\n';
    }
  }
  std::filesystem::rename(tmp, target);
}

std::size_t TopicStore::CountLines(const std::filesystem::path& path) const {
  std::ifstream input(path);
  std::size_t count = 0;
  std::string line;
  while (std::getline(input, line)) {
    ++count;
  }
  return count;
}

std::vector<std::string> TopicStore::ReadLastLines(const std::filesystem::path& path,
                                                   int tail) const {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
    if (tail > 0 && static_cast<int>(lines.size()) > tail) {
      lines.erase(lines.begin());
    }
  }
  return lines;
}

}  // namespace topic
}  // namespace cockpit

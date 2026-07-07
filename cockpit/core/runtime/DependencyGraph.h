#pragma once

#include <string>
#include <vector>

namespace cockpit {
namespace runtime {

enum class DependencyStrength {
  kRequired,
  kOptional,
};

struct ServiceDependency {
  std::string service;
  std::string depends_on;
  DependencyStrength strength = DependencyStrength::kRequired;
};

class DependencyGraph {
 public:
  DependencyGraph() = default;

  void AddRequired(std::string service, std::string depends_on);
  void AddOptional(std::string service, std::string depends_on);

  std::vector<ServiceDependency> DependenciesOf(const std::string& service) const;
  std::vector<std::string> RequiredDependenciesOf(const std::string& service) const;
  std::vector<std::string> OptionalDependenciesOf(const std::string& service) const;
  std::vector<ServiceDependency> dependencies() const;
  bool HasCycle(std::string* cycle) const;

 private:
  void Add(std::string service, std::string depends_on, DependencyStrength strength);

  std::vector<ServiceDependency> dependencies_;
};

}  // namespace runtime
}  // namespace cockpit

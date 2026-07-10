#include "cockpit/core/runtime/DependencyGraph.h"

#include <algorithm>
#include <set>
#include <utility>

namespace cockpit {
namespace runtime {
namespace {

bool Contains(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

// The graph depth is bounded by the number of configured services.
// NOLINTNEXTLINE(misc-no-recursion)
bool VisitRequired(const std::string& service, const std::vector<ServiceDependency>& dependencies,
                   std::vector<std::string>* visiting, std::set<std::string>* visited,
                   std::string* cycle) {
  if (Contains(*visiting, service)) {
    if (cycle != nullptr) {
      *cycle = service;
    }
    return true;
  }
  if (visited->find(service) != visited->end()) {
    return false;
  }

  visiting->push_back(service);
  for (const auto& dependency : dependencies) {
    if (dependency.service != service || dependency.strength != DependencyStrength::kRequired) {
      continue;
    }
    if (VisitRequired(dependency.depends_on, dependencies, visiting, visited, cycle)) {
      if (cycle != nullptr && *cycle != service) {
        *cycle = service + " -> " + *cycle;
      }
      return true;
    }
  }
  visiting->pop_back();
  visited->insert(service);
  return false;
}

}  // namespace

void DependencyGraph::AddRequired(std::string service, std::string depends_on) {
  Add(std::move(service), std::move(depends_on), DependencyStrength::kRequired);
}

void DependencyGraph::AddOptional(std::string service, std::string depends_on) {
  Add(std::move(service), std::move(depends_on), DependencyStrength::kOptional);
}

std::vector<ServiceDependency> DependencyGraph::DependenciesOf(const std::string& service) const {
  std::vector<ServiceDependency> result;
  for (const auto& dependency : dependencies_) {
    if (dependency.service == service) {
      result.push_back(dependency);
    }
  }
  return result;
}

std::vector<std::string> DependencyGraph::RequiredDependenciesOf(const std::string& service) const {
  std::vector<std::string> result;
  for (const auto& dependency : dependencies_) {
    if (dependency.service == service && dependency.strength == DependencyStrength::kRequired) {
      result.push_back(dependency.depends_on);
    }
  }
  return result;
}

std::vector<std::string> DependencyGraph::OptionalDependenciesOf(const std::string& service) const {
  std::vector<std::string> result;
  for (const auto& dependency : dependencies_) {
    if (dependency.service == service && dependency.strength == DependencyStrength::kOptional) {
      result.push_back(dependency.depends_on);
    }
  }
  return result;
}

std::vector<ServiceDependency> DependencyGraph::dependencies() const {
  return dependencies_;
}

bool DependencyGraph::HasCycle(std::string* cycle) const {
  std::set<std::string> visited;
  std::vector<std::string> visiting;
  for (const auto& dependency : dependencies_) {
    if (VisitRequired(dependency.service, dependencies_, &visiting, &visited, cycle)) {
      return true;
    }
  }
  return false;
}

void DependencyGraph::Add(std::string service, std::string depends_on,
                          DependencyStrength strength) {
  if (service.empty() || depends_on.empty() || service == depends_on) {
    return;
  }
  const auto found =
      std::find_if(dependencies_.begin(), dependencies_.end(), [&](const auto& item) {
        return item.service == service && item.depends_on == depends_on &&
               item.strength == strength;
      });
  if (found != dependencies_.end()) {
    return;
  }
  dependencies_.push_back(ServiceDependency{std::move(service), std::move(depends_on), strength});
}

}  // namespace runtime
}  // namespace cockpit

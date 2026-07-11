#include "cockpit/modules/recording/file_checksum.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cockpit {
namespace recording {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

bool ComputeFnv1a64(const std::filesystem::path& path, std::string* checksum, std::string* error) {
  if (checksum == nullptr) {
    AssignError(error, "file checksum result must not be null");
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    AssignError(error, "open file for checksum failed: " + path.string());
    return false;
  }
  std::uint64_t hash = kFnvOffsetBasis;
  char buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
    const std::streamsize bytes_read = input.gcount();
    for (std::streamsize index = 0; index < bytes_read; ++index) {
      hash ^= static_cast<unsigned char>(buffer[index]);
      hash *= kFnvPrime;
    }
  }
  if (input.bad()) {
    AssignError(error, "read file for checksum failed: " + path.string());
    return false;
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  *checksum = output.str();
  return true;
}

}  // namespace recording
}  // namespace cockpit

#pragma once

#include <filesystem>
#include <string>

namespace cockpit {
namespace recording {

bool ComputeFnv1a64(const std::filesystem::path& path, std::string* checksum, std::string* error);

}  // namespace recording
}  // namespace cockpit

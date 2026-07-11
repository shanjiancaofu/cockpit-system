#pragma once

#include <google/protobuf/message.h>

#include <iosfwd>
#include <string>

namespace cockpit {
namespace diagnostics {

enum class OutputFormat {
  kText,
  kJson,
};

enum class ExitCode {
  kSuccess = 0,
  kOperationFailed = 1,
  kInvalidArguments = 2,
  kUnhealthy = 3,
};

bool ParseOutputFormat(const std::string& value, OutputFormat* format, std::string* error);
bool JsonString(const google::protobuf::Message& message, std::string* json_text,
                std::string* error);
bool WriteJson(const google::protobuf::Message& message, std::ostream* output, std::string* error);
void WriteJsonError(const std::string& category, const std::string& message, std::ostream* output);
int ToInt(ExitCode code);

}  // namespace diagnostics
}  // namespace cockpit

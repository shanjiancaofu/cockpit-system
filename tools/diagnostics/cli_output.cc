#include "tools/diagnostics/cli_output.h"

#include <google/protobuf/util/json_util.h>

#include <ostream>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace diagnostics {

bool ParseOutputFormat(const std::string& value, OutputFormat* format, std::string* error) {
  if (format == nullptr) {
    if (error != nullptr) {
      *error = "output format result must not be null";
    }
    return false;
  }
  if (value == "text") {
    *format = OutputFormat::kText;
    return true;
  }
  if (value == "json") {
    *format = OutputFormat::kJson;
    return true;
  }
  if (error != nullptr) {
    *error = "output must be text or json";
  }
  return false;
}

bool JsonString(const google::protobuf::Message& message, std::string* json_text,
                std::string* error) {
  if (json_text == nullptr) {
    if (error != nullptr) {
      *error = "JSON result must not be null";
    }
    return false;
  }
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = false;
  options.always_print_primitive_fields = true;
  options.preserve_proto_field_names = true;
  const auto status = google::protobuf::util::MessageToJsonString(message, json_text, options);
  if (!status.ok()) {
    if (error != nullptr) {
      *error = status.ToString();
    }
    return false;
  }
  return true;
}

bool WriteJson(const google::protobuf::Message& message, std::ostream* output, std::string* error) {
  if (output == nullptr) {
    if (error != nullptr) {
      *error = "JSON output stream must not be null";
    }
    return false;
  }
  std::string json_text;
  if (!JsonString(message, &json_text, error)) {
    return false;
  }
  *output << json_text << '\n';
  return true;
}

void WriteJsonError(const std::string& category, const std::string& message, std::ostream* output) {
  if (output == nullptr) {
    return;
  }
  *output << "{\"ok\":false,\"error\":{\"category\":\"" << json::EscapeString(category)
          << "\",\"message\":\"" << json::EscapeString(message) << "\"}}\n";
}

int ToInt(ExitCode code) {
  return static_cast<int>(code);
}

}  // namespace diagnostics
}  // namespace cockpit

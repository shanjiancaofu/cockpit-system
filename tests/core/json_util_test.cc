#include <iostream>
#include <string>

#include "cockpit/core/json/json.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  std::string error;
  if (!Check(cockpit::json::IsValidValue("{\"name\":[true,null,-1.5e+2]}", &error),
             "valid JSON object was rejected") ||
      !Check(cockpit::json::IsValidValue("\"\xe4\xbd\xa0\xe5\xa5\xbd\"", &error),
             "valid UTF-8 JSON string was rejected") ||
      !Check(!cockpit::json::IsValidValue("{bad}", &error), "invalid JSON object was accepted") ||
      !Check(!cockpit::json::IsValidValue("01", &error), "invalid JSON number was accepted")) {
    return 1;
  }

  const std::string escaped = cockpit::json::EscapeString(std::string("line\n\x01\"\\", 8));
  return Check(escaped == "line\\n\\u0001\\\"\\\\", "JSON string escaping mismatch") ? 0 : 1;
}

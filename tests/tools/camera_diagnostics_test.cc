#include <cassert>
#include <cstdint>
#include <string>

#include "cockpit/core/json/json.h"
#include "tools/camera/camera-diag/diagnostics.h"

int main() {
  namespace diag = cockpit::camera_diag;
  diag::FrameStats stats;
  stats.Observe(UINT32_MAX - 1, 1000000000, false);
  stats.Observe(UINT32_MAX, 1033333333, false);
  stats.Observe(0, 1066666666, false);
  assert(stats.healthy());
  assert(stats.fps() > 29.9 && stats.fps() < 30.1);
  stats.Observe(3, 1100000000, true);
  assert(stats.sequence_gaps == 2 && stats.buffer_errors == 1);
  stats.Observe(3, 1100000000, false);
  assert(stats.duplicates == 1 && stats.timestamp_errors == 1);
  stats.Observe(1, 1099999999, false);
  assert(stats.regressions == 1 && stats.sequence_gaps == 2);
  assert(!stats.healthy());
  diag::FrameStats restarted;
  restarted.Observe(0, 2000000000, false);
  assert(restarted.healthy());
  assert(restarted.sequence_gaps == 0);
  assert(cockpit::json::IsValidValue(diag::Object({{"escaped\"key", diag::Quote("a\n\tb\\c\"")},
                                                   {"unavailable", "null"},
                                                   {"errors", diag::Strings({"bad\nframe"})}})));
  const auto missing = diag::RunCommand({"/definitely/missing/camera-diag-command"});
  assert(missing.code == 127);
  const auto timeout = diag::RunCommand({"sleep", "5"}, 20);
  assert(timeout.code == 124);
  const auto output = diag::RunCommand({"printf", "camera diagnostic evidence"});
  assert(output.code == 0 && output.output == "camera diagnostic evidence");
  return 0;
}

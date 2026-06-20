#include "topic_usage.h"

#include <iostream>

namespace cockpit {
namespace topic {

void PrintUsage() {
  std::cout
      << "usage:\n"
      << "  topic list [--config configs/config.yaml]\n"
      << "  topic info <topic>\n"
      << "  topic pub <topic> <payload> [--repeat N] [--rate-ms N]\n"
      << "  topic echo <topic> [--backend file|grpc] [--count N] [--timeout-ms N] [--follow]\n"
      << "  topic hz <topic> [--backend file|grpc] [--window N] [--count N] [--timeout-ms N] [--follow]\n"
      << "\n"
      << "examples:\n"
      << "  topic pub /vehicle/state '{\"speed_kph\":12.3}'\n"
      << "  topic echo /vehicle/state --tail 5\n"
      << "  topic echo /vehicle/state --backend grpc --count 5\n"
      << "  topic hz /vehicle/state --backend grpc --window 100 --count 100\n";
}

}  // namespace topic
}  // namespace cockpit

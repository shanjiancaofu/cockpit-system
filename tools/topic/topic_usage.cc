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
      << "  topic echo <topic> [--tail N] [--follow]\n"
      << "  topic hz <topic> [--window N] [--follow]\n"
      << "\n"
      << "examples:\n"
      << "  topic pub /vehicle/state '{\"speed_kph\":12.3}'\n"
      << "  topic echo /vehicle/state --tail 5\n"
      << "  topic hz /vehicle/state --window 100\n";
}

}  // namespace topic
}  // namespace cockpit

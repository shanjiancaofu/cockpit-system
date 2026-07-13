#include "tools/safe-ota/safe_ota.h"

int main(int argc, char** argv) {
  return cockpit::safe_ota::Run(argc, argv);
}

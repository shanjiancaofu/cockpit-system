#include "tools/camera/camera-diag/diagnostics.h"
int main(int argc, char** argv) {
  return cockpit::camera_diag::RunDiagnostics(argc, argv, true);
}

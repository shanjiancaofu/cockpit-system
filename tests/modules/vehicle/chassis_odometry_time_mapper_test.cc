#include "cockpit/modules/vehicle/chassis_odometry_time_mapper.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using cockpit::vehicle::ChassisOdometryTimeMapStatus;
  cockpit::vehicle::ChassisOdometryTimeMapper mapper;
  std::int64_t first = 0;
  Require(mapper.Map(1000U, 10000000000LL, &first) == ChassisOdometryTimeMapStatus::kMapped &&
              first == 10000000000LL,
          "first device timestamp did not establish the realtime anchor");
  std::int64_t second = 0;
  Require(mapper.Map(1030U, 10040000000LL, &second) == ChassisOdometryTimeMapStatus::kMapped &&
              second - first == 30000000LL,
          "receive latency changed the device sample timeline");
  Require(mapper.Map(1029U, 10050000000LL, &second) == ChassisOdometryTimeMapStatus::kInvalid,
          "unconfirmed backwards device time was accepted as a reset");
  mapper.Reset();
  Require(mapper.Map(5U, 10060000000LL, &second) == ChassisOdometryTimeMapStatus::kReset,
          "explicit device clock reset was not reported");
  Require(mapper.Map(6U, 10059000000LL, &second) == ChassisOdometryTimeMapStatus::kInvalid,
          "regressing host realtime was accepted");

  cockpit::vehicle::ChassisOdometryTimeMapper wrap_mapper;
  std::int64_t before_wrap = 0;
  std::int64_t after_wrap = 0;
  Require(wrap_mapper.Map(0xFFFFFFF0U, 2000000000000000LL, &before_wrap) ==
              ChassisOdometryTimeMapStatus::kMapped,
          "pre-wrap timestamp was rejected");
  Require(wrap_mapper.Map(0x00000010U, 2000000040000000LL, &after_wrap) ==
                  ChassisOdometryTimeMapStatus::kMapped &&
              after_wrap - before_wrap == 32000000LL,
          "uint32 device timestamp wrap was not extended correctly");

  mapper.Reset();
  Require(mapper.Map(5U, 30000000000LL, &first) == ChassisOdometryTimeMapStatus::kReset,
          "explicit mapper reset did not clear state");
  Require(mapper.Map(6U, 30001000000LL, nullptr) == ChassisOdometryTimeMapStatus::kInvalid,
          "null timestamp output was accepted");

  std::cout << "chassis odometry timestamp mapper tests passed\n";
  return 0;
}

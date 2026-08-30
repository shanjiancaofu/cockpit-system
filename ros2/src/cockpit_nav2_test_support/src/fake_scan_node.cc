#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <stdexcept>

namespace {

using namespace std::chrono_literals;

class FakeScanNode final : public rclcpp::Node {
 public:
  FakeScanNode() : Node("cockpit_fake_scan") {
    scenario_ = declare_parameter<std::string>("scenario", "empty");
    if (scenario_ != "empty" && scenario_ != "front_wall" && scenario_ != "left_obstacle" &&
        scenario_ != "right_obstacle" && scenario_ != "narrow_passage" && scenario_ != "dropout" &&
        scenario_ != "nan" && scenario_ != "inf" && scenario_ != "invalid_range" &&
        scenario_ != "stale") {
      throw std::invalid_argument("unsupported fake scan scenario: " + scenario_);
    }
    publisher_ = create_publisher<sensor_msgs::msg::LaserScan>("scan", 10);
    timer_ = create_wall_timer(50ms, [this] {
      Publish();
    });
  }

 private:
  void Publish() {
    sensor_msgs::msg::LaserScan scan;
    scan.header.stamp = scenario_ == "stale" ? now() - rclcpp::Duration::from_seconds(2.0) : now();
    scan.header.frame_id = "base_scan";
    scan.angle_min = -3.1415927F;
    scan.angle_max = 3.1415927F;
    scan.angle_increment = 0.0174533F;
    scan.scan_time = 0.05F;
    scan.range_min = 0.05F;
    scan.range_max = 3.0F;
    scan.ranges.assign(361, scan.range_max);
    if (scenario_ == "front_wall") {
      for (std::size_t index = 150; index <= 210; ++index) scan.ranges[index] = 0.7F;
    } else if (scenario_ == "left_obstacle") {
      for (std::size_t index = 210; index <= 270; ++index) scan.ranges[index] = 0.5F;
    } else if (scenario_ == "right_obstacle") {
      for (std::size_t index = 90; index <= 150; ++index) scan.ranges[index] = 0.5F;
    } else if (scenario_ == "narrow_passage") {
      for (std::size_t index = 120; index <= 240; ++index) scan.ranges[index] = 0.45F;
      for (std::size_t index = 0; index < 90; ++index) scan.ranges[index] = 0.6F;
      for (std::size_t index = 271; index < scan.ranges.size(); ++index) scan.ranges[index] = 0.6F;
    } else if (scenario_ == "nan") {
      scan.ranges[180] = std::numeric_limits<float>::quiet_NaN();
    } else if (scenario_ == "inf") {
      scan.ranges[180] = std::numeric_limits<float>::infinity();
    } else if (scenario_ == "invalid_range") {
      scan.ranges[180] = 0.01F;
      scan.ranges[181] = 4.0F;
    }
    if (scenario_ == "dropout") return;
    publisher_->publish(scan);
  }

  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::string scenario_;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeScanNode>());
  rclcpp::shutdown();
  return 0;
}

#include <chrono>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>

namespace {

using namespace std::chrono_literals;

class FakeScanNode final : public rclcpp::Node {
 public:
  FakeScanNode() : Node("cockpit_fake_scan") {
    publisher_ = create_publisher<sensor_msgs::msg::LaserScan>("scan", 10);
    timer_ = create_wall_timer(50ms, [this] {
      Publish();
    });
  }

 private:
  void Publish() {
    sensor_msgs::msg::LaserScan scan;
    scan.header.stamp = now();
    scan.header.frame_id = "base_scan";
    scan.angle_min = -3.1415927F;
    scan.angle_max = 3.1415927F;
    scan.angle_increment = 0.0174533F;
    scan.scan_time = 0.05F;
    scan.range_min = 0.05F;
    scan.range_max = 3.0F;
    scan.ranges.assign(361, scan.range_max);
    publisher_->publish(scan);
  }

  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeScanNode>());
  rclcpp::shutdown();
  return 0;
}

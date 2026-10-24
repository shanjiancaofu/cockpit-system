#include <tf2_ros/static_transform_broadcaster.h>

#include <geometry_msgs/msg/transform_stamped.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <vector>

namespace {

class FakeTfNode final : public rclcpp::Node {
 public:
  FakeTfNode() : Node("cockpit_fake_tf"), broadcaster_(*this) {
    geometry_msgs::msg::TransformStamped map_to_odom;
    map_to_odom.header.stamp = now();
    map_to_odom.header.frame_id = "map";
    map_to_odom.child_frame_id = "odom";
    map_to_odom.transform.rotation.w = 1.0;

    geometry_msgs::msg::TransformStamped base_to_scan;
    base_to_scan.header.stamp = map_to_odom.header.stamp;
    base_to_scan.header.frame_id = "base_link";
    base_to_scan.child_frame_id = "base_scan";
    base_to_scan.transform.rotation.w = 1.0;
    broadcaster_.sendTransform(std::vector{map_to_odom, base_to_scan});
  }

 private:
  tf2_ros::StaticTransformBroadcaster broadcaster_;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeTfNode>());
  rclcpp::shutdown();
  return 0;
}

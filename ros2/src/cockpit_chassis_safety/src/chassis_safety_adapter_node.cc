#include <chrono>
#include <cstdint>
#include <geometry_msgs/msg/twist.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <sstream>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int64.hpp>
#include <stdexcept>
#include <string>

#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

namespace {

using namespace std::chrono_literals;
using cockpit::vehicle::ChassisSafetyAdapter;
using cockpit::vehicle::ChassisSafetyPolicy;
using cockpit::vehicle::ChassisSafetyState;
using cockpit::vehicle::ChassisVelocityRequest;
using cockpit::vehicle::SafeChassisCommand;

std::int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

class ChassisSafetyAdapterNode final : public rclcpp::Node {
 public:
  ChassisSafetyAdapterNode()
      : Node("cockpit_chassis_safety_adapter"), policy_(LoadPolicy()), adapter_(policy_) {
    state_.enabled = declare_parameter<bool>("enabled", false);
    state_.authority_granted = declare_parameter<bool>("authority_granted", false);
    state_.emergency_stop = declare_parameter<bool>("emergency_stop", false);
    state_.peer_alive = declare_parameter<bool>("peer_alive", false);
    state_.chassis_fault = declare_parameter<bool>("chassis_fault", false);

    command_publisher_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel_safe", 10);
    last_command_publisher_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel_safe_last", 1);
    status_publisher_ = create_publisher<std_msgs::msg::String>(
        "chassis_safety/status", rclcpp::QoS(1).reliable().transient_local());
    sequence_publisher_ = create_publisher<std_msgs::msg::UInt64>(
        "chassis_safety/sequence", rclcpp::QoS(1).reliable().transient_local());

    command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "cmd_vel", 10, [this](const geometry_msgs::msg::Twist::ConstSharedPtr& message) {
          ChassisVelocityRequest request;
          request.linear_velocity_m_s = message->linear.x;
          request.angular_velocity_rad_s = message->angular.z;
          std::string error;
          if (!adapter_.Submit(request, state_, SteadyNowMs(), &error)) {
            RCLCPP_ERROR(get_logger(), "rejected cmd_vel: %s", error.c_str());
          }
        });
    enable_subscription_ = BoolSubscription("chassis_safety/enable", &state_.enabled);
    authority_subscription_ =
        BoolSubscription("chassis_safety/authority", &state_.authority_granted);
    emergency_stop_subscription_ =
        BoolSubscription("chassis_safety/emergency_stop", &state_.emergency_stop);
    peer_subscription_ = BoolSubscription("chassis_safety/peer_alive", &state_.peer_alive);
    fault_subscription_ = BoolSubscription("chassis_safety/chassis_fault", &state_.chassis_fault);

    timer_ = create_wall_timer(std::chrono::milliseconds(policy_.output_period_ms), [this] {
      Publish(adapter_.Evaluate(state_, SteadyNowMs()));
    });
    RCLCPP_WARN(get_logger(),
                "production safety policy active; no SocketCAN sink is connected by this node");
  }

 private:
  ChassisSafetyPolicy LoadPolicy() {
    ChassisSafetyPolicy policy;
    policy.max_linear_velocity_mm_s = declare_parameter<int>("max_linear_velocity_mm_s", 400);
    policy.max_angular_velocity_mrad_s =
        declare_parameter<int>("max_angular_velocity_mrad_s", 1200);
    policy.max_linear_acceleration_mm_s2 =
        declare_parameter<int>("max_linear_acceleration_mm_s2", 400);
    policy.max_angular_acceleration_mrad_s2 =
        declare_parameter<int>("max_angular_acceleration_mrad_s2", 1200);
    policy.command_timeout_ms = declare_parameter<int>("command_timeout_ms", 250);
    policy.output_period_ms = declare_parameter<int>("output_period_ms", 20);
    if (!policy.IsValid()) {
      throw std::invalid_argument("invalid chassis safety parameters");
    }
    return policy;
  }

  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr BoolSubscription(const std::string& topic,
                                                                        bool* destination) {
    return create_subscription<std_msgs::msg::Bool>(
        topic, 10, [this, destination](const std_msgs::msg::Bool::ConstSharedPtr& message) {
          *destination = message->data;
          if (!SafetyReady()) adapter_.Reset(SteadyNowMs());
        });
  }

  bool SafetyReady() const {
    return state_.enabled && state_.authority_granted && !state_.emergency_stop &&
           state_.peer_alive && !state_.chassis_fault;
  }

  void Publish(const SafeChassisCommand& command) {
    geometry_msgs::msg::Twist message;
    if (command.enabled) {
      message.linear.x = static_cast<double>(command.linear_velocity_mm_s) / 1000.0;
      message.angular.z = static_cast<double>(command.angular_velocity_mrad_s) / 1000.0;
    }
    command_publisher_->publish(message);
    last_command_publisher_->publish(message);

    std_msgs::msg::UInt64 sequence;
    sequence.data = command.sequence;
    sequence_publisher_->publish(sequence);

    std::ostringstream json;
    json << "{\"enabled\":" << (command.enabled ? "true" : "false")
         << ",\"sequence\":" << static_cast<unsigned int>(command.sequence)
         << ",\"linear_velocity_mm_s\":" << command.linear_velocity_mm_s
         << ",\"angular_velocity_mrad_s\":" << command.angular_velocity_mrad_s
         << ",\"stop_reason\":\"" << cockpit::vehicle::ToString(command.stop_reason) << "\"}";
    std_msgs::msg::String status;
    status.data = json.str();
    status_publisher_->publish(status);
  }

  ChassisSafetyPolicy policy_;
  ChassisSafetyAdapter adapter_;
  ChassisSafetyState state_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr command_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr last_command_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_publisher_;
  rclcpp::Publisher<std_msgs::msg::UInt64>::SharedPtr sequence_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr enable_subscription_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr authority_subscription_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr emergency_stop_subscription_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr peer_subscription_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr fault_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ChassisSafetyAdapterNode>());
  rclcpp::shutdown();
  return 0;
}

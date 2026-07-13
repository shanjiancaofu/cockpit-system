#include "cockpit/library/transfer/gateway_grpc_service.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "gateway.grpc.pb.h"

int main() {
  const std::string socket_path = "/tmp/cockpit-gateway-test-" + std::to_string(getpid()) + ".sock";
  const std::string address = "unix:" + socket_path;
  std::filesystem::remove(socket_path);

  cockpit::gateway::GatewayGrpcService service;
  if (!service.Start(address, 100)) {
    std::cerr << "failed to start gateway test server" << std::endl;
    return 1;
  }

  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
  auto stub = cockpit::proto::gateway::CockpitGateway::NewStub(channel);

  grpc::ClientContext list_context;
  list_context.set_wait_for_ready(true);
  list_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  cockpit::proto::gateway::ListTopicsRequest list_request;
  list_request.set_client_id("gateway-grpc-service-test");
  cockpit::proto::gateway::ListTopicsResponse list_response;
  const grpc::Status list_status = stub->ListTopics(&list_context, list_request, &list_response);
  if (!list_status.ok() || list_response.topics_size() != 1) {
    std::cerr << "topic list request failed" << std::endl;
    return 1;
  }

  const auto& waiting_topic = list_response.topics(0);
  if (waiting_topic.name() != "/vehicle/state" || waiting_topic.transport() != "grpc" ||
      waiting_topic.expected_update_period_ms() != 100 ||
      waiting_topic.availability() !=
          cockpit::proto::gateway::TOPIC_AVAILABILITY_WAITING_FOR_DATA ||
      waiting_topic.last_update_age_ms() != -1 || waiting_topic.error_reason().empty()) {
    std::cerr << "waiting topic metadata is invalid" << std::endl;
    return 1;
  }

  cockpit::proto::vehicle::VehicleState state;
  state.set_timestamp_ms(1000);
  state.set_source("test");
  service.PublishVehicleState(state);

  cockpit::proto::gateway::GetTopicInfoRequest info_request;
  info_request.set_client_id("gateway-grpc-service-test");
  info_request.set_topic("/vehicle/state");
  grpc::ClientContext available_context;
  available_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  cockpit::proto::gateway::TopicMetadata available_topic;
  const grpc::Status available_status =
      stub->GetTopicInfo(&available_context, info_request, &available_topic);
  if (!available_status.ok() ||
      available_topic.availability() != cockpit::proto::gateway::TOPIC_AVAILABILITY_AVAILABLE ||
      available_topic.last_update_age_ms() < 0 || !available_topic.error_reason().empty()) {
    std::cerr << "available topic metadata is invalid" << std::endl;
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(2100));
  grpc::ClientContext stale_context;
  stale_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  cockpit::proto::gateway::TopicMetadata stale_topic;
  const grpc::Status stale_status = stub->GetTopicInfo(&stale_context, info_request, &stale_topic);
  if (!stale_status.ok() ||
      stale_topic.availability() != cockpit::proto::gateway::TOPIC_AVAILABILITY_STALE ||
      stale_topic.last_update_age_ms() < 2000 || stale_topic.error_reason().empty()) {
    std::cerr << "stale topic metadata is invalid" << std::endl;
    return 1;
  }

  info_request.set_topic("/unknown");
  grpc::ClientContext missing_context;
  missing_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  cockpit::proto::gateway::TopicMetadata missing_topic;
  const grpc::Status missing_status =
      stub->GetTopicInfo(&missing_context, info_request, &missing_topic);
  if (missing_status.error_code() != grpc::StatusCode::NOT_FOUND) {
    std::cerr << "unknown topic did not return NOT_FOUND" << std::endl;
    return 1;
  }

  service.Shutdown();
  std::filesystem::remove(socket_path);
  return 0;
}

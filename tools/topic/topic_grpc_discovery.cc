#include "topic_grpc_discovery.h"

#include "gateway.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

namespace cockpit {
namespace topic {
namespace {

std::shared_ptr<grpc::Channel> CreateChannel(const std::string& address) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  return grpc::CreateCustomChannel(
      address, grpc::InsecureChannelCredentials(), arguments);
}

void ConfigureContext(grpc::ClientContext* context, int timeout_ms) {
  context->set_wait_for_ready(true);
  context->set_deadline(
      std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms));
}

TopicMetadata FromProto(const proto::gateway::TopicMetadata& metadata) {
  return {metadata.name(), metadata.message_type(), metadata.source(),
          metadata.subscribable(), metadata.publishable()};
}

int ReportError(const std::string& operation, const grpc::Status& status) {
  std::cerr << operation << " failed: code=" << status.error_code()
            << " message=" << status.error_message() << '\n';
  return status.error_code() == grpc::StatusCode::NOT_FOUND ? 2 : 1;
}

}  // namespace

TopicGrpcDiscovery::TopicGrpcDiscovery(std::string address, int timeout_ms)
    : address_(std::move(address)), timeout_ms_(timeout_ms) {}

int TopicGrpcDiscovery::List(std::vector<TopicMetadata>* topics) const {
  auto stub = proto::gateway::CockpitGateway::NewStub(CreateChannel(address_));
  grpc::ClientContext context;
  ConfigureContext(&context, timeout_ms_);
  proto::gateway::ListTopicsRequest request;
  request.set_client_id("topic-list");
  proto::gateway::ListTopicsResponse response;
  const grpc::Status status = stub->ListTopics(&context, request, &response);
  if (!status.ok()) {
    return ReportError("topic list", status);
  }

  topics->clear();
  topics->reserve(static_cast<std::size_t>(response.topics_size()));
  for (const auto& metadata : response.topics()) {
    topics->push_back(FromProto(metadata));
  }
  return 0;
}

int TopicGrpcDiscovery::Get(const std::string& topic, TopicMetadata* metadata) const {
  auto stub = proto::gateway::CockpitGateway::NewStub(CreateChannel(address_));
  grpc::ClientContext context;
  ConfigureContext(&context, timeout_ms_);
  proto::gateway::GetTopicInfoRequest request;
  request.set_client_id("topic-info");
  request.set_topic(topic);
  proto::gateway::TopicMetadata response;
  const grpc::Status status = stub->GetTopicInfo(&context, request, &response);
  if (!status.ok()) {
    return ReportError("topic info", status);
  }
  *metadata = FromProto(response);
  return 0;
}

}  // namespace topic
}  // namespace cockpit

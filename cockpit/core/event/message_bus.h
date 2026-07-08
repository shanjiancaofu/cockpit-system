#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/core/event/event_queue.h"

namespace cockpit {
namespace event {

struct EventMessage {
  std::string topic;
  std::string type;
  std::string source;
  std::string payload_json;
  std::int64_t timestamp_ms = 0;
  std::uint64_t sequence = 0;

  bool IsValid() const;
};

struct MessageBusMetrics {
  std::uint64_t published = 0;
  std::uint64_t delivered = 0;
  std::uint64_t dropped = 0;
  std::uint64_t subscribers = 0;
};

class MessageSubscription {
 public:
  MessageSubscription(std::string topic, std::shared_ptr<EventQueue<EventMessage>> queue);

  const std::string& topic() const;
  std::optional<EventMessage> TryPop();
  std::optional<EventMessage> WaitPop();
  std::optional<EventMessage> WaitPopFor(std::chrono::milliseconds timeout);
  std::uint64_t DropCount() const;
  void Close();

 private:
  std::string topic_;
  std::shared_ptr<EventQueue<EventMessage>> queue_;
};

class MessageBus {
 public:
  explicit MessageBus(std::size_t default_queue_capacity = 128);

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(MessageBus);

  std::shared_ptr<MessageSubscription> Subscribe(const std::string& topic);
  std::shared_ptr<MessageSubscription> Subscribe(const std::string& topic,
                                                 std::size_t queue_capacity);
  bool Publish(EventMessage message);
  MessageBusMetrics metrics() const;
  void Close();

 private:
  struct Subscriber;

  std::size_t default_queue_capacity_;
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Subscriber>> subscribers_;
  MessageBusMetrics metrics_;
  bool closed_ = false;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace event
}  // namespace cockpit

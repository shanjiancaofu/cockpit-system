#include "cockpit/core/event/message_bus.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace event {
namespace {

bool TopicMatches(const std::string& subscription, const std::string& topic) {
  return subscription == "*" || subscription == topic;
}

}  // namespace

struct MessageBus::Subscriber {
  std::string topic;
  std::weak_ptr<EventQueue<EventMessage>> queue;
};

bool EventMessage::IsValid() const {
  return !topic.empty() && !payload_json.empty();
}

MessageSubscription::MessageSubscription(std::string topic,
                                         std::shared_ptr<EventQueue<EventMessage>> queue)
    : topic_(std::move(topic)), queue_(std::move(queue)) {
}

const std::string& MessageSubscription::topic() const {
  return topic_;
}

std::optional<EventMessage> MessageSubscription::TryPop() {
  return queue_ == nullptr ? std::nullopt : queue_->TryPop();
}

std::optional<EventMessage> MessageSubscription::WaitPop() {
  return queue_ == nullptr ? std::nullopt : queue_->WaitPop();
}

std::optional<EventMessage> MessageSubscription::WaitPopFor(std::chrono::milliseconds timeout) {
  return queue_ == nullptr ? std::nullopt : queue_->WaitPopFor(timeout);
}

std::uint64_t MessageSubscription::DropCount() const {
  return queue_ == nullptr ? 0 : queue_->DropCount();
}

void MessageSubscription::Close() {
  if (queue_ != nullptr) {
    queue_->Close();
  }
}

MessageBus::MessageBus(std::size_t default_queue_capacity)
    : default_queue_capacity_(default_queue_capacity) {
}

std::shared_ptr<MessageSubscription> MessageBus::Subscribe(const std::string& topic) {
  return Subscribe(topic, default_queue_capacity_);
}

std::shared_ptr<MessageSubscription> MessageBus::Subscribe(const std::string& topic,
                                                           std::size_t queue_capacity) {
  auto queue = std::make_shared<EventQueue<EventMessage>>(queue_capacity);
  auto subscription = std::make_shared<MessageSubscription>(topic, queue);
  auto subscriber = std::make_shared<Subscriber>();
  subscriber->topic = topic;
  subscriber->queue = queue;
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    queue->Close();
    return subscription;
  }
  subscribers_.push_back(std::move(subscriber));
  metrics_.subscribers = subscribers_.size();
  return subscription;
}

bool MessageBus::Publish(EventMessage message) {
  if (!message.IsValid()) {
    return false;
  }
  if (message.timestamp_ms == 0) {
    message.timestamp_ms = time::WallTime::Now().ToMilliseconds();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    return false;
  }
  message.sequence = next_sequence_++;
  ++metrics_.published;
  subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                    [](const auto& sub) {
                                      return sub->queue.expired();
                                    }),
                     subscribers_.end());
  for (const auto& subscriber : subscribers_) {
    if (!TopicMatches(subscriber->topic, message.topic)) {
      continue;
    }
    auto queue = subscriber->queue.lock();
    if (queue == nullptr) {
      continue;
    }
    const EventQueuePushResult result = queue->Push(message);
    if (result == EventQueuePushResult::kAccepted) {
      ++metrics_.delivered;
    } else {
      ++metrics_.dropped;
    }
  }
  metrics_.subscribers = subscribers_.size();
  return true;
}

MessageBusMetrics MessageBus::metrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  MessageBusMetrics metrics = metrics_;
  metrics.subscribers = subscribers_.size();
  return metrics;
}

void MessageBus::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  closed_ = true;
  for (const auto& subscriber : subscribers_) {
    auto queue = subscriber->queue.lock();
    if (queue != nullptr) {
      queue->Close();
    }
  }
}

}  // namespace event
}  // namespace cockpit

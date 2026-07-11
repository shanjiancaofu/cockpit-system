#include "cockpit/core/event/event_queue.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

using cockpit::event::EventQueue;
using cockpit::event::EventQueuePushResult;

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool TestOrderAndOverflow() {
  EventQueue<int> queue(2);
  if (!Check(queue.capacity() == 2, "event queue capacity changed") ||
      !Check(queue.Push(1) == EventQueuePushResult::kAccepted, "first event was rejected") ||
      !Check(queue.Push(2) == EventQueuePushResult::kAccepted, "second event was rejected") ||
      !Check(queue.Push(3) == EventQueuePushResult::kFull, "full event queue accepted an event") ||
      !Check(queue.DropCount() == 1, "drop count did not increase") ||
      !Check(queue.Size() == 2, "queue size changed after overflow")) {
    return false;
  }

  auto first = queue.TryPop();
  auto second = queue.TryPop();
  return Check(first.has_value() && *first == 1, "first event order changed") &&
         Check(second.has_value() && *second == 2, "second event order changed") &&
         Check(!queue.TryPop().has_value(), "empty event queue returned data");
}

bool TestMoveOnlyEvent() {
  struct MoveOnlyEvent {
    explicit MoveOnlyEvent(std::string input) : value(std::move(input)) {
    }
    MoveOnlyEvent(const MoveOnlyEvent&) = delete;
    MoveOnlyEvent& operator=(const MoveOnlyEvent&) = delete;
    MoveOnlyEvent(MoveOnlyEvent&&) = default;
    MoveOnlyEvent& operator=(MoveOnlyEvent&&) = default;

    std::string value;
  };

  EventQueue<MoveOnlyEvent> queue(1);
  if (!Check(queue.Push(MoveOnlyEvent("voice.intent")) == EventQueuePushResult::kAccepted,
             "move-only event was rejected")) {
    return false;
  }
  auto event = queue.TryPop();
  return Check(event.has_value(), "move-only event was lost") &&
         Check(event->value == "voice.intent", "move-only event value changed");
}

bool TestWaitPopAndClose() {
  EventQueue<int> queue(4);
  std::thread producer([&queue] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    queue.Push(42);
  });

  auto event = queue.WaitPopFor(std::chrono::milliseconds(500));
  producer.join();
  if (!Check(event.has_value() && *event == 42, "wait pop did not receive event")) {
    return false;
  }

  const auto timeout = queue.WaitPopFor(std::chrono::milliseconds(20));
  if (!Check(!timeout.has_value(), "empty timed wait returned event")) {
    return false;
  }

  queue.Close();
  return Check(queue.closed(), "queue did not close") &&
         Check(queue.Push(7) == EventQueuePushResult::kClosed, "closed queue accepted event") &&
         Check(!queue.WaitPop().has_value(), "closed empty queue returned event");
}

bool TestReset() {
  EventQueue<int> queue(1);
  queue.Push(1);
  queue.Push(2);
  queue.Close();
  queue.Reset();

  return Check(!queue.closed(), "reset queue is still closed") &&
         Check(queue.DropCount() == 0, "reset did not clear drops") &&
         Check(queue.Size() == 0, "reset did not clear queue") &&
         Check(queue.Push(3) == EventQueuePushResult::kAccepted, "reset queue rejected event");
}

bool TestDiscardPending() {
  EventQueue<int> queue(3);
  queue.Push(1);
  queue.Push(2);
  queue.Close();
  return Check(queue.DiscardPending() == 2, "discarded event count mismatch") &&
         Check(queue.Size() == 0, "discard did not clear queue") &&
         Check(queue.DropCount() == 2, "discard did not update drop count");
}

}  // namespace

int main() {
  return TestOrderAndOverflow() && TestMoveOnlyEvent() && TestWaitPopAndClose() && TestReset() &&
                 TestDiscardPending()
             ? 0
             : 1;
}

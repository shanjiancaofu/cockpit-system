#include "cockpit/transfer/video/h264_receiver.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace cockpit::transfer;
using namespace std::chrono_literals;
namespace {
void Check(bool ok, const char* error) {
  if (!ok) throw std::runtime_error(error);
}
std::vector<std::uint8_t> Datagram(std::uint32_t frame, std::uint16_t index, std::uint16_t total,
                                   std::uint32_t pts, std::vector<std::uint8_t> payload) {
  std::vector<std::uint8_t> data(24);
  auto put32 = [&](std::size_t offset, std::uint32_t v) {
    v = htonl(v);
    std::memcpy(data.data() + offset, &v, 4);
  };
  auto put16 = [&](std::size_t offset, std::uint16_t v) {
    v = htons(v);
    std::memcpy(data.data() + offset, &v, 2);
  };
  put32(0, 0x43525031);
  put32(4, frame);
  put16(8, index);
  put16(10, total);
  put32(16, pts);
  put32(20, 1);
  data.insert(data.end(), payload.begin(), payload.end());
  return data;
}
H264Packet Packet(std::int64_t pts, bool idr) {
  H264Packet packet;
  packet.pts = packet.dts = pts;
  packet.keyframe = !idr;  // Deliberately wrong: validated NAL types must win.
  if (idr) packet.data = {0, 0, 1, 0x67, 0x42, 0xe0, 0x1f, 0, 0, 0, 1, 0x68, 0xce, 0x3c, 0x80};
  packet.data.insert(packet.data.end(),
                     {0, 0, 1, static_cast<std::uint8_t>(idr ? 0x65 : 0x41), 0x88});
  return packet;
}
}  // namespace
int main() {
  try {
    Crp1Assembler assembler;
    auto a = Datagram(0, 0, 2, 10, {0, 0, 0, 1});
    auto b = Datagram(0, 1, 2, 10, {0x65, 0x88});
    Check(!assembler.Feed(b.data(), b.size()), "out-of-order partial should wait");
    Check(!assembler.Feed(b.data(), b.size()), "duplicate fragment should wait");
    auto packet = assembler.Feed(a.data(), a.size());
    Check(packet && packet->data == std::vector<std::uint8_t>({0, 0, 0, 1, 0x65, 0x88}) &&
              packet->pts == 10,
          "reassembly failed");
    Check(assembler.TakeDiscontinuity(), "first packet must be discontinuous");
    Check(!assembler.Feed(a.data(), a.size()), "completed duplicate must be ignored");
    auto next = Datagram(1, 0, 1, 11, {0, 0, 1, 0x41, 0x88});
    Check(assembler.Feed(next.data(), next.size()).has_value(), "next frame failed");
    Check(!assembler.TakeDiscontinuity(), "contiguous frames marked discontinuous");
    auto malformed = Datagram(2, 0, 8193, 12, {1});
    Check(!assembler.Feed(malformed.data(), malformed.size()), "unbounded fragments accepted");
    auto skipped = Datagram(3, 0, 1, 13, {0, 0, 1, 0x65, 0x88});
    Check(
        assembler.Feed(skipped.data(), skipped.size()).has_value() && assembler.TakeDiscontinuity(),
        "gap not detected");
    a = Datagram(4, 0, 2, 14, {1});
    b = Datagram(4, 1, 2, 15, {2});
    Check(!assembler.Feed(a.data(), a.size()) && !assembler.Feed(b.data(), b.size()),
          "inconsistent timestamps accepted");
    Check(H264Nals({0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0x80}).size() == 2,
          "mixed Annex-B parsing failed");
    Check(H264Nals({0x65, 0x88}).empty(), "non Annex-B accepted");

    H264Receiver waiting;
    H264Packet parameter_sets;
    parameter_sets.data = {0, 0, 0, 1, 0x67, 0x42, 0xe0, 0x1f, 0, 0, 0, 1, 0x68, 0x80};
    waiting.Publish(parameter_sets, false);
    Check(waiting.Status().available && !waiting.Status().has_idr,
          "parameter sets alone incorrectly marked IDR ready");
    waiting.Publish(Packet(1, false), false);
    Check(!waiting.Status().has_idr, "P frame incorrectly marked IDR ready");
    waiting.Publish(Packet(2, true), false);
    Check(waiting.Status().has_idr, "valid IDR did not mark stream ready");
    waiting.Publish(Packet(0, false), false);
    Check(!waiting.Status().has_idr, "new generation retained old IDR readiness");
    H264Receiver receiver;
    std::promise<VideoFramePtr> delivered;
    auto subscription = receiver.Subscribe([&](VideoFramePtr frame) {
      delivered.set_value(frame);
    });
    receiver.Publish(Packet(1, false), false);
    receiver.Publish(Packet(2, true), false);
    auto future = delivered.get_future();
    Check(future.wait_for(1s) == std::future_status::ready, "IDR not delivered");
    auto frame = future.get();
    Check(frame->packet.keyframe && frame->sps.size() == 4 && !frame->pps.empty() &&
              frame->discontinuity,
          "codec/IDR validation failed");
    subscription->Stop();
    const auto old_generation = receiver.Status().generation;
    receiver.Publish(Packet(0, true), false);
    Check(receiver.Status().generation > old_generation, "PTS rollback must create generation");

    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::promise<VideoFramePtr> resumed;
    std::atomic_int calls{0};
    std::atomic_uint requested_idrs{0};
    VideoSubscription slow(
        [&](VideoFramePtr value) {
          if (++calls == 1) {
            entered.set_value();
            gate.wait();
          } else
            resumed.set_value(value);
        },
        1,
        [&] {
          ++requested_idrs;
        });
    slow.Enqueue(frame);
    Check(entered.get_future().wait_for(1s) == std::future_status::ready, "consumer did not start");
    auto delta = std::make_shared<VideoFrame>(*frame);
    delta->packet = Packet(3, false);
    delta->packet.keyframe = false;
    delta->discontinuity = false;
    slow.Enqueue(delta);
    slow.Enqueue(delta);  // Overflow: queued P frames must be discarded.
    Check(requested_idrs > 0, "queue overflow did not actively request IDR");
    auto idr = std::make_shared<VideoFrame>(*frame);
    idr->packet.pts = 4;
    idr->discontinuity = false;
    slow.Enqueue(idr);
    release.set_value();
    auto recovered = resumed.get_future();
    Check(recovered.wait_for(1s) == std::future_status::ready && recovered.get()->discontinuity,
          "overflow did not recover at IDR");
    slow.Stop();
    // One blocked transport must not block Publish or a healthy subscription.
    H264Receiver isolation;
    std::promise<void> blocked, unblock, healthy_last;
    auto unblock_gate = unblock.get_future().share();
    std::atomic_int slow_calls{0};
    auto slow_peer = isolation.Subscribe([&](VideoFramePtr) {
      if (++slow_calls == 1) {
        blocked.set_value();
        unblock_gate.wait();
      }
    });
    auto fast_peer = isolation.Subscribe([&](VideoFramePtr value) {
      if (value->packet.pts == 300) healthy_last.set_value();
    });
    isolation.Publish(Packet(0, true), false);
    const bool slow_started = blocked.get_future().wait_for(1s) == std::future_status::ready;
    for (int pts = 1; pts <= 300; ++pts) isolation.Publish(Packet(pts, pts % 10 == 0), false);
    const bool healthy_progress =
        healthy_last.get_future().wait_for(2s) == std::future_status::ready;
    const auto slow_stats = slow_peer->Stats();
    unblock.set_value();  // Release even when an assertion would fail; destructor joins.
    Check(slow_started && healthy_progress, "blocked client stalled a healthy transport");
    Check(slow_stats.dropped > 0 && slow_stats.queued <= 8 &&
              slow_stats.queued_bytes <= 16 * 1024 * 1024,
          "slow-client queue was not bounded/accounted");
    slow_peer->Stop();
    fast_peer->Stop();

    std::atomic_int attempts{0};
    auto faulty = isolation.Subscribe([&](VideoFramePtr) {
      const auto attempt = ++attempts;
      if (attempt == 1) throw std::runtime_error("injected transport failure");
      if (attempt == 2) throw 42;
    });
    auto await = [&](const auto& predicate) {
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
      return predicate();
    };
    isolation.Publish(Packet(310, true), false);
    Check(await([&] {
            return faulty->Stats().failures == 1;
          }),
          "consumer exception not counted");
    Check(faulty->Stats().last_error == "injected transport failure", "consumer diagnostic lost");
    isolation.Publish(Packet(311, false), false);
    isolation.Publish(Packet(320, true), false);
    Check(await([&] {
            return faulty->Stats().failures == 2;
          }),
          "non-standard exception escaped");
    isolation.Publish(Packet(321, false), false);
    isolation.Publish(Packet(330, true), false);
    Check(await([&] {
            return faulty->Stats().delivered == 1;
          }),
          "consumer did not recover at IDR");
    Check(isolation.Status().consumer_failures == 2 && isolation.Status().consumer_dropped > 0,
          "receiver status does not expose active consumer failures/loss");
    faulty->Stop();
    receiver.Stop();
    Check(!receiver.Status().available, "stop leaves stream available");
    std::cout << "CRP1, Annex-B, generation and bounded-queue tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

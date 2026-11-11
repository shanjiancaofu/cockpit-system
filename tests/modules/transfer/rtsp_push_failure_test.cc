#include <arpa/inet.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "cockpit/transfer/rtsp/rtsp_server.h"

namespace {
std::atomic_bool fail_next{false};
void Check(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}
std::uint16_t FreePort() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  Check(fd >= 0, "test socket failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool ok = bind(fd, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
                  getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  close(fd);
  Check(ok, "test port allocation failed");
  return ntohs(address.sin_port);
}
}  // namespace
extern "C" GstFlowReturn __real_gst_app_src_push_buffer(GstAppSrc*, GstBuffer*);
extern "C" GstFlowReturn __wrap_gst_app_src_push_buffer(GstAppSrc* source, GstBuffer* buffer) {
  if (fail_next.exchange(false)) {
    gst_buffer_unref(buffer);
    return GST_FLOW_FLUSHING;
  }
  return __real_gst_app_src_push_buffer(source, buffer);
}

int main() {
  using namespace cockpit::transfer;
  gst_init(nullptr, nullptr);
  for (const char* name : {"videotestsrc", "x264enc", "h264parse", "rtph264pay", "rtspsrc"}) {
    auto* factory = gst_element_factory_find(name);
    if (!factory) {
      std::cerr << "SKIP missing " << name << '\n';
      return 77;
    }
    gst_object_unref(factory);
  }
  GstElement* client = nullptr;
  try {
    auto* encoder = gst_parse_launch(
        "videotestsrc num-buffers=1 ! "
        "video/x-raw,format=I420,width=64,height=64,framerate=30/1 ! "
        "x264enc tune=zerolatency byte-stream=true ! "
        "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au ! "
        "appsink name=out",
        nullptr);
    Check(encoder, "create fixture failed");
    auto* sink = gst_bin_get_by_name(GST_BIN(encoder), "out");
    gst_element_set_state(encoder, GST_STATE_PLAYING);
    auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    Check(sample, "no encoded fixture");
    GstMapInfo map{};
    auto* buffer = gst_sample_get_buffer(sample);
    Check(gst_buffer_map(buffer, &map, GST_MAP_READ), "map fixture failed");
    H264Packet packet;
    packet.data.assign(map.data, map.data + map.size);
    packet.keyframe = true;
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    gst_element_set_state(encoder, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(encoder);

    H264Receiver receiver;
    receiver.Publish(packet, false);
    RtspServer server;
    RtspServerConfig config;
    config.address = "127.0.0.1";
    config.port = FreePort();
    std::string error;
    Check(server.Start(config, receiver, &error), error.c_str());
    const auto launch =
        "rtspsrc location=rtsp://127.0.0.1:" + std::to_string(config.port) +
        "/camera protocols=tcp latency=0 ! rtph264depay ! h264parse ! fakesink sync=false";
    client = gst_parse_launch(launch.c_str(), nullptr);
    Check(client, "create RTSP client failed");
    fail_next = true;  // Cached preroll IDR push fails first.
    gst_element_set_state(client, GST_STATE_PLAYING);
    const auto pump = [&](std::uint64_t failures, std::uint64_t pushed) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      do {
        ++packet.pts;
        packet.dts = packet.pts;
        receiver.Publish(packet, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        const auto stats = server.Stats();
        if (stats.failures >= failures && stats.pushed > pushed) return true;
      } while (std::chrono::steady_clock::now() < deadline);
      return false;
    };
    Check(pump(1, 0), "cached push failure not counted/recovered");
    const auto first = server.Stats();
    fail_next = true;  // Then fail a live push on the already configured appsrc.
    Check(pump(2, first.pushed), "live push failure not counted/recovered");
    Check(server.running() && server.Stats().last_flow_return == GST_FLOW_OK,
          "failed transport permanently stopped server");
    gst_element_set_state(client, GST_STATE_NULL);
    gst_object_unref(client);
    client = nullptr;
    server.Stop();
    std::cout << "cached/live RTSP push failures counted and recovered at IDR\n";
    return 0;
  } catch (const std::exception& error) {
    fail_next = false;
    if (client) {
      gst_element_set_state(client, GST_STATE_NULL);
      gst_object_unref(client);
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
}

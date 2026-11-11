#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "cockpit/spider/recording/runtime/recording_service.h"
#include "cockpit/spider/recording/video/event_video_recorder.h"

namespace {
enum class Fault { None, Timeout, Pipeline, EndStream, Write };
Fault fault = Fault::None;
std::size_t write_calls = 0;
void Require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace

// Link-time faults affect this test executable only; no production test hooks.
extern "C" GstMessage* __real_gst_bus_timed_pop_filtered(GstBus*, GstClockTime, GstMessageType);
extern "C" GstMessage* __wrap_gst_bus_timed_pop_filtered(GstBus* bus, GstClockTime timeout,
                                                         GstMessageType types) {
  if (fault == Fault::Timeout) {
    Require(timeout == 5 * GST_SECOND, "unexpected EOS deadline");
    return nullptr;
  }
  if (fault == Fault::Pipeline) {
    GError* error =
        g_error_new_literal(g_quark_from_static_string("test-mp4"), 1, "injected pipeline error");
    auto* message = gst_message_new_error(nullptr, error, "test");
    g_error_free(error);
    return message;
  }
  return __real_gst_bus_timed_pop_filtered(bus, timeout, types);
}
extern "C" GstFlowReturn __real_gst_app_src_end_of_stream(GstAppSrc*);
extern "C" GstFlowReturn __wrap_gst_app_src_end_of_stream(GstAppSrc* source) {
  if (fault == Fault::EndStream) return GST_FLOW_ERROR;
  return __real_gst_app_src_end_of_stream(source);
}
extern "C" GstFlowReturn __real_gst_app_src_push_buffer(GstAppSrc*, GstBuffer*);
extern "C" GstFlowReturn __wrap_gst_app_src_push_buffer(GstAppSrc* source, GstBuffer* buffer) {
  if (fault == Fault::Write) {
    gst_buffer_unref(buffer);
    return GST_FLOW_ERROR;
  }
  ++write_calls;
  return __real_gst_app_src_push_buffer(source, buffer);
}

int main() {
  using namespace cockpit::recording;
  gst_init(nullptr, nullptr);
  for (const char* name :
       {"videotestsrc", "x264enc", "h264parse", "mp4mux", "appsink", "openh264dec"}) {
    auto* factory = gst_element_factory_find(name);
    if (!factory) {
      std::cerr << "SKIP: missing GStreamer test plugin " << name << '\n';
      return 77;
    }
    gst_object_unref(factory);
  }
  const auto directory =
      std::filesystem::temp_directory_path() / ("cockpit-mp4-failure-" + std::to_string(getpid()));
  try {
    std::filesystem::create_directories(directory);
    // Real CPU-only H264 fixture: no sensor, Argus, display or hardware codec.
    GError* error = nullptr;
    auto* pipeline = gst_parse_launch(
        "videotestsrc num-buffers=1 ! video/x-raw,format=I420,width=64,height=64,framerate=30/1 ! "
        "x264enc tune=zerolatency byte-stream=true ! "
        "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au ! "
        "appsink name=sample",
        &error);
    Require(pipeline && !error, "H264 test fixture unavailable");
    auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sample");
    Require(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
            "start fixture failed");
    auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    Require(sample, "no encoded fixture");
    GstMapInfo map{};
    auto* buffer = gst_sample_get_buffer(sample);
    Require(gst_buffer_map(buffer, &map, GST_MAP_READ), "map fixture failed");
    EncodedVideoPacket packet;
    packet.data.assign(map.data, map.data + map.size);
    packet.keyframe = true;
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(pipeline);

    Mp4Muxer muxer;
    std::string why = "stale caller error";
    Require(muxer.Open((directory / "quoted \"path.mp4").string(), 30), "open normal MP4");
    Require(muxer.Write(packet), "write normal MP4");
    Require(muxer.Close(&why), "EOS success depends on caller error contents");
    Require(muxer.Close(), "idempotent successful close");
    for (auto injected : {Fault::Timeout, Fault::Pipeline, Fault::EndStream}) {
      for (bool with_error : {false, true}) {
        Require(muxer.Open((directory / "fault.mp4").string(), 30), "open fault MP4");
        Require(muxer.Write(packet), "write fault fixture");
        fault = injected;
        why.clear();
        Require(!muxer.Close(with_error ? &why : nullptr), "failed close reported success");
        if (with_error) Require(!why.empty(), "missing failure diagnostic");
        fault = Fault::None;
        Require(!muxer.Close(), "close failure lost during cleanup");
      }
    }

    EventVideoRecorderConfig config;
    config.output_directory = directory;
    EventVideoRecorder recorder(config);
    Require(recorder.Append(packet), "append pre-roll");
    std::string output;
    Require(recorder.Trigger("good", 0, &output), "trigger normal event");
    Require(!std::filesystem::exists(output), "published before EOS");
    Require(std::filesystem::exists(output + ".incomplete"), "missing staging file");
    Require(recorder.Finish(&why), "finish normal event");
    Require(std::filesystem::exists(output) && !std::filesystem::exists(output + ".incomplete"),
            "MP4 not atomically published");
    auto* decode = gst_parse_launch(
        "filesrc name=input ! qtdemux ! h264parse ! openh264dec ! fakesink", nullptr);
    Require(decode, "create MP4 decoder");
    auto* source = gst_bin_get_by_name(GST_BIN(decode), "input");
    g_object_set(source, "location", output.c_str(), nullptr);
    Require(gst_element_set_state(decode, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
            "start MP4 decoder");
    auto* bus = gst_element_get_bus(decode);
    auto* decoded = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    Require(decoded && GST_MESSAGE_TYPE(decoded) == GST_MESSAGE_EOS, "published MP4 cannot decode");
    gst_message_unref(decoded);
    gst_object_unref(bus);
    gst_element_set_state(decode, GST_STATE_NULL);
    gst_object_unref(source);
    gst_object_unref(decode);
    Require(!recorder.Trigger("good", 0), "overwrote existing event");
    for (auto injected : {Fault::Timeout, Fault::Pipeline, Fault::EndStream, Fault::Write}) {
      const auto id = "bad" + std::to_string(static_cast<int>(injected));
      Require(recorder.Trigger(id, 0, &output), "trigger failed event");
      fault = injected;
      if (injected == Fault::Write) {
        auto next = packet;
        next.pts = next.dts = 1;
        Require(!recorder.Append(next), "write failure ignored");
      }
      Require(!recorder.Finish(), "failed event reported success");
      fault = Fault::None;
      Require(!recorder.Finish(), "event lost sticky failure");
      Require(!recorder.active() && !std::filesystem::exists(output), "published failed event");
      Require(std::filesystem::exists(output + ".incomplete"), "failure evidence missing");
    }
    fault = Fault::Write;
    Require(!recorder.Trigger("preroll-failure", 0), "pre-roll failure ignored");
    fault = Fault::None;
    Require(!recorder.Finish() && !std::filesystem::exists(directory / "preroll-failure.mp4"),
            "pre-roll failure became success");
    Require(recorder.Trigger("recovered", 0, nullptr, &why), why);
    Require(recorder.Finish(&why), why);

    Require(recorder.Trigger("missing-file", 0, &output), "trigger missing-file event");
    std::filesystem::remove(output + ".incomplete");
    Require(!recorder.Finish() && !std::filesystem::exists(output),
            "EOS alone published an absent file");
    Require(recorder.Trigger("collision", 0, &output), "trigger collision event");
    std::ofstream(output) << "existing";
    Require(!recorder.Finish(), "publication overwrote a concurrent final file");
    std::ifstream existing(output);
    std::string sentinel;
    existing >> sentinel;
    Require(sentinel == "existing", "existing final file was modified");

    // A lost history GOP must not suppress live writes to an already open event.
    config.max_history_bytes = packet.data.size();
    EventVideoRecorder bounded(config);
    Require(bounded.Append(packet) && bounded.Trigger("bounded", 0), "bounded event trigger");
    packet.keyframe = false;
    packet.pts = packet.dts = 1;
    const auto writes_before_eviction = write_calls;
    Require(bounded.Append(packet) && bounded.HistoryStartPts() == -1, "history did not reset");
    Require(write_calls == writes_before_eviction + 1, "history eviction suppressed live write");
    Require(bounded.Finish(), "history limit broke live recording");
    packet.keyframe = true;
    packet.pts = packet.dts = 0;

    // Service must retire a failed pending video, never index it on Poll/Stop.
    for (auto injected : {Fault::Write, Fault::Timeout, Fault::Pipeline, Fault::EndStream}) {
      const auto sessions = directory / ("sessions-" + std::to_string(static_cast<int>(injected)));
      RecordingService service(sessions, "test", {}, {}, {}, config);
      Require(service.Initialize(&why) && service.Start("test", &why), "start service");
      packet.pts = packet.dts = 0;
      Require(service.HandleVideoPacket(packet, false, &why), "service pre-roll");
      std::string path;
      Require(service.TriggerEventVideo("failure", 1, "manual", &path, &why), "service trigger");
      fault = injected;
      if (injected == Fault::Write) {
        packet.pts = packet.dts = 1;
        Require(!service.HandleVideoPacket(packet, false, nullptr),
                "service swallowed write error");
      } else {
        Require(!service.Stop(nullptr), "service swallowed close error");
      }
      fault = Fault::None;
      service.PollVideo();
      Require(service.Stop(&why), "failed video prevents session cleanup");
      bool failed_event = false;
      for (const auto& entry : std::filesystem::recursive_directory_iterator(sessions)) {
        if (!entry.is_regular_file()) continue;
        Require(entry.path().extension() != ".mp4", "service published failed MP4");
        if (entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        Require(text.find("camera_event_video") == std::string::npos, "indexed incomplete MP4");
        Require(text.find("/recording/video/complete") == std::string::npos,
                "emitted success for failed video");
        failed_event |= text.find("/recording/video/failed") != std::string::npos;
      }
      Require(failed_event, "missing failure event");
    }
    std::filesystem::remove_all(directory);
    std::cout << "EOS/error/timeout, temporary publication, sticky write failure and service "
                 "cleanup passed\n";
    return 0;
  } catch (const std::exception& e) {
    fault = Fault::None;
    std::cerr << e.what() << "; evidence: " << directory << '\n';
    return 1;
  }
}

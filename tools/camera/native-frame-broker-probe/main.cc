#include <fcntl.h>
#include <sys/resource.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/driver/camera/frames/latest_frame_buffer.h"
#include "cockpit/driver/camera/transform/vic_transform.h"
#include "tools/camera/camera-diag/diagnostics.h"

namespace camera = cockpit::camera;
namespace diag = cockpit::camera_diag;
namespace {
void Require(bool ok, const std::string& reason) {
  if (!ok) throw std::runtime_error(reason);
}
struct Worker {
  std::uint64_t frames = 0, last_capture_id = 0, preview_frames = 0;
  std::string error;
  camera::VicTransform vic;
};
diag::Json Queue(const camera::FrameQueueStats& stats) {
  return diag::Object({{"enqueued", diag::Number(stats.enqueued)},
                       {"dequeued", diag::Number(stats.dequeued)},
                       {"dropped", diag::Number(stats.dropped)},
                       {"depth", diag::Number(stats.depth)},
                       {"high_watermark", diag::Number(stats.high_watermark)}});
}
}  // namespace
int main(int argc, char** argv) {
  int frames = 3000, cycles = 1, slow_ms = 100;
  bool convert = false;
  std::string output = "native_frame_broker.json";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") {
      std::cout << "native-frame-broker-probe [--frames 3000] [--cycles 1] [--slow-ms 100] [--mode "
                   "metadata|vic] [--output report.json]\n";
      return 0;
    }
    if (++i == argc) return 2;
    const std::string value = argv[i];
    if (arg == "--output")
      output = value;
    else if (arg == "--mode") {
      if (value != "metadata" && value != "vic") return 2;
      convert = value == "vic";
    } else {
      int number;
      try {
        std::size_t end;
        number = std::stoi(value, &end);
        if (end != value.size() || number < 0) return 2;
      } catch (...) {
        return 2;
      }
      if (arg == "--frames")
        frames = number;
      else if (arg == "--cycles")
        cycles = number;
      else if (arg == "--slow-ms")
        slow_ms = number;
      else
        return 2;
    }
  }
  if (frames < 2 || cycles < 1 || slow_ms > 1000) return 2;
  std::ofstream metadata(output + ".frames.tsv");
  metadata << "cycle\tcapture_id\tsensor_timestamp_ns\tsession_id\tbuffer_index\tfd\n";
  if (!metadata) return 2;
  camera::NativeArgusCapture capture;
  camera::NativeFrameBroker broker;
  camera::LatestFrameBuffer latest;
  std::vector<diag::Json> runs;
  bool passed = true;
  const auto start = std::chrono::steady_clock::now();
  rusage before{};
  getrusage(RUSAGE_SELF, &before);
  for (int cycle = 0; cycle < cycles; ++cycle) {
    diag::FrameStats frame_stats;
    Worker preview, transform;
    std::string error;
    std::atomic<bool> done{false};
    std::thread preview_thread, transform_thread;
    std::uint64_t session = 0;
    bool held_stop = false, revoked = false;
    std::uint64_t no_buffer = 0, budget_drops = 0;
    camera::SharedFrameLease held;
    const auto run_start = std::chrono::steady_clock::now();
    auto consume = [&](camera::FrameConsumer consumer, Worker& worker) {
      try {
        while (!done) {
          auto shared = broker.WaitPop(consumer, 20);
          if (!shared) continue;
          const auto frame = shared.frame();
          Require(frame.session_id == session && frame.capture_id > worker.last_capture_id &&
                      fcntl(frame.dmabuf_fd, F_GETFD) >= 0,
                  "consumer received stale/reused frame");
          if (convert) {
            camera::CpuVideoFrame cpu;
            camera::VicTransformConfig config;
            config.format = consumer == camera::FrameConsumer::kPreview
                                ? camera::VicOutputFormat::kBgrx
                                : camera::VicOutputFormat::kNv12;
            std::string conversion_error;
            Require(worker.vic.Convert(shared, config, &cpu, &conversion_error), conversion_error);
            Require(cpu.capture_id == frame.capture_id && cpu.session_id == frame.session_id,
                    "converted metadata mismatch");
            if (consumer == camera::FrameConsumer::kPreview) {
              camera::CameraFrame legacy;
              Require(camera::TakeCameraFrame(std::move(cpu), &legacy, &conversion_error),
                      conversion_error);
              Require(latest.Publish(std::move(legacy)), "LatestFrameBuffer rejected BGRx");
              ++worker.preview_frames;
            }
          }
          worker.last_capture_id = frame.capture_id;
          ++worker.frames;
          if (consumer == camera::FrameConsumer::kTransform && slow_ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(slow_ms));
        }
      } catch (const std::exception& exception) {
        worker.error = exception.what();
      }
    };
    try {
      Require(capture.Start({}, &error), error);
      camera::FrameLease first;
      Require(capture.Acquire(&first, 5000, &error) == camera::NativeAcquireStatus::kFrame, error);
      session = first.frame().session_id;
      Require(broker.BeginSession(session), "begin broker session");
      preview_thread = std::thread(consume, camera::FrameConsumer::kPreview, std::ref(preview));
      transform_thread =
          std::thread(consume, camera::FrameConsumer::kTransform, std::ref(transform));
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(frames / 30 * 2 + 15);
      while (frame_stats.frames < static_cast<std::uint64_t>(frames)) {
        Require(std::chrono::steady_clock::now() < deadline, "capture deadline exceeded");
        camera::FrameLease lease = std::move(first);
        if (!lease) {
          const auto status = capture.Acquire(&lease, 5000, &error);
          if (status == camera::NativeAcquireStatus::kNoBuffer) {
            ++no_buffer;
            continue;
          }
          Require(status == camera::NativeAcquireStatus::kFrame, error);
        }
        const auto frame = lease.frame();
        if (!frame_stats.frames) {
          std::uint64_t sample = 0;
          Require(capture.SampleLuma(lease, 64, &sample, &error),
                  error);  // Same workload as PR-03.
        }
        frame_stats.Observe(frame.capture_id, frame.sensor_timestamp_ns, false);
        metadata << cycle << '\t' << frame.capture_id << '\t' << frame.sensor_timestamp_ns << '\t'
                 << frame.session_id << '\t' << frame.buffer_index << '\t' << frame.dmabuf_fd
                 << '\n';
        Require(static_cast<bool>(metadata), "write metadata");
        if (frame_stats.frames == static_cast<std::uint64_t>(frames)) {
          done = true;
          preview_thread.join();
          transform_thread.join();
        }
        const auto status = broker.Publish(std::move(lease));
        if (status == camera::FramePublishStatus::kBudgetDropped)
          ++budget_drops;
        else
          Require(status == camera::FramePublishStatus::kPublished,
                  "broker rejected current session");
      }
      // The measured final frame was published after workers joined, so this hold cannot race a
      // pop.
      held = broker.TryPop(camera::FrameConsumer::kPreview);
      if (!held) held = broker.TryPop(camera::FrameConsumer::kTransform);
      Require(static_cast<bool>(held), "no shared hold for Stop check");
      std::string held_error;
      held_stop = capture.Stop(&held_error) == camera::NativeStopStatus::kLeasesOutstanding;
      Require(held_stop, "Stop did not retain consumer-owned storage");
    } catch (const std::exception& exception) {
      error = exception.what();
    }
    done = true;
    if (preview_thread.joinable()) preview_thread.join();
    if (transform_thread.joinable()) transform_thread.join();
    broker.Close();
    if (held) error += " shared frame remained valid after Close";
    try {
      held.frame();
    } catch (const std::logic_error&) {
      revoked = true;
    }
    held.Reset();
    std::string stop_error;
    const bool stopped = capture.Stop(&stop_error) == camera::NativeStopStatus::kStopped;
    const bool outputs_freed = preview.vic.Reset() && transform.vic.Reset();
    const auto stats = capture.stats();
    const auto queues = broker.stats();
    const bool ok = frame_stats.frames == static_cast<std::uint64_t>(frames) &&
                    frame_stats.healthy() && frame_stats.fps() >= 28.5 &&
                    frame_stats.fps() <= 31.5 && no_buffer == 0 && held_stop && revoked &&
                    stopped && outputs_freed && stats.ResourcesBalanced() && error.empty() &&
                    preview.error.empty() && transform.error.empty() && preview.frames > 0 &&
                    transform.frames > 0 && (!convert || preview.preview_frames > 0);
    passed = passed && ok;
    runs.push_back(diag::Object(
        {{"cycle", diag::Number(cycle)},
         {"passed", diag::Boolean(ok)},
         {"session_id", diag::Number(session)},
         {"frames", diag::Number(frame_stats.frames)},
         {"sensor_fps", diag::Number(frame_stats.fps())},
         {"capture_id_gaps", diag::Number(frame_stats.sequence_gaps)},
         {"timestamp_errors", diag::Number(frame_stats.timestamp_errors)},
         {"preview_frames", diag::Number(preview.frames)},
         {"transform_frames", diag::Number(transform.frames)},
         {"no_buffer", diag::Number(no_buffer)},
         {"budget_drops", diag::Number(budget_drops)},
         {"held_stop", diag::Boolean(held_stop)},
         {"revoked_on_close", diag::Boolean(revoked)},
         {"retained_after_close", diag::Number(queues.retained_frames)},
         {"preview_queue", Queue(queues.preview)},
         {"transform_queue", Queue(queues.transform)},
         {"peak_retained", diag::Number(queues.peak_retained_frames)},
         {"vic_preview_frames", diag::Number(preview.vic.stats().transformed)},
         {"vic_transform_frames", diag::Number(transform.vic.stats().transformed)},
         {"vic_outputs_balanced", diag::Boolean(preview.vic.stats().ResourcesBalanced() &&
                                                transform.vic.stats().ResourcesBalanced())},
         {"error", diag::Quote(error + (stop_error.empty() ? "" : " stop: " + stop_error))},
         {"preview_error", diag::Quote(preview.error)},
         {"transform_error", diag::Quote(transform.error)},
         {"wall_ms", diag::Number(std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - run_start)
                                      .count())}}));
    latest.Clear();
    if (!ok) break;
  }
  metadata.close();
  rusage after{};
  getrusage(RUSAGE_SELF, &after);
  const auto cpu = [](const rusage& r) {
    return (r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1000.0 +
           (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1000.0;
  };
  const double wall =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const auto stats = capture.stats();
  const auto report =
      diag::Object({{"schema_version", "1"},
                    {"passed", diag::Boolean(passed && static_cast<bool>(metadata))},
                    {"mode", diag::Quote(convert ? "vic_bgrx_nv12" : "metadata_only")},
                    {"slow_ms", diag::Number(slow_ms)},
                    {"runs", diag::Array(runs)},
                    {"allocated", diag::Number(stats.allocated)},
                    {"destroyed", diag::Number(stats.destroyed)},
                    {"frames_acquired", diag::Number(stats.frames_acquired)},
                    {"leases_returned", diag::Number(stats.leases_returned)},
                    {"buffers_released", diag::Number(stats.buffers_released)},
                    {"outstanding_leases", diag::Number(stats.outstanding_leases)},
                    {"resources_balanced", diag::Boolean(stats.ResourcesBalanced())},
                    {"cleanup_errors", diag::Strings(stats.cleanup_errors)},
                    {"process_cpu_percent", diag::Number((cpu(after) - cpu(before)) / wall * 100)},
                    {"process_wall_ms", diag::Number(wall)},
                    {"max_rss_kib", diag::Number(after.ru_maxrss)}});
  if (!diag::WriteReport(output, report)) return 2;
  std::cout << "passed=" << passed << " report=" << output << '\n';
  return passed ? 0 : 1;
}

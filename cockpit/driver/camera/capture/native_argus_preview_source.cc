#include "cockpit/driver/camera/capture/native_argus_preview_source.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cockpit/driver/camera/tuning/tuning_profile.h"
#include "cockpit/exchange/foundation/time/time.h"

namespace cockpit::camera {
namespace {
void Error(std::string* error, const std::string& why) {
  if (error) *error = why;
}
void Require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
struct Run {
  NativeArgusCapture capture;
  NativeFrameBroker broker;
  VicTransform vic;
  VicTransform encode_vic;
  std::unique_ptr<H264Encoder> encoder;
  std::unique_ptr<H264UdpPublisher> udp;
  H264FileWriter writer;
  bool encoding = false;
  std::atomic<bool> running{false};
  std::thread producer, preview, encode;
  mutable std::mutex mutex;
  NativePreviewStatus status;
  std::deque<double> latencies;
  void Fail(const std::string& message) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      status.error = message;
    }
    running = false;
  }
  void Produce() {
    int timeouts = 0;
    try {
      while (running) {
        FrameLease lease;
        std::string error;
        const auto result = capture.Acquire(&lease, 100, &error);
        if (result == NativeAcquireStatus::kTimeout) {
          if (++timeouts >= 20) throw std::runtime_error("native preview stalled: " + error);
          continue;
        }
        Require(result == NativeAcquireStatus::kFrame, error);
        timeouts = 0;
        if (!running) break;
        const auto published = broker.Publish(std::move(lease));
        Require(published == FramePublishStatus::kPublished ||
                    published == FramePublishStatus::kBudgetDropped,
                "native preview broker rejected frame");
        if (!encoding) {
          while (auto unused = broker.TryPop(FrameConsumer::kTransform)) unused.Reset();
        }
        std::this_thread::yield();
      }
    } catch (const std::exception& e) {
      Fail(e.what());
    }
  }
  void Preview(CameraPreviewConfig config, CameraPreviewSource::FrameCallback callback) {
    try {
      while (running) {
        auto lease = broker.WaitPop(FrameConsumer::kPreview, 100);
        if (!lease) continue;
        const auto metadata = lease.frame();
        CpuVideoFrame cpu;
        std::string error;
        Require(
            vic.Convert(lease, {config.width, config.height, VicOutputFormat::kBgrx}, &cpu, &error),
            error);
        lease.Reset();  // CPU owns all pixels before shared-memory/Qt delivery.
        CameraFrame frame;
        Require(TakeCameraFrame(std::move(cpu), &frame, &error), error);
        callback(std::move(frame));
        const double ms =
            (time::SteadyTime::Now().ToNanoseconds() - metadata.received_steady_ns) / 1000000.0;
        std::lock_guard<std::mutex> lock(mutex);
        ++status.frames_delivered;
        status.last_delivered_capture_id = metadata.capture_id;
        status.last_delivered_tuning = metadata.tuning;
        ++status.latency_samples;
        if (latencies.size() == 4096) latencies.pop_front();
        latencies.push_back(std::max(0.0, ms));
      }
    } catch (const std::exception& e) {
      Fail(e.what());
    }
  }
  void Encode(CameraPreviewConfig config) {
    try {
      while (running) {
        auto lease = broker.WaitPop(FrameConsumer::kTransform, 100);
        if (!lease) continue;
        CpuVideoFrame cpu;
        std::string error;
        Require(encode_vic.Convert(lease,
                                   {config.h264_width, config.h264_height, VicOutputFormat::kI420},
                                   &cpu, &error),
                error);
        lease.Reset();
        std::vector<H264Packet> packets;
        Require(encoder->Encode(cpu, &packets, &error), error);
        for (const auto& packet : packets) {
          if (config.h264_output_path.size()) Require(writer.Write(packet, &error), error);
          if (udp) Require(udp->Publish(packet, &error), error);
        }
      }
      std::vector<H264Packet> packets;
      Require(encoder->Flush(&packets, nullptr), "H264 flush failed");
      for (const auto& packet : packets) {
        std::string error;
        if (config.h264_output_path.size()) Require(writer.Write(packet, &error), error);
        if (udp) Require(udp->Publish(packet, &error), error);
      }
      std::string error;
      Require(writer.Close(&error), error);
    } catch (const std::exception& e) {
      Fail(e.what());
    }
  }
};
}  // namespace
struct NativeArgusPreviewSource::Impl {
  mutable std::mutex lifecycle;
  std::shared_ptr<Run> run;
  void Stop() {
    if (!run) return;
    run->running = false;
    if (run->producer.joinable()) run->producer.join();
    if (run->preview.joinable()) run->preview.join();
    if (run->encode.joinable()) run->encode.join();
    // Close only after device access has finished: Close revokes shared handles.
    run->broker.Close();
    std::string error;
    if (!run->vic.Reset(&error)) run->Fail(error);
    if (!run->encode_vic.Reset(&error)) run->Fail(error);
    if (run->capture.Stop(&error) != NativeStopStatus::kStopped) run->Fail(error);
  }
  bool Check(std::uint64_t session, std::uint32_t revision, std::string* error) const {
    if (!run || !run->running) {
      Error(error, "native preview is not running");
      return false;
    }
    CameraTuningState state;
    if (!run->capture.GetTuning(&state, error)) return false;
    if (!session || !revision || session != state.session_id || revision != state.revision) {
      Error(error, "stale native session/revision; query tuning again");
      return false;
    }
    return true;
  }
};
NativeArgusPreviewSource::NativeArgusPreviewSource() : impl_(std::make_unique<Impl>()) {
}
NativeArgusPreviewSource::~NativeArgusPreviewSource() {
  Stop();
}
bool NativeArgusPreviewSource::Start(const CameraPreviewConfig& config, FrameCallback callback,
                                     std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  impl_->Stop();
  try {
    Require(NativeArgusCapture::IsSupported() && VicTransform::IsSupported(),
            "native Argus/VIC SDK unavailable");
    Require(callback && config.width && config.height && config.fps &&
                config.output_format == CameraPixelFormat::kBgrx,
            "native preview requires positive dimensions/FPS and BGRx callback");
    constexpr const char* prefix = "nvargus://";
    Require(config.device.rfind(prefix, 0) == 0, "native preview device must be nvargus://<index>");
    const auto index = config.device.substr(std::char_traits<char>::length(prefix));
    std::uint32_t device = 0;
    const auto parsed = std::from_chars(index.data(), index.data() + index.size(), device);
    Require(!index.empty() && parsed.ec == std::errc{} && parsed.ptr == index.data() + index.size(),
            "invalid native sensor index");
    TuningProfile profile;
    if (!config.tuning_profile.empty())
      Require(LoadTuningProfile(config.tuning_profile, &profile, error),
              error ? *error : "load native profile failed");
    auto next = std::make_shared<Run>();
    impl_->run = next;  // Stop owns every resource even if initialization fails.
    NativeCaptureConfig native;
    native.device_index = device;
    native.width = config.width;
    native.height = config.height;
    native.fps = config.fps;
    Require(next->capture.Start(native, error), error ? *error : "native start failed");
    CameraTuningState state;
    if (!config.tuning_profile.empty())
      Require(next->capture.ApplyTuningProfile(profile, nullptr, error),
              error ? *error : "native profile rejected");
    Require(next->capture.GetTuning(&state, error), error ? *error : "native state unavailable");
    if (!config.h264_output_path.empty() || config.h264_udp_port != 0) {
      Require(H264Encoder::IsSupported(), "H264 encoder dependencies unavailable");
      H264EncoderConfig encoder_config;
      encoder_config.width = config.h264_width;
      encoder_config.height = config.h264_height;
      encoder_config.fps = config.fps;
      encoder_config.bitrate = config.h264_bitrate;
      encoder_config.gop = config.h264_gop;
      next->encoder = std::make_unique<H264Encoder>();
      Require(next->encoder->Start(encoder_config, error), error ? *error : "H264 start failed");
      if (!config.h264_output_path.empty())
        Require(next->writer.Open(config.h264_output_path, error),
                error ? *error : "H264 output open failed");
      if (config.h264_udp_port != 0) {
        next->udp = std::make_unique<H264UdpPublisher>();
        Require(next->udp->Open({config.h264_udp_host, config.h264_udp_port, 1200}, error),
                error ? *error : "H264 UDP publisher open failed");
      }
      next->encoding = true;
    }
    next->status.session_id = state.session_id;
    Require(next->broker.BeginSession(state.session_id), "begin native preview broker session");
    next->running = true;
    next->producer = std::thread([next] {
      next->Produce();
    });
    next->preview = std::thread([next, config, callback = std::move(callback)] {
      next->Preview(config, callback);
    });
    if (next->encoding) {
      next->encode = std::thread([next, config] {
        next->Encode(config);
      });
    }
    return true;
  } catch (const std::exception& e) {
    Error(error, e.what());
    impl_->Stop();
    return false;
  }
}
void NativeArgusPreviewSource::Stop() {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  impl_->Stop();
}
bool NativeArgusPreviewSource::IsRunning() const {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  return impl_->run && impl_->run->running;
}
bool NativeArgusPreviewSource::GetTuning(CameraTuningState* state, std::string* error) const {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  if (!impl_->run || !impl_->run->running) {
    Error(error, "native preview is not running");
    return false;
  }
  return impl_->run->capture.GetTuning(state, error);
}
bool NativeArgusPreviewSource::SetTuning(std::uint64_t session, std::uint32_t revision,
                                         const CameraTuning& settings, CameraTuningState* state,
                                         std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  return impl_->Check(session, revision, error) &&
         impl_->run->capture.SetTuning(settings, nullptr, error) &&
         impl_->run->capture.GetTuning(state, error);
}
bool NativeArgusPreviewSource::ResetTuning(std::uint64_t session, std::uint32_t revision,
                                           CameraTuningState* state, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  return impl_->Check(session, revision, error) &&
         impl_->run->capture.ResetTuning(nullptr, error) &&
         impl_->run->capture.GetTuning(state, error);
}
bool NativeArgusPreviewSource::ApplyTuningProfile(std::uint64_t session, std::uint32_t revision,
                                                  const TuningProfile& profile,
                                                  CameraTuningState* state, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  return impl_->Check(session, revision, error) &&
         impl_->run->capture.ApplyTuningProfile(profile, nullptr, error) &&
         impl_->run->capture.GetTuning(state, error);
}
bool NativeArgusPreviewSource::GetNativeStatus(NativePreviewStatus* status) const {
  if (!status) return false;
  std::lock_guard<std::mutex> lock(impl_->lifecycle);
  if (!impl_->run) {
    *status = {};
    return true;
  }
  auto& run = *impl_->run;
  {
    std::lock_guard<std::mutex> state(run.mutex);
    *status = run.status;
    if (!run.latencies.empty()) {
      std::vector<double> values(run.latencies.begin(), run.latencies.end());
      std::sort(values.begin(), values.end());
      status->delivery_p95_ms = values[(values.size() - 1) * 95 / 100];
    }
  }
  status->running = run.running;
  status->capture = run.capture.stats();
  status->broker = run.broker.stats();
  status->vic = run.vic.stats();
  status->encode_vic = run.encode_vic.stats();
  if (run.encoder) status->encoder = run.encoder->stats();
  if (run.udp) status->udp = run.udp->stats();
  return true;
}
}  // namespace cockpit::camera

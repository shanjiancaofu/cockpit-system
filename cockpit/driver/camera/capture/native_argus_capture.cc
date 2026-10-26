#include "cockpit/driver/camera/capture/native_argus_capture.h"

#include <Argus/Argus.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <nvbufsurface.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "cockpit/exchange/foundation/time/time.h"

namespace cockpit::camera {
namespace {
using namespace Argus;

void SetError(std::string* error, const std::string& text) {
  if (error) *error = text;
}
void Require(bool ok, const std::string& action) {
  if (!ok) throw std::runtime_error(action);
}
void Check(Status status, const std::string& action) {
  Require(status == STATUS_OK, action + " status=" + std::to_string(status));
}
struct Counters {
  mutable std::mutex mutex;
  NativeCaptureStats value;
  template <class F>
  void Update(F update) {
    std::lock_guard<std::mutex> lock(mutex);
    update(value);
  }
  NativeCaptureStats Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return value;
  }
  void Error(const std::string& text) {
    Update([&](auto& stats) {
      stats.cleanup_errors.push_back(text);
    });
  }
};
struct EglDisplay {
  EGLDisplay value = EGL_NO_DISPLAY;
  void Initialize() {
    value = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    Require(value != EGL_NO_DISPLAY, "eglGetDisplay");
    if (!eglInitialize(value, nullptr, nullptr)) {
      value = EGL_NO_DISPLAY;
      throw std::runtime_error("eglInitialize failed; configure NVIDIA EGL");
    }
  }
  ~EglDisplay() {
    if (value != EGL_NO_DISPLAY) eglTerminate(value);
  }
};
enum class SlotState { kInArgus, kLeased, kPendingReturn, kReleaseFailed };
struct Slot {
  explicit Slot(std::shared_ptr<Counters> counters) : counters(std::move(counters)) {
  }
  std::shared_ptr<Counters> counters;
  NvBufSurface* surface = nullptr;
  UniqueObj<Buffer> buffer;
  bool egl_mapped = false, cpu_mapped = false;
  SlotState state = SlotState::kInArgus;
  bool UnmapCpu() {
    if (!cpu_mapped) return true;
    if (NvBufSurfaceUnMap(surface, 0, -1) != 0) {
      counters->Error("NvBufSurfaceUnMap CPU");
      return false;
    }
    cpu_mapped = false;
    counters->Update([](auto& stats) {
      ++stats.cpu_unmapped;
    });
    return true;
  }
  ~Slot() {
    buffer.reset();
    // Do not free a possible DMA target after an unsuccessful driver shutdown.
    if (counters->Snapshot().native_resources_retained_until_exit) return;
    UnmapCpu();
    if (egl_mapped) {
      if (NvBufSurfaceUnMapEglImage(surface, 0) == 0)
        counters->Update([](auto& stats) {
          ++stats.egl_unmapped;
        });
      else
        counters->Error("NvBufSurfaceUnMapEglImage");
    }
    if (surface) {
      if (NvBufSurfaceDestroy(surface) == 0)
        counters->Update([](auto& stats) {
          ++stats.destroyed;
        });
      else
        counters->Error("NvBufSurfaceDestroy");
    }
  }
};
std::atomic<std::uint64_t> next_session_id{1};

const std::array<std::pair<const char*, AeMode>, 2> ae_modes{
    {{"auto", AE_MODE_ON}, {"manual", AE_MODE_OFF}}};
const std::array<std::pair<const char*, AwbMode>, 10> awb_modes{
    {{"off", AWB_MODE_OFF},
     {"auto", AWB_MODE_AUTO},
     {"incandescent", AWB_MODE_INCANDESCENT},
     {"fluorescent", AWB_MODE_FLUORESCENT},
     {"warm-fluorescent", AWB_MODE_WARM_FLUORESCENT},
     {"daylight", AWB_MODE_DAYLIGHT},
     {"cloudy", AWB_MODE_CLOUDY_DAYLIGHT},
     {"twilight", AWB_MODE_TWILIGHT},
     {"shade", AWB_MODE_SHADE},
     {"manual", AWB_MODE_MANUAL}}};
const std::array<std::pair<const char*, AeAntibandingMode>, 4> antibanding_modes{
    {{"off", AE_ANTIBANDING_MODE_OFF},
     {"auto", AE_ANTIBANDING_MODE_AUTO},
     {"50hz", AE_ANTIBANDING_MODE_50HZ},
     {"60hz", AE_ANTIBANDING_MODE_60HZ}}};
template <class T, std::size_t N>
T Mode(const std::string& name, const std::array<std::pair<const char*, T>, N>& modes) {
  for (const auto& entry : modes)
    if (name == entry.first) return entry.second;
  throw std::runtime_error("unknown tuning mode: " + name);
}
template <class T, std::size_t N>
std::string Name(const T& mode, const std::array<std::pair<const char*, T>, N>& modes) {
  for (const auto& entry : modes)
    if (mode == entry.second) return entry.first;
  return "unknown";
}
template <class T>
TuningRange<T> Bounds(const Range<T>& range) {
  return {range.min(), range.max()};
}
template <class T>
TuningRange<T> Fit(TuningRange<T> requested, TuningRange<T> limits) {
  requested.min = std::max(requested.min, limits.min);
  requested.max = std::min(requested.max, limits.max);
  return requested.min <= requested.max ? requested : limits;
}

// Shared only with leases. Argus operations are executed by the serialized
// controller; return callbacks merely enqueue indices under returns_mutex.
struct Session : std::enable_shared_from_this<Session> {
  Session(std::shared_ptr<Counters> counters, NativeCaptureConfig config)
      : counters(std::move(counters)), config(config), id(next_session_id.fetch_add(1)) {
  }
  std::shared_ptr<Counters> counters;
  NativeCaptureConfig config;
  const std::uint64_t id;
  EglDisplay display;
  UniqueObj<CameraProvider> provider;
  UniqueObj<CaptureSession> session;
  UniqueObj<OutputStream> stream;
  std::vector<std::unique_ptr<Slot>> slots;
  UniqueObj<Request> request;
  ICaptureSession* capture = nullptr;
  IBufferOutputStream* output = nullptr;
  bool active = false, stopping = false, closed = false;
  int mode_index = -1;
  std::string version;
  SensorMode* selected_mode = nullptr;
  CameraTuningState tuning;
  std::uint64_t next_tuning_revision = 2;
  std::string tuning_fault;
  std::mutex returns_mutex;
  std::vector<std::uint32_t> returns;

  ~Session() {
    Stop(nullptr);
  }

  void Initialize() {
    returns.reserve(config.buffer_count);
    display.Initialize();
    provider.reset(CameraProvider::create());
    auto* camera = interface_cast<ICameraProvider>(provider);
    Require(camera, "CameraProvider::create");
    version = camera->getVersion();
    std::vector<CameraDevice*> devices;
    Check(camera->getCameraDevices(&devices), "getCameraDevices");
    Require(config.device_index < devices.size(), "Argus camera index unavailable");
    CameraDevice* device = devices[config.device_index];
    auto* properties = interface_cast<ICameraProperties>(device);
    Require(properties, "ICameraProperties");
    std::vector<SensorMode*> modes;
    Check(properties->getAllSensorModes(&modes), "getAllSensorModes");
    const std::uint64_t duration = (1000000000ULL + config.fps - 1) / config.fps;
    SensorMode* mode = nullptr;
    for (std::size_t i = 0; i < modes.size(); ++i) {
      auto* info = interface_cast<ISensorMode>(modes[i]);
      if (info && info->getResolution() == Size2D<uint32_t>(config.width, config.height) &&
          duration >= info->getFrameDurationRange().min() &&
          duration <= info->getFrameDurationRange().max()) {
        mode = modes[i];
        mode_index = static_cast<int>(i);
        break;
      }
    }
    Require(mode, "no sensor mode matching exact dimensions and frame duration");
    selected_mode = mode;
    auto* info = interface_cast<ISensorMode>(mode);
    tuning.session_id = id;
    tuning.capabilities.exposure_ns = Bounds(info->getExposureTimeRange());
    tuning.capabilities.exposure_ns.max = std::min(tuning.capabilities.exposure_ns.max, duration);
    tuning.capabilities.frame_duration_ns = duration;
    tuning.capabilities.sensor_gain = Bounds(info->getAnalogGainRange());
    tuning.capabilities.isp_gain = Bounds(properties->getIspDigitalGainRange());
    tuning.capabilities.exposure_compensation_ev =
        Bounds(properties->getExposureCompensationRange());
    session.reset(camera->createCaptureSession(device));
    capture = interface_cast<ICaptureSession>(session);
    Require(capture, "createCaptureSession");
    UniqueObj<OutputStreamSettings> settings(
        capture->createOutputStreamSettings(STREAM_TYPE_BUFFER));
    auto* buffer_settings = interface_cast<IBufferOutputStreamSettings>(settings);
    Require(buffer_settings, "STREAM_TYPE_BUFFER unsupported");
    Check(buffer_settings->setBufferType(BUFFER_TYPE_EGL_IMAGE), "setBufferType EGL_IMAGE");
    Check(buffer_settings->setSyncType(SYNC_TYPE_NONE), "setSyncType NONE");
    buffer_settings->setMetadataEnable(true);
    stream.reset(capture->createOutputStream(settings.get()));
    output = interface_cast<IBufferOutputStream>(stream);
    Require(output, "create BufferOutputStream");
    UniqueObj<BufferSettings> native_settings(output->createBufferSettings());
    auto* image_settings = interface_cast<IEGLImageBufferSettings>(native_settings);
    Require(image_settings, "IEGLImageBufferSettings");
    Check(image_settings->setEGLDisplay(display.value), "setEGLDisplay");
    for (std::uint32_t i = 0; i < config.buffer_count; ++i) {
      slots.push_back(std::make_unique<Slot>(counters));
      auto& slot = *slots.back();
      NvBufSurfaceAllocateParams params{};
      params.params.width = config.width;
      params.params.height = config.height;
      params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
      params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
      params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
      params.memtag = NvBufSurfaceTag_CAMERA;
      Require(NvBufSurfaceAllocate(&slot.surface, 1, &params) == 0 && slot.surface,
              "NvBufSurfaceAllocate");
      counters->Update([](auto& stats) {
        ++stats.allocated;
      });
      slot.surface->numFilled = 1;
      void* imported = nullptr;
      Require(NvBufSurfaceFromFd(static_cast<int>(slot.surface->surfaceList[0].bufferDesc),
                                 &imported) == 0 &&
                  imported == slot.surface,
              "NvBufSurfaceFromFd identity check");
      counters->Update([](auto& stats) {
        ++stats.imported_fds;
      });
      Require(NvBufSurfaceMapEglImage(slot.surface, 0) == 0, "NvBufSurfaceMapEglImage");
      slot.egl_mapped = true;
      counters->Update([](auto& stats) {
        ++stats.egl_mapped;
      });
      const auto image = static_cast<EGLImageKHR>(slot.surface->surfaceList[0].mappedAddr.eglImage);
      Require(image != EGL_NO_IMAGE_KHR, "NvBufSurface returned no EGLImage");
      Check(image_settings->setEGLImage(image), "setEGLImage");
      slot.buffer.reset(output->createBuffer(native_settings.get()));
      Require(interface_cast<IBuffer>(slot.buffer), "createBuffer");
      Check(output->releaseBuffer(slot.buffer.get()), "release initial buffer");
    }
    request.reset(capture->createRequest(CAPTURE_INTENT_VIDEO_RECORD));
    auto* req = interface_cast<IRequest>(request);
    auto* source = interface_cast<ISourceSettings>(request);
    Require(req && source, "createRequest / ISourceSettings");
    Check(source->setSensorMode(mode), "setSensorMode");
    Check(source->setFrameDurationRange(Range<uint64_t>(duration)), "setFrameDurationRange");
    CameraTuning defaults = ReadTuningRequest(request.get());
    defaults.exposure_ns = Fit(defaults.exposure_ns, tuning.capabilities.exposure_ns);
    defaults.sensor_gain = Fit(defaults.sensor_gain, tuning.capabilities.sensor_gain);
    defaults.isp_gain = Fit(defaults.isp_gain, tuning.capabilities.isp_gain);
    tuning.defaults = defaults;
    std::string why;
    if (!ValidateCameraTuning(defaults, tuning.capabilities, &why))
      throw std::runtime_error("invalid SDK defaults: " + why);
    BuildTuningRequest(request.get(), defaults, 1);
    active = true;  // Failed repeat may still need cancellation.
    Check(capture->repeat(request.get()), "repeat");
    tuning.requested = defaults;
    tuning.accepted = ReadTuningRequest(request.get());
    tuning.revision = 1;
    counters->Update([](auto& stats) {
      ++stats.starts;
    });
  }

  CameraTuning ReadTuningRequest(Request* target) const {
    auto* req = interface_cast<IRequest>(target);
    Require(req, "IRequest for tuning query");
    auto* source = interface_cast<ISourceSettings>(req->getSourceSettings());
    auto* automatic = interface_cast<IAutoControlSettings>(req->getAutoControlSettings());
    Require(source && automatic, "source/automatic tuning query unavailable");
    CameraTuning settings;
    settings.ae_mode = Name(automatic->getAeMode(), ae_modes);
    settings.awb_mode = Name(automatic->getAwbMode(), awb_modes);
    settings.antibanding = Name(automatic->getAeAntibandingMode(), antibanding_modes);
    settings.ae_lock = automatic->getAeLock();
    settings.awb_lock = automatic->getAwbLock();
    settings.exposure_ns = Bounds(source->getExposureTimeRange());
    settings.sensor_gain = Bounds(source->getGainRange());
    settings.isp_gain = Bounds(automatic->getIspDigitalGainRange());
    settings.exposure_compensation_ev = automatic->getExposureCompensation();
    const auto wb = automatic->getWbGains();
    for (std::size_t i = 0; i < 4; ++i) settings.wb_gains[i] = wb[i];
    return settings;
  }

  void BuildTuningRequest(Request* target, const CameraTuning& settings, std::uint32_t revision) {
    auto* req = interface_cast<IRequest>(target);
    Require(req, "IRequest for tuning");
    auto* source = interface_cast<ISourceSettings>(req->getSourceSettings());
    auto* automatic = interface_cast<IAutoControlSettings>(req->getAutoControlSettings());
    Require(source && automatic, "source/automatic tuning interfaces unavailable");
    Check(req->enableOutputStream(stream.get()), "tuning enableOutputStream");
    Check(source->setSensorMode(selected_mode), "tuning setSensorMode");
    Check(source->setFrameDurationRange(Range<uint64_t>(tuning.capabilities.frame_duration_ns)),
          "tuning setFrameDurationRange");
    Check(source->setExposureTimeRange(
              Range<uint64_t>(settings.exposure_ns.min, settings.exposure_ns.max)),
          "tuning setExposureTimeRange");
    Check(source->setGainRange(Range<float>(settings.sensor_gain.min, settings.sensor_gain.max)),
          "tuning setGainRange");
    Check(automatic->setIspDigitalGainRange(
              Range<float>(settings.isp_gain.min, settings.isp_gain.max)),
          "tuning setIspDigitalGainRange");
    Check(automatic->setAeMode(Mode(settings.ae_mode, ae_modes)), "tuning setAeMode");
    Check(automatic->setAeLock(settings.ae_lock), "tuning setAeLock");
    Check(automatic->setExposureCompensation(settings.exposure_compensation_ev),
          "tuning setExposureCompensation");
    Check(automatic->setAeAntibandingMode(Mode(settings.antibanding, antibanding_modes)),
          "tuning setAeAntibandingMode");
    Check(automatic->setAwbMode(Mode(settings.awb_mode, awb_modes)), "tuning setAwbMode");
    Check(automatic->setAwbLock(settings.awb_lock), "tuning setAwbLock");
    if (settings.awb_mode == "manual")
      Check(automatic->setWbGains(BayerTuple<float>(settings.wb_gains[0], settings.wb_gains[1],
                                                    settings.wb_gains[2], settings.wb_gains[3])),
            "tuning setWbGains");
    Check(req->setClientData(revision), "tuning setClientData");
  }

  bool SetTuning(const CameraTuning& settings, std::uint32_t* revision, std::string* error) {
    if (!active || stopping || closed || !tuning_fault.empty()) {
      SetError(error,
               "native tuning unavailable: session stopped or control fault: " + tuning_fault);
      return false;
    }
    if (!ValidateCameraTuning(settings, tuning.capabilities, error)) return false;
    try {
      Require(next_tuning_revision <= std::numeric_limits<std::uint32_t>::max(),
              "tuning revision exhausted");
      CameraTuning next_settings = settings;
      UniqueObj<Request> candidate(capture->createRequest(CAPTURE_INTENT_VIDEO_RECORD));
      Require(candidate, "create tuning request");
      // A failed submission might still have reached the daemon. Never reuse
      // its clientData for a later request with different settings.
      const auto next_revision = static_cast<std::uint32_t>(next_tuning_revision++);
      BuildTuningRequest(candidate.get(), settings, next_revision);
      auto accepted = ReadTuningRequest(candidate.get());
      const auto status = capture->repeat(candidate.get());
      if (status != STATUS_OK) {
        // Preserve the old request/state on any failed update. If restoring the
        // old repeat also fails, reject further acquire/control until Stop.
        if (capture->repeat(request.get()) != STATUS_OK)
          tuning_fault = "failed to restore repeating request";
        throw std::runtime_error("tuning repeat rejected status=" + std::to_string(status) +
                                 (tuning_fault.empty() ? "" : "; " + tuning_fault));
      }
      request.reset(candidate.release());
      tuning.requested = std::move(next_settings);
      tuning.accepted = std::move(accepted);
      tuning.revision = next_revision;
      if (revision) *revision = next_revision;
      return true;
    } catch (const std::exception& exception) {
      SetError(error, exception.what());
      return false;
    }
  }

  void Return(std::uint32_t index) noexcept {
    std::lock_guard<std::mutex> lock(returns_mutex);
    // Only the unique lease can transition this slot back to the return queue.
    slots[index]->state = SlotState::kPendingReturn;
    returns.push_back(index);  // Capacity reserved at initialization, bounded by pool size.
    counters->Update([](auto& stats) {
      --stats.outstanding_leases;
      ++stats.leases_returned;
      ++stats.pending_returns;
    });
  }
  bool FlushReturns() {
    std::array<std::uint32_t, 32> ready{};
    std::size_t count;
    {
      std::lock_guard<std::mutex> lock(returns_mutex);
      count = returns.size();
      std::copy(returns.begin(), returns.end(), ready.begin());
      returns.clear();
    }
    bool ok = true;
    for (std::size_t i = 0; i < count; ++i) {
      auto& slot = *slots[ready[i]];
      const bool unmapped = slot.UnmapCpu();
      const auto status =
          unmapped ? output->releaseBuffer(slot.buffer.get()) : STATUS_INVALID_PARAMS;
      counters->Update([&](auto& stats) {
        --stats.pending_returns;
        if (status == STATUS_OK)
          ++stats.buffers_released;
        else
          ++stats.release_errors;
      });
      if (status == STATUS_OK)
        slot.state = SlotState::kInArgus;
      else {
        slot.state = SlotState::kReleaseFailed;
        counters->Error("release returned buffer status=" + std::to_string(status));
        ok = false;
      }
    }
    return ok;
  }

  NativeAcquireStatus Acquire(FrameLease* lease, int timeout_ms, std::string* error) {
    if (stopping || closed || !active) return NativeAcquireStatus::kStopped;
    if (!tuning_fault.empty()) {
      SetError(error, tuning_fault);
      return NativeAcquireStatus::kError;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
      if (!FlushReturns()) {
        SetError(error, "failed to return previous frame");
        return NativeAcquireStatus::kError;
      }
      const auto held = counters->Snapshot().outstanding_leases;
      if (held == slots.size()) {
        counters->Update([](auto& stats) {
          ++stats.no_buffer;
        });
        SetError(error, "all native buffers are held by consumers");
        return NativeAcquireStatus::kNoBuffer;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 deadline - std::chrono::steady_clock::now())
                                 .count();
      const uint64_t wait = static_cast<uint64_t>(std::max<std::int64_t>(
          1, held ? std::min<std::int64_t>(remaining, 100000000) : remaining));
      Status status = STATUS_OK;
      Buffer* acquired = output->acquireBuffer(wait, &status);
      if (status == STATUS_TIMEOUT) continue;  // Flush asynchronous returns before retry.
      if (status == STATUS_END_OF_STREAM) return NativeAcquireStatus::kStopped;
      if (status != STATUS_OK || !acquired) {
        SetError(error, "acquireBuffer status=" + std::to_string(status));
        return NativeAcquireStatus::kError;
      }
      try {
        std::uint32_t index = 0;
        while (index < slots.size() && slots[index]->buffer.get() != acquired) ++index;
        Require(index < slots.size(), "acquired buffer is not in this session's pool");
        auto* buffer = interface_cast<IBuffer>(acquired);
        const auto* metadata =
            buffer ? interface_cast<const ICaptureMetadata>(buffer->getMetadata()) : nullptr;
        Require(metadata, "missing frame metadata");
        NativeFrame frame;
        frame.session_id = id;
        frame.buffer_index = index;
        frame.dmabuf_fd = static_cast<int>(slots[index]->surface->surfaceList[0].bufferDesc);
        frame.width = config.width;
        frame.height = config.height;
        frame.capture_id = metadata->getCaptureId();
        frame.sensor_timestamp_ns = metadata->getSensorTimestamp();
        frame.frame_duration_ns = metadata->getFrameDuration();
        frame.tuning.valid = true;
        frame.tuning.revision = metadata->getClientData();
        frame.tuning.exposure_ns = metadata->getSensorExposureTime();
        frame.tuning.sensor_gain = metadata->getSensorAnalogGain();
        frame.tuning.isp_gain = metadata->getIspDigitalGain();
        frame.tuning.ae_mode = Name(metadata->getAeMode(), ae_modes);
        frame.tuning.awb_mode = Name(metadata->getAwbMode(), awb_modes);
        frame.tuning.ae_locked = metadata->getAeLocked();
        frame.tuning.awb_locked = metadata->getAwbState() == AWB_STATE_LOCKED;
        frame.tuning.awb_cct = metadata->getAwbCct();
        const auto gains = metadata->getAwbGains();
        for (std::size_t i = 0; i < 4; ++i) frame.tuning.wb_gains[i] = gains[i];
        tuning.last_observed = frame.tuning;
        frame.received_at_ns = time::WallTime::Now().ToNanoseconds();
        frame.received_steady_ns = time::SteadyTime::Now().ToNanoseconds();
        FrameLease next(frame, [self = shared_from_this(), index]() noexcept {
          self->Return(index);
        });
        {
          std::lock_guard<std::mutex> lock(returns_mutex);
          slots[index]->state = SlotState::kLeased;
          counters->Update([](auto& stats) {
            ++stats.frames_acquired;
            ++stats.outstanding_leases;
            stats.peak_leases = std::max(stats.peak_leases, stats.outstanding_leases);
          });
        }
        *lease = std::move(next);
        return NativeAcquireStatus::kFrame;
      } catch (const std::exception& exception) {
        const auto released = output->releaseBuffer(acquired);
        if (released != STATUS_OK) counters->Error("release buffer after acquire error");
        SetError(error, exception.what());
        return NativeAcquireStatus::kError;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    counters->Update([](auto& stats) {
      ++stats.timeouts;
    });
    SetError(error, "native frame acquire timed out");
    return NativeAcquireStatus::kTimeout;
  }

  NativeStopStatus Stop(std::string* error) {
    if (closed)
      return counters->Snapshot().cleanup_errors.empty() ? NativeStopStatus::kStopped
                                                         : NativeStopStatus::kError;
    if (!stopping) {
      stopping = true;
      if (active) {
        capture->stopRepeat();
        const auto eos = output->endOfStream();
        if (eos != STATUS_OK) counters->Error("endOfStream status=" + std::to_string(eos));
      }
    }
    FlushReturns();
    if (counters->Snapshot().outstanding_leases != 0) {
      SetError(error, "native frames are still leased; release them and call Stop again");
      return NativeStopStatus::kLeasesOutstanding;
    }
    // A consumer may have returned its last lease just after the first flush.
    // No new leases can be acquired while stopping; zero held means this flush is final.
    FlushReturns();
    if (active) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(config.shutdown_timeout_ms);
      bool drained = false;
      while (std::chrono::steady_clock::now() < deadline) {
        Status status = STATUS_OK;
        Buffer* buffer = output->acquireBuffer(200000000ULL, &status);
        if (status == STATUS_END_OF_STREAM) {
          drained = true;
          break;
        }
        if (buffer) {
          if (output->releaseBuffer(buffer) != STATUS_OK) {
            counters->Error("releaseBuffer during drain");
            break;
          }
          counters->Update([](auto& stats) {
            ++stats.drained;
          });
        } else if (status != STATUS_TIMEOUT)
          break;
      }
      if (!drained) {
        counters->Error("stream did not drain before shutdown deadline");
        if (capture->cancelRequests() != STATUS_OK)
          counters->Error("cancelRequests after drain failure");
      }
      const auto wait_ns = static_cast<uint64_t>(config.shutdown_timeout_ms) * 1000000ULL;
      auto idle = capture->waitForIdle(wait_ns);
      if (idle != STATUS_OK) {
        counters->Error("waitForIdle status=" + std::to_string(idle));
        if (capture->cancelRequests() != STATUS_OK)
          counters->Error("cancelRequests after idle failure");
        idle = capture->waitForIdle(wait_ns);
        if (idle != STATUS_OK)
          counters->Update([](auto& stats) {
            stats.native_resources_retained_until_exit = true;
          });
      }
    }
    active = false;
    request.reset();
    slots.clear();
    stream.reset();
    session.reset();
    provider.reset();
    output = nullptr;
    capture = nullptr;
    closed = true;
    const auto stats = counters->Snapshot();
    if (!stats.cleanup_errors.empty()) {
      SetError(error, stats.cleanup_errors.back());
      return NativeStopStatus::kError;
    }
    return NativeStopStatus::kStopped;
  }

  bool SampleLuma(const FrameLease& lease, std::size_t bytes, std::uint64_t* sum,
                  std::string* error) {
    if (!lease || !sum || bytes == 0 || closed || lease.frame().session_id != id ||
        lease.frame().buffer_index >= slots.size()) {
      SetError(error, "invalid native frame sample");
      return false;
    }
    auto& slot = *slots[lease.frame().buffer_index];
    if (slot.state != SlotState::kLeased ||
        lease.frame().dmabuf_fd != static_cast<int>(slot.surface->surfaceList[0].bufferDesc) ||
        bytes > config.width) {
      SetError(error, "sample must reference a live lease and fit the first luma row");
      return false;
    }
    try {
      Require(!slot.cpu_mapped, "CPU mapping still active");
      Require(NvBufSurfaceMap(slot.surface, 0, -1, NVBUF_MAP_READ) == 0, "NvBufSurfaceMap CPU");
      slot.cpu_mapped = true;
      counters->Update([](auto& stats) {
        ++stats.cpu_mapped;
      });
      Require(NvBufSurfaceSyncForCpu(slot.surface, 0, -1) == 0, "NvBufSurfaceSyncForCpu");
      const auto* luma =
          static_cast<const unsigned char*>(slot.surface->surfaceList[0].mappedAddr.addr[0]);
      Require(luma, "missing CPU luma mapping");
      std::uint64_t total = 0;
      for (std::size_t i = 0; i < bytes; ++i) total += luma[i];
      Require(slot.UnmapCpu(), "NvBufSurfaceUnMap CPU");
      *sum = total;
      return true;
    } catch (const std::exception& exception) {
      slot.UnmapCpu();
      SetError(error, exception.what());
      return false;
    }
  }
};
}  // namespace

struct NativeArgusCapture::Impl {
  mutable std::mutex control;
  std::shared_ptr<Counters> counters = std::make_shared<Counters>();
  std::shared_ptr<Session> session;
  std::string version;
  int mode = -1;
};
NativeArgusCapture::NativeArgusCapture() : impl_(std::make_unique<Impl>()) {
}
NativeArgusCapture::~NativeArgusCapture() {
  Stop(nullptr);
}
bool NativeArgusCapture::IsSupported() {
  return true;
}
bool NativeArgusCapture::Start(const NativeCaptureConfig& config, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (impl_->session) {
    SetError(error, "native session already exists; Stop and return all leases before Start");
    return false;
  }
  if (!config.width || !config.height || !config.fps || config.buffer_count < 2 ||
      config.buffer_count > 32 || config.shutdown_timeout_ms <= 0) {
    SetError(error, "invalid native capture configuration");
    return false;
  }
  if (!impl_->counters->Snapshot().ResourcesBalanced()) {
    SetError(error, "previous native cleanup failed; this runtime cannot restart");
    return false;
  }
  auto next = std::make_shared<Session>(impl_->counters, config);
  try {
    next->Initialize();
  } catch (const std::exception& exception) {
    SetError(error, exception.what());
    return false;
  }
  impl_->version = next->version;
  impl_->mode = next->mode_index;
  impl_->session = std::move(next);
  return true;
}
NativeAcquireStatus NativeArgusCapture::Acquire(FrameLease* lease, int timeout_ms,
                                                std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!lease || *lease || timeout_ms <= 0) {
    SetError(error, "Acquire requires an empty lease and positive timeout");
    return NativeAcquireStatus::kError;
  }
  if (!impl_->session) return NativeAcquireStatus::kStopped;
  return impl_->session->Acquire(lease, timeout_ms, error);
}
NativeStopStatus NativeArgusCapture::Stop(std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!impl_->session)
    return impl_->counters->Snapshot().ResourcesBalanced() ? NativeStopStatus::kStopped
                                                           : NativeStopStatus::kError;
  const auto status = impl_->session->Stop(error);
  if (status != NativeStopStatus::kLeasesOutstanding) impl_->session.reset();
  return status;
}
bool NativeArgusCapture::running() const {
  std::lock_guard<std::mutex> lock(impl_->control);
  return impl_->session && impl_->session->active && !impl_->session->stopping;
}
NativeCaptureStats NativeArgusCapture::stats() const {
  return impl_->counters->Snapshot();
}
std::string NativeArgusCapture::version() const {
  std::lock_guard<std::mutex> lock(impl_->control);
  return impl_->version;
}
int NativeArgusCapture::sensor_mode() const {
  std::lock_guard<std::mutex> lock(impl_->control);
  return impl_->mode;
}
bool NativeArgusCapture::GetTuning(CameraTuningState* state, std::string* error) const {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!state || !impl_->session || !impl_->session->active || impl_->session->stopping) {
    SetError(error, "GetTuning requires an active native session and output state");
    return false;
  }
  *state = impl_->session->tuning;
  return true;
}
bool NativeArgusCapture::SetTuning(const CameraTuning& settings, std::uint32_t* revision,
                                   std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!impl_->session) {
    SetError(error, "SetTuning requires an active native session");
    return false;
  }
  return impl_->session->SetTuning(settings, revision, error);
}
bool NativeArgusCapture::ResetTuning(std::uint32_t* revision, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!impl_->session) {
    SetError(error, "ResetTuning requires an active native session");
    return false;
  }
  return impl_->session->SetTuning(impl_->session->tuning.defaults, revision, error);
}
bool NativeArgusCapture::SampleLuma(const FrameLease& lease, std::size_t bytes, std::uint64_t* sum,
                                    std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->control);
  if (!impl_->session) {
    SetError(error, "native session is stopped");
    return false;
  }
  return impl_->session->SampleLuma(lease, bytes, sum, error);
}
}  // namespace cockpit::camera

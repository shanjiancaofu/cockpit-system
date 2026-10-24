#include <Argus/Argus.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <nvbufsurface.h>
#include <sys/resource.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "tools/camera/camera-diag/diagnostics.h"

namespace {
using namespace Argus;
namespace diag = cockpit::camera_diag;

void Require(bool ok, const std::string& action) {
  if (!ok) throw std::runtime_error(action);
}
void Check(Status status, const std::string& action) {
  Require(status == STATUS_OK, action + " status=" + std::to_string(status));
}
struct Lifecycle {
  int allocated = 0, destroyed = 0, egl_mapped = 0, egl_unmapped = 0;
  int cpu_mapped = 0, cpu_unmapped = 0;
  bool unsafe_shutdown = false;
  std::vector<std::string> errors;
};
struct EglDisplay {
  EGLDisplay value = EGL_NO_DISPLAY;
  EglDisplay() {
    value = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    Require(value != EGL_NO_DISPLAY, "eglGetDisplay");
    if (!eglInitialize(value, nullptr, nullptr)) {
      value = EGL_NO_DISPLAY;
      throw std::runtime_error("eglInitialize failed; configure a working NVIDIA EGL display");
    }
  }
  ~EglDisplay() {
    if (value != EGL_NO_DISPLAY) eglTerminate(value);
  }
};
struct Slot {
  explicit Slot(Lifecycle& state) : state(state) {
  }
  Lifecycle& state;
  NvBufSurface* surface = nullptr;
  UniqueObj<Buffer> buffer;
  bool egl_mapped = false, cpu_mapped = false;
  ~Slot() {
    buffer.reset();
    // If the driver cannot quiesce, keep native storage alive until this isolated
    // probe exits. Report the imbalance instead of freeing a possible DMA target.
    if (state.unsafe_shutdown) return;
    if (cpu_mapped) {
      if (NvBufSurfaceUnMap(surface, 0, -1) == 0)
        ++state.cpu_unmapped;
      else
        state.errors.push_back("NvBufSurfaceUnMap");
    }
    if (egl_mapped) {
      if (NvBufSurfaceUnMapEglImage(surface, 0) == 0)
        ++state.egl_unmapped;
      else
        state.errors.push_back("NvBufSurfaceUnMapEglImage");
    }
    if (surface) {
      if (NvBufSurfaceDestroy(surface) == 0)
        ++state.destroyed;
      else
        state.errors.push_back("NvBufSurfaceDestroy");
    }
  }
};

// Declared after buffers/request so every exit path stops capture before freeing images.
struct StopSession {
  ICaptureSession* session;
  IBufferOutputStream* stream;
  Lifecycle& state;
  bool active = false;
  ~StopSession() {
    Stop();
  }
  void Stop() {
    if (!active) return;
    active = false;
    session->stopRepeat();
    const auto eos = stream->endOfStream();
    if (eos != STATUS_OK) state.errors.push_back("endOfStream status=" + std::to_string(eos));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool drained = false;
    while (std::chrono::steady_clock::now() < deadline) {
      Status status;
      Buffer* buffer = stream->acquireBuffer(200000000ULL, &status);
      if (status == STATUS_END_OF_STREAM) {
        drained = true;
        break;
      }
      if (buffer && stream->releaseBuffer(buffer) != STATUS_OK) {
        state.errors.push_back("releaseBuffer during drain");
        break;
      }
      if (!buffer && status != STATUS_TIMEOUT) break;
    }
    if (!drained) {
      state.errors.push_back("stream did not drain before shutdown deadline");
      const auto cancelled = session->cancelRequests();
      if (cancelled != STATUS_OK)
        state.errors.push_back("cancelRequests status=" + std::to_string(cancelled));
    }
    auto idle = session->waitForIdle(5000000000ULL);
    if (idle != STATUS_OK) {
      state.errors.push_back("waitForIdle status=" + std::to_string(idle));
      session->cancelRequests();
      idle = session->waitForIdle(5000000000ULL);
      state.unsafe_shutdown = idle != STATUS_OK;
    }
  }
};

diag::Json Run(ICameraProvider* provider, CameraDevice* device, const diag::Options& options,
               EGLDisplay display, int cycle, Lifecycle& lifecycle, std::ofstream& metadata,
               bool* passed) {
  diag::FrameStats stats;
  std::string error;
  int selected_mode = -1, imported_fds = 0, timeouts = 0;
  std::uint64_t luma_sample_sum = 0;
  const auto started = std::chrono::steady_clock::now();
  const auto cleanup_errors_before = lifecycle.errors.size();
  try {
    std::vector<SensorMode*> modes;
    auto* properties = interface_cast<ICameraProperties>(device);
    Require(properties, "ICameraProperties");
    Check(properties->getAllSensorModes(&modes), "getAllSensorModes");
    SensorMode* mode = nullptr;
    // Frame duration must be >= the sensor's minimum, rounding up at fractional boundaries.
    std::uint64_t duration = (1000000000ULL + options.fps - 1) / options.fps;
    for (std::size_t i = 0; i < modes.size(); ++i) {
      auto* info = interface_cast<ISensorMode>(modes[i]);
      if (info && info->getResolution() == Size2D<uint32_t>(options.width, options.height) &&
          duration >= info->getFrameDurationRange().min() &&
          duration <= info->getFrameDurationRange().max()) {
        mode = modes[i];
        selected_mode = static_cast<int>(i);
        break;
      }
    }
    Require(mode, "no sensor mode matching exact dimensions and requested frame duration");
    UniqueObj<CaptureSession> session(provider->createCaptureSession(device));
    auto* capture = interface_cast<ICaptureSession>(session);
    Require(capture, "createCaptureSession");
    UniqueObj<OutputStreamSettings> settings(
        capture->createOutputStreamSettings(STREAM_TYPE_BUFFER));
    auto* buffer_settings = interface_cast<IBufferOutputStreamSettings>(settings);
    Require(buffer_settings, "STREAM_TYPE_BUFFER unsupported");
    Check(buffer_settings->setBufferType(BUFFER_TYPE_EGL_IMAGE), "setBufferType EGL_IMAGE");
    Check(buffer_settings->setSyncType(SYNC_TYPE_NONE), "setSyncType NONE");
    buffer_settings->setMetadataEnable(true);
    UniqueObj<OutputStream> stream(capture->createOutputStream(settings.get()));
    auto* output = interface_cast<IBufferOutputStream>(stream);
    Require(output, "create BufferOutputStream");
    std::vector<std::unique_ptr<Slot>> slots;
    UniqueObj<BufferSettings> native_settings(output->createBufferSettings());
    auto* image_settings = interface_cast<IEGLImageBufferSettings>(native_settings);
    Require(image_settings, "IEGLImageBufferSettings");
    Check(image_settings->setEGLDisplay(display), "setEGLDisplay");
    for (int i = 0; i < 6; ++i) {
      slots.push_back(std::make_unique<Slot>(lifecycle));
      auto& slot = *slots.back();
      NvBufSurfaceAllocateParams params{};
      params.params.width = options.width;
      params.params.height = options.height;
      params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
      params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
      params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
      params.memtag = NvBufSurfaceTag_CAMERA;
      Require(NvBufSurfaceAllocate(&slot.surface, 1, &params) == 0 && slot.surface,
              "NvBufSurfaceAllocate");
      ++lifecycle.allocated;
      slot.surface->numFilled = 1;
      void* imported = nullptr;
      Require(NvBufSurfaceFromFd(static_cast<int>(slot.surface->surfaceList[0].bufferDesc),
                                 &imported) == 0 &&
                  imported == slot.surface,
              "NvBufSurfaceFromFd identity check");
      ++imported_fds;
      Require(NvBufSurfaceMapEglImage(slot.surface, 0) == 0, "NvBufSurfaceMapEglImage");
      slot.egl_mapped = true;
      ++lifecycle.egl_mapped;
      auto image = static_cast<EGLImageKHR>(slot.surface->surfaceList[0].mappedAddr.eglImage);
      Require(image != EGL_NO_IMAGE_KHR, "NvBufSurface returned no EGLImage");
      Check(image_settings->setEGLImage(image), "setEGLImage");
      slot.buffer.reset(output->createBuffer(native_settings.get()));
      auto* buffer = interface_cast<IBuffer>(slot.buffer);
      Require(buffer, "createBuffer");
      buffer->setClientData(&slot);
      Check(output->releaseBuffer(slot.buffer.get()), "release initial buffer");
    }
    UniqueObj<Request> request(capture->createRequest(CAPTURE_INTENT_VIDEO_RECORD));
    auto* req = interface_cast<IRequest>(request);
    auto* source = interface_cast<ISourceSettings>(request);
    Require(req && source, "createRequest / ISourceSettings");
    Check(req->enableOutputStream(stream.get()), "enableOutputStream");
    Check(source->setSensorMode(mode), "setSensorMode");
    Check(source->setFrameDurationRange(Range<uint64_t>(duration)), "setFrameDurationRange");
    StopSession stop{capture, output, lifecycle};
    // Set before repeat: a failed submission may still need cancellation.
    stop.active = true;
    Check(capture->repeat(request.get()), "repeat");
    for (int i = 0; i < options.frames; ++i) {
      Status status;
      Buffer* acquired =
          output->acquireBuffer(static_cast<uint64_t>(options.timeout_ms) * 1000000ULL, &status);
      if (status == STATUS_TIMEOUT) ++timeouts;
      Check(status, "acquireBuffer");
      auto* buffer = interface_cast<IBuffer>(acquired);
      Require(buffer, "acquire returned no IBuffer");
      const auto* frame = interface_cast<const ICaptureMetadata>(buffer->getMetadata());
      Require(frame, "missing frame metadata");
      stats.Observe(frame->getCaptureId(), static_cast<std::int64_t>(frame->getSensorTimestamp()),
                    false);
      auto* slot = const_cast<Slot*>(static_cast<const Slot*>(buffer->getClientData()));
      Require(slot && slot->buffer.get() == acquired, "buffer identity mismatch");
      if (i == 0) {
        Require(NvBufSurfaceMap(slot->surface, 0, -1, NVBUF_MAP_READ) == 0, "NvBufSurfaceMap CPU");
        slot->cpu_mapped = true;
        ++lifecycle.cpu_mapped;
        Require(NvBufSurfaceSyncForCpu(slot->surface, 0, -1) == 0, "NvBufSurfaceSyncForCpu");
        const auto* luma =
            static_cast<const unsigned char*>(slot->surface->surfaceList[0].mappedAddr.addr[0]);
        Require(luma, "missing CPU luma mapping");
        for (int sample = 0; sample < 64; ++sample) luma_sample_sum += luma[sample];
        Require(NvBufSurfaceUnMap(slot->surface, 0, -1) == 0, "NvBufSurfaceUnMap CPU");
        slot->cpu_mapped = false;
        ++lifecycle.cpu_unmapped;
      }
      metadata << cycle << '\t' << i << '\t' << frame->getCaptureId() << '\t'
               << frame->getSensorTimestamp() << '\t' << frame->getFrameDuration() << '\t'
               << slot->surface->surfaceList[0].bufferDesc << '\n';
      Require(static_cast<bool>(metadata), "write metadata");
      // SYNC_TYPE_NONE: CPU reads have finished; no outstanding downstream GPU work.
      Check(output->releaseBuffer(acquired), "release acquired buffer");
    }
    stop.Stop();
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  const bool ok = error.empty() && lifecycle.errors.size() == cleanup_errors_before &&
                  stats.healthy() && stats.frames == static_cast<std::uint64_t>(options.frames) &&
                  stats.fps() >= options.fps * 0.95 && stats.fps() <= options.fps * 1.05;
  *passed = *passed && ok;
  return diag::Object({{"cycle", diag::Number(cycle)},
                       {"passed", diag::Boolean(ok)},
                       {"sensor_mode", diag::Number(selected_mode)},
                       {"frames", diag::Number(stats.frames)},
                       {"sensor_fps", diag::Number(stats.fps())},
                       {"capture_id_gaps", diag::Number(stats.sequence_gaps)},
                       {"capture_id_duplicates", diag::Number(stats.duplicates)},
                       {"capture_id_regressions", diag::Number(stats.regressions)},
                       {"timestamp_errors", diag::Number(stats.timestamp_errors)},
                       {"timeouts", diag::Number(timeouts)},
                       {"first_timestamp_ns", diag::Number(stats.first_timestamp)},
                       {"last_timestamp_ns", diag::Number(stats.previous_timestamp)},
                       {"imported_fds", diag::Number(imported_fds)},
                       {"luma_sample_sum", diag::Number(luma_sample_sum)},
                       {"error", diag::Quote(error)},
                       {"wall_ms", diag::Number(std::chrono::duration<double, std::milli>(
                                                    std::chrono::steady_clock::now() - started)
                                                    .count())}});
}
}  // namespace

int main(int argc, char** argv) {
  diag::Options options;
  options.device = "0";
  options.output = "libargus_capture.json";
  std::string error;
  if (!diag::ParseOptions(argc, argv, &options, &error)) {
    std::cerr << error << '\n';
    return 2;
  }
  if (options.help) {
    std::cout << "libargus-capture-probe [--device 0] [--width 1920] [--height 1080] [--fps 30]"
                 " [--frames 300] [--cycles 1] [--timeout-ms 5000] [--output report.json]\n";
    return 0;
  }
  if (options.inspect_only || options.frames < 2) {
    std::cerr << "capture requires at least two frames\n";
    return 2;
  }
  Lifecycle lifecycle;
  std::vector<diag::Json> runs;
  bool passed = true;
  std::string argus_version;
  std::ofstream metadata(options.output + ".frames.tsv");
  metadata << "cycle\tframe\tcapture_id\tsensor_timestamp_ns\tframe_duration_ns\tdmabuf_fd\n";
  const auto started = std::chrono::steady_clock::now();
  rusage before{};
  getrusage(RUSAGE_SELF, &before);
  try {
    Require(static_cast<bool>(metadata), "open metadata output");
    std::size_t end = 0;
    const auto index = std::stoul(options.device, &end);
    Require(end == options.device.size() && options.device.front() != '-',
            "device must be a nonnegative Argus index");
    EglDisplay display;
    UniqueObj<CameraProvider> provider(CameraProvider::create());
    auto* camera = interface_cast<ICameraProvider>(provider);
    Require(camera, "CameraProvider::create");
    argus_version = camera->getVersion();
    std::vector<CameraDevice*> devices;
    Check(camera->getCameraDevices(&devices), "getCameraDevices");
    Require(index < devices.size(), "Argus camera index unavailable");
    for (int cycle = 0; cycle < options.cycles; ++cycle) {
      runs.push_back(
          Run(camera, devices[index], options, display.value, cycle, lifecycle, metadata, &passed));
      if (lifecycle.unsafe_shutdown) break;
    }
  } catch (const std::exception& exception) {
    error = exception.what();
    passed = false;
  }
  metadata.close();
  const bool balanced = lifecycle.allocated == lifecycle.destroyed &&
                        lifecycle.egl_mapped == lifecycle.egl_unmapped &&
                        lifecycle.cpu_mapped == lifecycle.cpu_unmapped && lifecycle.errors.empty();
  passed = passed && balanced && static_cast<bool>(metadata);
  rusage after{};
  getrusage(RUSAGE_SELF, &after);
  const auto cpu_ms = [](const rusage& value) {
    return (value.ru_utime.tv_sec + value.ru_stime.tv_sec) * 1000.0 +
           (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1000.0;
  };
  const double wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  const auto report = diag::Object(
      {{"schema_version", "1"},
       {"passed", diag::Boolean(passed)},
       {"environment", diag::InspectEnvironment()},
       {"argus_version", diag::Quote(argus_version)},
       {"stream_type", diag::Quote("STREAM_TYPE_BUFFER")},
       {"buffer_type", diag::Quote("NvBufSurface NV12 BLOCK_LINEAR -> EGLImage")},
       {"sync_type",
        diag::Quote(
            "SYNC_TYPE_NONE; acquire completes producer writes, CPU read finishes before release")},
       {"width", diag::Number(options.width)},
       {"height", diag::Number(options.height)},
       {"fps", diag::Number(options.fps)},
       {"runs", diag::Array(runs)},
       {"allocated", diag::Number(lifecycle.allocated)},
       {"destroyed", diag::Number(lifecycle.destroyed)},
       {"egl_mapped", diag::Number(lifecycle.egl_mapped)},
       {"egl_unmapped", diag::Number(lifecycle.egl_unmapped)},
       {"cpu_mapped", diag::Number(lifecycle.cpu_mapped)},
       {"cpu_unmapped", diag::Number(lifecycle.cpu_unmapped)},
       {"resources_balanced", diag::Boolean(balanced)},
       {"native_resources_retained_until_exit", diag::Boolean(lifecycle.unsafe_shutdown)},
       {"cleanup_errors", diag::Strings(lifecycle.errors)},
       {"process_wall_ms", diag::Number(wall_ms)},
       {"process_cpu_percent", diag::Number((cpu_ms(after) - cpu_ms(before)) / wall_ms * 100.0)},
       {"max_rss_kib", diag::Number(after.ru_maxrss)},
       {"error", diag::Quote(error)}});
  if (!diag::WriteReport(options.output, report)) {
    std::cerr << "cannot write report\n";
    return 2;
  }
  std::cout << "passed=" << passed << " report=" << options.output << " error=" << error << '\n';
  return passed ? 0 : 1;
}

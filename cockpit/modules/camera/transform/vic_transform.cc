#include "cockpit/modules/camera/transform/vic_transform.h"

#include <nvbufsurface.h>
#include <nvbufsurftransform.h>

#include <chrono>
#include <mutex>
#include <stdexcept>

namespace cockpit::camera {
namespace {
std::mutex vic_session_mutex;
void Require(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
}  // namespace
struct VicTransform::Impl {
  mutable std::mutex mutex;
  NvBufSurface* destination = nullptr;
  VicTransformConfig config;
  VicTransformStats counters;
  bool mapped = false;
  bool Unmap() {
    if (!mapped) return true;
    if (NvBufSurfaceUnMap(destination, 0, -1) != 0) return false;
    mapped = false;
    ++counters.unmapped;
    return true;
  }
  bool Clear() {
    if (!Unmap()) return false;
    if (destination) {
      if (NvBufSurfaceDestroy(destination) != 0) return false;
      destination = nullptr;
      ++counters.destroyed;
    }
    return true;
  }
};
VicTransform::VicTransform() : impl_(std::make_unique<Impl>()) {
}
VicTransform::~VicTransform() {
  Reset();
}
bool VicTransform::IsSupported() {
  return true;
}
bool VicTransform::Reset(std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const bool ok = impl_->Clear();
  if (!ok && error) *error = "failed to unmap/destroy VIC output surface";
  return ok;
}
VicTransformStats VicTransform::stats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->counters;
}
bool VicTransform::Convert(SharedFrameLease input, const VicTransformConfig& config,
                           CpuVideoFrame* output, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto started = std::chrono::steady_clock::now();
  try {
    Require(input && output, "VIC requires a live shared frame and an output");
    const NativeFrame source_frame = input.frame();
    Require(source_frame.dmabuf_fd >= 0 && source_frame.format == CameraPixelFormat::kNv12,
            "VIC expects native NV12 input");
    Require(config.width && config.height && config.width <= 8192 && config.height <= 8192 &&
                config.width % 2 == 0 && config.height % 2 == 0,
            "VIC requires bounded even output dimensions");
    NvBufSurfaceColorFormat format;
    switch (config.format) {
      case VicOutputFormat::kBgrx:
        format = NVBUF_COLOR_FORMAT_BGRx;
        break;
      case VicOutputFormat::kNv12:
        format = NVBUF_COLOR_FORMAT_NV12;
        break;
      case VicOutputFormat::kI420:
        format = NVBUF_COLOR_FORMAT_YUV420;
        break;
      default:
        throw std::runtime_error("unsupported VIC output format");
    }
    void* imported = nullptr;
    Require(NvBufSurfaceFromFd(source_frame.dmabuf_fd, &imported) == 0 && imported,
            "NvBufSurfaceFromFd input");
    auto* source = static_cast<NvBufSurface*>(imported);
    Require(source->numFilled == 1 && source->surfaceList &&
                source->surfaceList[0].width == source_frame.width &&
                source->surfaceList[0].height == source_frame.height &&
                source->surfaceList[0].colorFormat == NVBUF_COLOR_FORMAT_NV12,
            "source surface metadata mismatch");
    if (!impl_->destination || impl_->config.width != config.width ||
        impl_->config.height != config.height || impl_->config.format != config.format) {
      Require(impl_->Clear(), "reset VIC output");
      NvBufSurfaceAllocateParams params{};
      params.params.width = config.width;
      params.params.height = config.height;
      params.params.layout = NVBUF_LAYOUT_PITCH;
      params.params.colorFormat = format;
      params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
      params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
      Require(NvBufSurfaceAllocate(&impl_->destination, 1, &params) == 0 && impl_->destination,
              "allocate VIC output");
      ++impl_->counters.allocated;
      impl_->destination->numFilled = 1;
      impl_->config = config;
    }
    Require(impl_->Unmap(), "unmap previous VIC output");
    {
      std::lock_guard<std::mutex> session_lock(vic_session_mutex);
      NvBufSurfTransformConfigParams previous{}, selected{};
      // SDK session configuration can be uninitialized on a newly created consumer thread.
      const bool have_previous =
          NvBufSurfTransformGetSessionParams(&previous) == NvBufSurfTransformError_Success;
      selected.compute_mode = NvBufSurfTransformCompute_VIC;
      Require(NvBufSurfTransformSetSessionParams(&selected) == NvBufSurfTransformError_Success,
              "select VIC compute engine");
      NvBufSurfTransformParams params{};
      params.transform_flag = NVBUFSURF_TRANSFORM_FILTER;
      params.transform_filter = NvBufSurfTransformInter_Bilinear;
      const auto result = NvBufSurfTransform(source, impl_->destination, &params);  // Synchronous.
      const auto restored = have_previous ? NvBufSurfTransformSetSessionParams(&previous)
                                          : NvBufSurfTransformError_Success;
      Require(result == NvBufSurfTransformError_Success, "VIC transform failed (no GPU fallback)");
      Require(restored == NvBufSurfTransformError_Success, "restore VIC session parameters");
    }
    Require(NvBufSurfaceMap(impl_->destination, 0, -1, NVBUF_MAP_READ) == 0,
            "map VIC output for CPU");
    impl_->mapped = true;
    ++impl_->counters.mapped;
    Require(NvBufSurfaceSyncForCpu(impl_->destination, 0, -1) == 0, "sync VIC output for CPU");
    const auto& surface = impl_->destination->surfaceList[0];
    std::vector<detail::MappedVideoPlane> planes;
    for (unsigned p = 0; p < surface.planeParams.num_planes; ++p)
      planes.push_back({static_cast<const std::uint8_t*>(surface.mappedAddr.addr[p]),
                        surface.planeParams.psize[p], surface.planeParams.pitch[p]});
    CpuVideoFrame next;
    Require(detail::CopyMappedVideoPlanes(planes, config, &next, error),
            "invalid VIC plane layout");
    Require(impl_->Unmap(), "unmap VIC output after CPU copy");
    Require(static_cast<bool>(input), "native session was revoked during conversion");
    next.session_id = source_frame.session_id;
    next.capture_id = source_frame.capture_id;
    next.sensor_timestamp_ns = source_frame.sensor_timestamp_ns;
    next.frame_duration_ns = source_frame.frame_duration_ns;
    next.received_at_ns = source_frame.received_at_ns;
    next.source_clock = source_frame.source_clock;
    *output = std::move(next);
    ++impl_->counters.transformed;
    impl_->counters.total_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    return true;
  } catch (const std::exception& exception) {
    impl_->Unmap();
    ++impl_->counters.failures;
    if (error) *error = exception.what();
    return false;
  }
}
}  // namespace cockpit::camera

#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>

#include "cockpit/driver/camera/frames/latest_frame_buffer.h"
#include "cockpit/driver/camera/photo/jpeg_encoder.h"
#include "cockpit/driver/camera/shared_memory/shared_frame_buffer.h"
#include "cockpit/driver/camera/transform/vic_transform.h"
#include "tools/camera/camera-diag/diagnostics.h"
#ifdef COCKPIT_CAMERA_HAS_VIC
#include <nvbufsurface.h>
#include <nvbufsurftransform.h>
#endif

namespace camera = cockpit::camera;
namespace diag = cockpit::camera_diag;
namespace {
void Require(bool ok, const std::string& reason) {
  if (!ok) throw std::runtime_error(reason);
}
#ifdef COCKPIT_CAMERA_HAS_VIC
struct Surface {
  NvBufSurface* value = nullptr;
  bool mapped = false;
  ~Surface() {
    if (mapped) NvBufSurfaceUnMap(value, 0, -1);
    if (value) NvBufSurfaceDestroy(value);
  }
  void Allocate(NvBufSurfaceLayout layout) {
    NvBufSurfaceAllocateParams params{};
    params.params.width = 642;
    params.params.height = 482;
    params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    params.params.layout = layout;
    params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    Require(NvBufSurfaceAllocate(&value, 1, &params) == 0 && value, "allocate synthetic fixture");
    value->numFilled = 1;
  }
};
#endif
}  // namespace
int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--output") return 2;
  const std::string output = argv[2];
  std::string error;
  bool color = false, nv12 = false, i420 = false, shm = false, jpeg = false, released = false;
  std::array<int, 4> pixel{};
  camera::VicTransform converter;
  camera::NativeFrameBroker broker;
  struct ShmCleanup {
    std::string name;
    ~ShmCleanup() {
      shm_unlink(name.c_str());
    }
  } cleanup{"/cockpit_vic_pr04_" + std::to_string(getpid())};
  try {
    Require(camera::VicTransform::IsSupported(), "VIC SDK unavailable");
#ifdef COCKPIT_CAMERA_HAS_VIC
    auto pitch = std::make_shared<Surface>();
    pitch->Allocate(NVBUF_LAYOUT_PITCH);
    Require(NvBufSurfaceMap(pitch->value, 0, -1, NVBUF_MAP_WRITE) == 0, "map fixture");
    pitch->mapped = true;
    auto& source = pitch->value->surfaceList[0];
    Require(source.planeParams.num_planes == 2, "fixture NV12 plane count");
    for (unsigned plane = 0; plane < 2; ++plane) {
      auto* pointer = static_cast<unsigned char*>(source.mappedAddr.addr[plane]);
      Require(pointer && source.planeParams.pitch[plane] >= 642, "fixture pitch");
      std::memset(pointer, 0, source.planeParams.psize[plane]);
      for (unsigned row = 0; row < (plane == 0 ? 482U : 241U); ++row) {
        auto* line = pointer + row * source.planeParams.pitch[plane];
        for (unsigned x = 0; x < 642; ++x) line[x] = plane == 0 ? 82 : x % 2 == 0 ? 90 : 240;
      }
    }
    Require(NvBufSurfaceSyncForDevice(pitch->value, 0, -1) == 0, "sync fixture for VIC");
    Require(NvBufSurfaceUnMap(pitch->value, 0, -1) == 0, "unmap fixture");
    pitch->mapped = false;
    auto block = std::make_shared<Surface>();
    block->Allocate(NVBUF_LAYOUT_BLOCK_LINEAR);
    NvBufSurfTransformConfigParams previous{}, selected{};
    Require(NvBufSurfTransformGetSessionParams(&previous) == NvBufSurfTransformError_Success,
            "get fixture session");
    selected.compute_mode = NvBufSurfTransformCompute_VIC;
    Require(NvBufSurfTransformSetSessionParams(&selected) == NvBufSurfTransformError_Success,
            "select fixture VIC");
    NvBufSurfTransformParams params{};
    const auto result = NvBufSurfTransform(pitch->value, block->value, &params);
    NvBufSurfTransformSetSessionParams(&previous);
    Require(result == NvBufSurfTransformError_Success, "create block-linear red fixture");
    camera::NativeFrame metadata;
    metadata.session_id = 1;
    metadata.width = 642;
    metadata.height = 482;
    metadata.dmabuf_fd = static_cast<int>(block->value->surfaceList[0].bufferDesc);
    metadata.capture_id = 37;
    metadata.sensor_timestamp_ns = 1234567;
    metadata.received_at_ns = 1000000000;
    camera::FrameLease lease(metadata, [owner = block, &released]() noexcept {
      released = true;
    });
    block.reset();
    Require(broker.BeginSession(1) &&
                broker.Publish(std::move(lease)) == camera::FramePublishStatus::kPublished,
            "publish fixture");
    auto preview = broker.TryPop(camera::FrameConsumer::kPreview);
    auto transform = broker.TryPop(camera::FrameConsumer::kTransform);
    camera::CpuVideoFrame cpu;
    Require(converter.Convert(preview, {322, 242, camera::VicOutputFormat::kBgrx}, &cpu, &error),
            error);
    for (unsigned i = 0; i < 4; ++i) pixel[i] = cpu.data[i];
    color = pixel[0] < 30 && pixel[1] < 30 && pixel[2] > 220;
    Require(color, "VIC BGRx red/blue channel order mismatch");
    Require(cpu.planes[0].stride >= 322 * 4 && cpu.planes[0].stride > cpu.planes[0].row_bytes,
            "padded pitch fixture missing");
    camera::CameraFrame legacy;
    Require(camera::TakeCameraFrame(std::move(cpu), &legacy, &error) && legacy.IsValid(),
            "BGRx compatibility frame");
    Require(camera::JpegEncoder::Encode(legacy, output + ".jpg", 90, &error), error);
    std::ifstream image(output + ".jpg", std::ios::binary);
    unsigned char a = image.get(), b = image.get();
    jpeg = a == 0xff && b == 0xd8;
    Require(jpeg, "JPEG header missing");
    camera::LatestFrameBuffer latest;
    Require(latest.Publish(legacy), "LatestFrameBuffer rejected converted pixels");
    camera::SharedFrameBufferConfig config;
    config.name = cleanup.name;
    auto writer = camera::SharedFrameWriter::Create(config, &error);
    Require(static_cast<bool>(writer), error);
    auto reader = camera::SharedFrameReader::Open(config.name, &error);
    Require(reader && writer->Publish(std::move(legacy)), "existing shared memory write");
    camera::CameraFrame readback;
    std::uint64_t generation = 0;
    Require(reader->ReadLatest(&readback, &generation, &error), error);
    shm = readback.IsValid() && generation > 0 && readback.sequence == 37 &&
          readback.data[2] > 220 && readback.source_clock == camera::CameraTimestampClock::kUnknown;
    Require(shm, "shared memory stride/color/metadata mismatch");
    Require(converter.Convert(transform, {322, 242, camera::VicOutputFormat::kNv12}, &cpu, &error),
            error);
    nv12 = cpu.planes.size() == 2 && std::abs(int(cpu.data[0]) - 82) <= 3 &&
           std::abs(int(cpu.data[cpu.planes[1].offset]) - 90) <= 3 &&
           std::abs(int(cpu.data[cpu.planes[1].offset + 1]) - 240) <= 3;
    Require(nv12, "NV12 Y/UV layout mismatch");
    Require(converter.Convert(transform, {322, 242, camera::VicOutputFormat::kI420}, &cpu, &error),
            error);
    i420 = cpu.planes.size() == 3 && std::abs(int(cpu.data[cpu.planes[1].offset]) - 90) <= 3 &&
           std::abs(int(cpu.data[cpu.planes[2].offset]) - 240) <= 3;
    Require(i420, "I420 U/V plane mismatch");
    broker.Close();
    camera::CpuVideoFrame untouched;
    untouched.width = 123;
    Require(!converter.Convert(preview, {}, &untouched, &error) && untouched.width == 123,
            "revoked session accepted by VIC");
    preview.Reset();
    Require(!released, "first consumer released the shared input");
    transform.Reset();
    Require(released, "last consumer did not release input");
    error.clear();
#endif
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  broker.Close();
  const bool reset = converter.Reset();
  const auto stats = converter.stats();
  const bool passed = error.empty() && color && nv12 && i420 && shm && jpeg && released && reset &&
                      stats.ResourcesBalanced();
  const auto report =
      diag::Object({{"schema_version", "1"},
                    {"passed", diag::Boolean(passed)},
                    {"bgrx_color", diag::Boolean(color)},
                    {"first_pixel", diag::Array({diag::Number(pixel[0]), diag::Number(pixel[1]),
                                                 diag::Number(pixel[2]), diag::Number(pixel[3])})},
                    {"nv12_planes", diag::Boolean(nv12)},
                    {"i420_planes", diag::Boolean(i420)},
                    {"shared_memory", diag::Boolean(shm)},
                    {"jpeg", diag::Boolean(jpeg)},
                    {"last_consumer_release", diag::Boolean(released)},
                    {"transformed", diag::Number(stats.transformed)},
                    {"allocated", diag::Number(stats.allocated)},
                    {"destroyed", diag::Number(stats.destroyed)},
                    {"mapped", diag::Number(stats.mapped)},
                    {"unmapped", diag::Number(stats.unmapped)},
                    {"resources_balanced", diag::Boolean(stats.ResourcesBalanced())},
                    {"error", diag::Quote(error)}});
  if (!diag::WriteReport(output, report)) return 2;
  std::cout << "passed=" << passed << " report=" << output << " error=" << error << '\n';
  return passed ? 0 : 1;
}

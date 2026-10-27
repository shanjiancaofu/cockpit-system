#include "cockpit/driver/camera/encode/h264_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace cockpit::camera {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error) *error = message;
}
std::string AvError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return std::string(buffer);
}
bool ValidEven(std::uint32_t value) {
  return value > 0 && value % 2 == 0;
}

}  // namespace

struct H264Encoder::Impl {
  mutable std::mutex mutex;
  AVCodecContext* context = nullptr;
  H264EncoderConfig config;
  H264EncoderStats counters;

  ~Impl() {
    if (context) avcodec_free_context(&context);
  }

  bool Emit(std::vector<H264Packet>* packets, std::string* error) {
    if (!packets) {
      SetError(error, "H264 packet output is null");
      return false;
    }
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
      SetError(error, "cannot allocate H264 packet");
      return false;
    }
    bool ok = true;
    for (;;) {
      const int result = avcodec_receive_packet(context, packet);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
      if (result < 0) {
        SetError(error, "H264 packet receive failed: " + AvError(result));
        ok = false;
        break;
      }
      H264Packet output;
      output.data.assign(packet->data, packet->data + packet->size);
      output.pts = packet->pts == AV_NOPTS_VALUE ? 0 : packet->pts;
      output.dts = packet->dts == AV_NOPTS_VALUE ? output.pts : packet->dts;
      output.keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
      counters.bytes_emitted += output.data.size();
      ++counters.packets_emitted;
      if (output.keyframe) ++counters.keyframes;
      packets->push_back(std::move(output));
      av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return ok;
  }
};

H264Encoder::H264Encoder() : impl_(std::make_unique<Impl>()) {
}
H264Encoder::~H264Encoder() = default;

bool H264Encoder::IsSupported() {
  return avcodec_find_encoder_by_name("libx264") != nullptr ||
         avcodec_find_encoder(AV_CODEC_ID_H264) != nullptr;
}

bool H264Encoder::Start(const H264EncoderConfig& config, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->context) avcodec_free_context(&impl_->context);
  if (!ValidEven(config.width) || !ValidEven(config.height) || config.fps == 0 ||
      config.bitrate == 0 || config.gop == 0 || config.b_frames > 0 || config.preset.empty() ||
      config.tune.empty()) {
    SetError(error, "invalid H264 encoder configuration");
    return false;
  }
  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!codec) {
    SetError(error, "no H264 encoder is available");
    return false;
  }
  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) {
    SetError(error, "cannot allocate H264 codec context");
    return false;
  }
  context->width = static_cast<int>(config.width);
  context->height = static_cast<int>(config.height);
  context->pix_fmt = AV_PIX_FMT_YUV420P;
  context->time_base = AVRational{1, static_cast<int>(config.fps)};
  context->framerate = AVRational{static_cast<int>(config.fps), 1};
  context->bit_rate = config.bitrate;
  context->gop_size = static_cast<int>(config.gop);
  context->max_b_frames = 0;
  av_opt_set(context->priv_data, "preset", config.preset.c_str(), 0);
  av_opt_set(context->priv_data, "tune", config.tune.c_str(), 0);
  av_opt_set(context->priv_data, "repeat-headers", "1", 0);
  const int result = avcodec_open2(context, codec, nullptr);
  if (result < 0) {
    SetError(error, "H264 encoder open failed: " + AvError(result));
    avcodec_free_context(&context);
    return false;
  }
  impl_->context = context;
  impl_->config = config;
  impl_->counters = {};
  return true;
}

bool H264Encoder::Encode(const CpuVideoFrame& input, std::vector<H264Packet>* packets,
                         std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->context || !packets) {
    SetError(error, "H264 encoder is not running or output is null");
    return false;
  }
  if (input.format != VicOutputFormat::kI420 || input.width != impl_->config.width ||
      input.height != impl_->config.height || input.planes.size() != 3 || input.data.empty()) {
    SetError(error, "H264 encoder expects an I420 frame matching its configuration");
    return false;
  }
  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    SetError(error, "cannot allocate H264 input frame");
    return false;
  }
  frame->format = AV_PIX_FMT_YUV420P;
  frame->width = static_cast<int>(input.width);
  frame->height = static_cast<int>(input.height);
  frame->pts = input.capture_id;
  bool valid = true;
  const std::uint32_t row_bytes[3] = {input.width, input.width / 2, input.width / 2};
  const std::uint32_t heights[3] = {input.height, input.height / 2, input.height / 2};
  for (int plane = 0; plane < 3; ++plane) {
    const auto& layout = input.planes[plane];
    const std::uint64_t end = static_cast<std::uint64_t>(layout.offset) +
                              static_cast<std::uint64_t>(layout.stride) * (heights[plane] - 1) +
                              row_bytes[plane];
    if (layout.row_bytes < row_bytes[plane] || layout.height != heights[plane] ||
        layout.stride < row_bytes[plane] || end > input.data.size()) {
      valid = false;
      break;
    }
    frame->data[plane] = const_cast<std::uint8_t*>(input.data.data() + layout.offset);
    frame->linesize[plane] = static_cast<int>(layout.stride);
  }
  if (!valid) {
    av_frame_free(&frame);
    SetError(error, "invalid I420 plane layout");
    return false;
  }
  const int result = avcodec_send_frame(impl_->context, frame);
  av_frame_free(&frame);
  if (result < 0) {
    SetError(error, "H264 frame submit failed: " + AvError(result));
    return false;
  }
  ++impl_->counters.frames_submitted;
  return impl_->Emit(packets, error);
}

bool H264Encoder::Flush(std::vector<H264Packet>* packets, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->context || !packets) {
    SetError(error, "H264 encoder is not running or output is null");
    return false;
  }
  const int result = avcodec_send_frame(impl_->context, nullptr);
  if (result < 0 && result != AVERROR_EOF) {
    SetError(error, "H264 flush submit failed: " + AvError(result));
    return false;
  }
  return impl_->Emit(packets, error);
}

void H264Encoder::Stop() {
  if (impl_->context) avcodec_free_context(&impl_->context);
}
bool H264Encoder::running() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->context != nullptr;
}
H264EncoderStats H264Encoder::stats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->counters;
}

struct H264FileWriter::Impl {
  std::ofstream output;
  std::uint64_t bytes = 0;
};
H264FileWriter::H264FileWriter() : impl_(std::make_unique<Impl>()) {
}
H264FileWriter::~H264FileWriter() {
  Close();
}
bool H264FileWriter::Open(const std::string& path, std::string* error) {
  if (path.empty()) {
    SetError(error, "H264 output path is empty");
    return false;
  }
  impl_->output.close();
  impl_->output.clear();
  impl_->output.open(path, std::ios::binary | std::ios::trunc);
  impl_->bytes = 0;
  if (!impl_->output) {
    SetError(error, "cannot open H264 output: " + path);
    return false;
  }
  return true;
}
bool H264FileWriter::Write(const H264Packet& packet, std::string* error) {
  if (!impl_->output || packet.data.empty()) {
    SetError(error, "H264 output is not open or packet is empty");
    return false;
  }
  impl_->output.write(reinterpret_cast<const char*>(packet.data.data()),
                      static_cast<std::streamsize>(packet.data.size()));
  if (!impl_->output) {
    SetError(error, "write H264 packet failed");
    return false;
  }
  impl_->bytes += packet.data.size();
  return true;
}
bool H264FileWriter::Close(std::string* error) {
  if (!impl_->output.is_open()) return true;
  impl_->output.flush();
  if (!impl_->output) {
    SetError(error, "flush H264 output failed");
    impl_->output.close();
    return false;
  }
  impl_->output.close();
  return true;
}
std::uint64_t H264FileWriter::bytes_written() const {
  return impl_->bytes;
}
}  // namespace cockpit::camera

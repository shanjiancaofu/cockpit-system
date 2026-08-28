#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

namespace cockpit::camera {
namespace {

// NVIDIA Jetson tegra-video private control exposed by v4l2-ctl as bypass_mode.
constexpr std::uint32_t kTegraCameraBypassControl = 0x009a2064U;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

std::string ErrorMessage(const std::string& action) {
  return action + ": " + std::strerror(errno);
}

int Xioctl(int fd, unsigned long request, void* arg) {
  int result = 0;
  do {
    result = ioctl(fd, request, arg);
  } while (result < 0 && errno == EINTR);
  return result;
}

}  // namespace

V4l2MmapCapture::~V4l2MmapCapture() {
  Stop();
}

bool V4l2MmapCapture::Start(const V4l2MmapConfig& config, std::string* error) {
  Stop();
  if (config.device.empty() || config.width == 0 || config.height == 0 || config.fps == 0 ||
      config.buffer_count < 2) {
    SetError(error, "invalid V4L2 MMAP configuration");
    return false;
  }
  fd_ = open(config.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) {
    SetError(error, ErrorMessage("open " + config.device));
    return false;
  }
  config_ = config;

  v4l2_capability capability{};
  if (Xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
    SetError(error, ErrorMessage("VIDIOC_QUERYCAP"));
    Stop();
    return false;
  }
  const std::uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
                                 ? capability.device_caps
                                 : capability.capabilities;
  if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0 || (caps & V4L2_CAP_STREAMING) == 0) {
    SetError(error, "V4L2 device lacks single-plane capture or streaming capability");
    Stop();
    return false;
  }
  driver_ = reinterpret_cast<const char*>(capability.driver);
  card_ = reinterpret_cast<const char*>(capability.card);

  if (config.set_tegra_bypass_mode) {
    v4l2_control bypass{};
    bypass.id = kTegraCameraBypassControl;
    bypass.value = 0;
    if (Xioctl(fd_, VIDIOC_S_CTRL, &bypass) < 0 && errno != EINVAL && errno != ENOTTY) {
      SetError(error, ErrorMessage("set tegra bypass_mode=0"));
      Stop();
      return false;
    }
  }

  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = config.width;
  format.fmt.pix.height = config.height;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGGB10;
  format.fmt.pix.field = V4L2_FIELD_NONE;
  if (Xioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
    SetError(error, ErrorMessage("VIDIOC_S_FMT RG10"));
    Stop();
    return false;
  }
  if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_SRGGB10) {
    SetError(error, "V4L2 driver selected an unexpected pixel format");
    Stop();
    return false;
  }
  width_ = format.fmt.pix.width;
  height_ = format.fmt.pix.height;
  bytes_per_line_ = format.fmt.pix.bytesperline;
  size_image_ = format.fmt.pix.sizeimage;

  v4l2_streamparm streamparm{};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  streamparm.parm.capture.timeperframe.numerator = 1;
  streamparm.parm.capture.timeperframe.denominator = config.fps;
  if (Xioctl(fd_, VIDIOC_S_PARM, &streamparm) < 0 && errno != EINVAL) {
    SetError(error, ErrorMessage("VIDIOC_S_PARM"));
    Stop();
    return false;
  }

  v4l2_requestbuffers request{};
  request.count = config.buffer_count;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (Xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
    SetError(error, ErrorMessage("VIDIOC_REQBUFS"));
    Stop();
    return false;
  }
  if (request.count < 2) {
    SetError(error, "V4L2 driver returned fewer than two MMAP buffers");
    Stop();
    return false;
  }
  buffers_.resize(request.count);
  for (std::uint32_t index = 0; index < request.count; ++index) {
    v4l2_buffer buffer{};
    buffer.type = request.type;
    buffer.memory = request.memory;
    buffer.index = index;
    if (Xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
      SetError(error, ErrorMessage("VIDIOC_QUERYBUF"));
      Stop();
      return false;
    }
    buffers_[index].length = buffer.length;
    buffers_[index].start =
        mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);
    if (buffers_[index].start == MAP_FAILED) {
      buffers_[index].start = nullptr;
      SetError(error, ErrorMessage("mmap V4L2 buffer"));
      Stop();
      return false;
    }
    if (Xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
      SetError(error, ErrorMessage("VIDIOC_QBUF"));
      Stop();
      return false;
    }
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (Xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
    SetError(error, ErrorMessage("VIDIOC_STREAMON"));
    Stop();
    return false;
  }
  streaming_ = true;
  return true;
}

bool V4l2MmapCapture::WaitFrame(V4l2RawFrame* frame, int timeout_ms, std::string* error) {
  if (!running() || frame == nullptr || timeout_ms <= 0) {
    SetError(error, "invalid V4L2 WaitFrame request");
    return false;
  }
  pollfd descriptor{fd_, POLLIN | POLLERR, 0};
  int poll_result;
  do {
    poll_result = poll(&descriptor, 1, timeout_ms);
  } while (poll_result < 0 && errno == EINTR);
  if (poll_result == 0) {
    SetError(error, "V4L2 frame poll timed out");
    return false;
  }
  if (poll_result < 0) {
    SetError(error, ErrorMessage("poll V4L2 frame"));
    return false;
  }
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  if (Xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
    if (errno == EAGAIN) {
      SetError(error, "V4L2 frame was not ready after poll");
    } else {
      SetError(error, ErrorMessage("VIDIOC_DQBUF"));
    }
    return false;
  }
  if (buffer.index >= buffers_.size() || buffer.bytesused > buffers_[buffer.index].length) {
    SetError(error, "V4L2 returned an invalid buffer index or bytesused");
    return false;
  }
  frame->width = width_;
  frame->height = height_;
  frame->bytes_per_line = bytes_per_line_;
  frame->bytes_used = buffer.bytesused;
  frame->sequence = buffer.sequence;
  frame->timestamp_ns = static_cast<std::int64_t>(buffer.timestamp.tv_sec) * 1000000000LL +
                        static_cast<std::int64_t>(buffer.timestamp.tv_usec) * 1000LL;
  frame->data.assign(
      static_cast<const std::uint8_t*>(buffers_[buffer.index].start),
      static_cast<const std::uint8_t*>(buffers_[buffer.index].start) + buffer.bytesused);
  if (Xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
    SetError(error, ErrorMessage("VIDIOC_QBUF after frame"));
    return false;
  }
  return true;
}

void V4l2MmapCapture::Stop() {
  if (fd_ < 0) return;
  if (streaming_) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    Xioctl(fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
  }
  for (auto& buffer : buffers_) {
    if (buffer.start != nullptr) munmap(buffer.start, buffer.length);
  }
  buffers_.clear();
  close(fd_);
  fd_ = -1;
  width_ = height_ = bytes_per_line_ = size_image_ = 0;
  driver_.clear();
  card_.clear();
}

bool V4l2MmapCapture::running() const {
  return fd_ >= 0 && streaming_;
}

}  // namespace cockpit::camera

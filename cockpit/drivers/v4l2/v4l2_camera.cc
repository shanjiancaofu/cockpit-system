#include "cockpit/drivers/v4l2/v4l2_camera.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

namespace cockpit {
namespace camera {
namespace {

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) : fd_(fd) {
  }
  ~FileDescriptor() {
    Close();
  }

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  FileDescriptor(FileDescriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {
  }

  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      Close();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  int get() const {
    return fd_;
  }
  bool valid() const {
    return fd_ >= 0;
  }

 private:
  void Close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int fd_ = -1;
};

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::string SystemError(const std::string& action) {
  return action + ": " + std::strerror(errno);
}

int Xioctl(int fd, unsigned long request, void* arg) {
  int result = -1;
  while (true) {
    result = ::ioctl(fd, request, arg);
    if (result >= 0 || errno != EINTR) {
      break;
    }
  }
  return result;
}

std::string FourccToString(std::uint32_t fourcc) {
  std::string result(4, ' ');
  result[0] = static_cast<char>(fourcc & 0xff);
  result[1] = static_cast<char>((fourcc >> 8) & 0xff);
  result[2] = static_cast<char>((fourcc >> 16) & 0xff);
  result[3] = static_cast<char>((fourcc >> 24) & 0xff);
  return result;
}

bool StartsWithVideo(const char* name) {
  return std::strncmp(name, "video", 5) == 0;
}

std::vector<std::string> EnumerateVideoPaths(std::string* error) {
  std::vector<std::string> paths;
  DIR* directory = ::opendir("/dev");
  if (directory == nullptr) {
    SetError(error, SystemError("open /dev failed"));
    return paths;
  }

  while (dirent* entry = ::readdir(directory)) {
    if (StartsWithVideo(entry->d_name)) {
      paths.emplace_back(std::string("/dev/") + entry->d_name);
    }
  }
  ::closedir(directory);
  std::sort(paths.begin(), paths.end());
  return paths;
}

bool QueryDevice(const std::string& path, VideoDeviceInfo* info) {
  info->path = path;
  FileDescriptor fd(::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
  if (!fd.valid()) {
    info->error = SystemError("open " + path + " failed");
    return false;
  }

  struct v4l2_capability capability {};
  if (Xioctl(fd.get(), VIDIOC_QUERYCAP, &capability) < 0) {
    info->error = SystemError("query capability for " + path + " failed");
    return false;
  }

  const std::uint32_t caps = capability.capabilities;
  const std::uint32_t effective_caps =
      (caps & V4L2_CAP_DEVICE_CAPS) != 0 ? capability.device_caps : caps;

  info->driver = reinterpret_cast<const char*>(capability.driver);
  info->card = reinterpret_cast<const char*>(capability.card);
  info->bus_info = reinterpret_cast<const char*>(capability.bus_info);
  info->capabilities = caps;
  info->device_caps = effective_caps;
  info->query_ok = true;
  info->supports_capture =
      (effective_caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) != 0;
  info->supports_streaming = (effective_caps & V4L2_CAP_STREAMING) != 0;
  return true;
}

std::vector<FrameSizeInfo> ListFrameSizes(int fd, std::uint32_t fourcc) {
  std::vector<FrameSizeInfo> sizes;
  struct v4l2_frmsizeenum frame_size {};
  frame_size.pixel_format = fourcc;

  for (frame_size.index = 0; Xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0;
       ++frame_size.index) {
    if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      sizes.push_back({frame_size.discrete.width, frame_size.discrete.height});
    }
  }
  return sizes;
}

void AppendFormatsForType(int fd, v4l2_buf_type type, std::vector<PixelFormatInfo>* formats) {
  struct v4l2_fmtdesc format {};
  format.type = type;

  for (format.index = 0; Xioctl(fd, VIDIOC_ENUM_FMT, &format) == 0; ++format.index) {
    PixelFormatInfo info;
    info.fourcc = format.pixelformat;
    info.fourcc_text = FourccToString(format.pixelformat);
    info.description = reinterpret_cast<const char*>(format.description);
    info.frame_sizes = ListFrameSizes(fd, format.pixelformat);
    formats->push_back(std::move(info));
  }
}

}  // namespace

std::vector<VideoDeviceInfo> V4l2Camera::ListDevices(std::string* error) {
  std::vector<VideoDeviceInfo> devices;
  const auto paths = EnumerateVideoPaths(error);
  if (paths.empty()) {
    return devices;
  }

  for (const auto& path : paths) {
    VideoDeviceInfo info;
    QueryDevice(path, &info);
    devices.push_back(std::move(info));
  }
  return devices;
}

std::vector<PixelFormatInfo> V4l2Camera::ListFormats(const std::string& device_path,
                                                     std::string* error) {
  std::vector<PixelFormatInfo> formats;
  if (device_path.empty()) {
    SetError(error, "V4L2 device path is empty");
    return formats;
  }

  FileDescriptor fd(::open(device_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
  if (!fd.valid()) {
    SetError(error, SystemError("open " + device_path + " failed"));
    return formats;
  }

  AppendFormatsForType(fd.get(), V4L2_BUF_TYPE_VIDEO_CAPTURE, &formats);
  AppendFormatsForType(fd.get(), V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &formats);
  return formats;
}

}  // namespace camera
}  // namespace cockpit

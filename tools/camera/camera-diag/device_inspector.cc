#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <sys/utsname.h>

#include <cerrno>
#include <cstring>

#include "diagnostics.h"

#include "cockpit/drivers/v4l2/v4l2_camera.h"

namespace cockpit::camera_diag {
Json InspectEnvironment() {
  utsname kernel{};
  uname(&kernel);
  const auto module_path = RunCommand({"modinfo", "-F", "filename", "nv_imx219"});
  const auto module_vermagic = RunCommand({"modinfo", "-F", "vermagic", "nv_imx219"});
  const auto packages = RunCommand({"dpkg-query", "-W", "nvidia-l4t-core", "nvidia-l4t-camera"});
  return Object({{"bsp", Quote(ReadText("/etc/nv_tegra_release"))},
                 {"kernel", Quote(kernel.release)},
                 {"architecture", Quote(kernel.machine)},
                 {"imx219_module_version", Quote(ReadText("/sys/module/nv_imx219/version"))},
                 {"imx219_module_srcversion", Quote(ReadText("/sys/module/nv_imx219/srcversion"))},
                 {"imx219_module_path", module_path.code == 0 ? Quote(module_path.output) : "null"},
                 {"imx219_module_vermagic",
                  module_vermagic.code == 0 ? Quote(module_vermagic.output) : "null"},
                 {"bsp_packages", Object({{"returncode", Number(packages.code)},
                                          {"output", Quote(packages.output)}})},
                 {"driver_version_note", Quote("Empty module version means unavailable; "
                                               "video/media versions are reported per device")}});
}
Json InspectDevices(std::vector<std::string>* errors) {
  std::string error;
  const auto devices = camera::V4l2Camera::ListDevices(&error);
  if (!error.empty()) errors->push_back(error);
  std::vector<Json> result;
  for (const auto& device : devices) {
    if (!device.query_ok) errors->push_back(device.path + ": " + device.error);
    Fd fd(open(device.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    v4l2_capability cap{};
    const bool have_version = fd.value >= 0 && Ioctl(fd.value, VIDIOC_QUERYCAP, &cap) == 0;
    result.push_back(Object({{"path", Quote(device.path)},
                             {"query_ok", Boolean(device.query_ok)},
                             {"driver", Quote(device.driver)},
                             {"driver_version", have_version ? Number(cap.version) : "null"},
                             {"card", Quote(device.card)},
                             {"bus_info", Quote(device.bus_info)},
                             {"capture", Boolean(device.supports_capture)},
                             {"streaming", Boolean(device.supports_streaming)},
                             {"capabilities", Number(device.device_caps)},
                             {"error", Quote(device.error)}}));
  }
  return Array(result);
}
Json InspectMedia(std::vector<std::string>* errors) {
  std::vector<Json> devices;
  for (const auto& path : DevicePaths("media")) {
    Fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
    media_device_info info{};
    if (fd.value < 0 || Ioctl(fd.value, MEDIA_IOC_DEVICE_INFO, &info) < 0) {
      const std::string error = path + ": " + std::strerror(errno);
      errors->push_back(error);
      devices.push_back(Object({{"path", Quote(path)}, {"error", Quote(error)}}));
      continue;
    }
    std::vector<Json> entities;
    media_entity_desc entity{};
    entity.id = MEDIA_ENT_ID_FLAG_NEXT;
    while (Ioctl(fd.value, MEDIA_IOC_ENUM_ENTITIES, &entity) == 0) {
      std::vector<media_pad_desc> pads(entity.pads);
      std::vector<media_link_desc> links(entity.links);
      media_links_enum topology{};
      topology.entity = entity.id;
      topology.pads = pads.data();
      topology.links = links.data();
      std::vector<Json> pad_json, link_json;
      if (Ioctl(fd.value, MEDIA_IOC_ENUM_LINKS, &topology) < 0) {
        errors->push_back(path + " enumerate links: " + std::strerror(errno));
      } else {
        for (const auto& pad : pads)
          pad_json.push_back(Object({{"index", Number(pad.index)}, {"flags", Number(pad.flags)}}));
        for (const auto& link : links)
          link_json.push_back(
              Object({{"source_entity", Number(link.source.entity)},
                      {"source_pad", Number(link.source.index)},
                      {"sink_entity", Number(link.sink.entity)},
                      {"sink_pad", Number(link.sink.index)},
                      {"flags", Number(link.flags)},
                      {"enabled", Boolean((link.flags & MEDIA_LNK_FL_ENABLED) != 0)}}));
      }
      entities.push_back(Object({{"id", Number(entity.id)},
                                 {"name", Quote(entity.name)},
                                 {"type", Number(entity.type)},
                                 {"device_major", Number(entity.dev.major)},
                                 {"device_minor", Number(entity.dev.minor)},
                                 {"pads", Array(pad_json)},
                                 {"links", Array(link_json)}}));
      entity.id |= MEDIA_ENT_ID_FLAG_NEXT;
    }
    if (errno != EINVAL) errors->push_back(path + " enumerate entities: " + std::strerror(errno));
    devices.push_back(Object({{"path", Quote(path)},
                              {"driver", Quote(info.driver)},
                              {"model", Quote(info.model)},
                              {"driver_version", Number(info.driver_version)},
                              {"media_version", Number(info.media_version)},
                              {"entities", Array(entities)}}));
  }
  return Array(devices);
}
}  // namespace cockpit::camera_diag

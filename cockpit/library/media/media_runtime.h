#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace media {

class MediaGrpcService;
class MediaService;

class MediaRuntime final {
 public:
  MediaRuntime();
  ~MediaRuntime();

  MediaRuntime(const MediaRuntime&) = delete;
  MediaRuntime& operator=(const MediaRuntime&) = delete;

  bool Start(const std::string& config_path);
  void Stop();
  int Poll() const;

 private:
  std::unique_ptr<MediaService> service_;
  std::unique_ptr<MediaGrpcService> grpc_;
};

}  // namespace media
}  // namespace cockpit

#pragma once

#include "audio.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

namespace cockpit {
namespace audio {

class AudioControlClient {
 public:
  explicit AudioControlClient(const std::string& address);

  bool StartCapture(const std::string& input_device,
                    proto::audio::AudioStatus* status,
                    std::string* error);
  bool StopCapture(proto::audio::AudioStatus* status, std::string* error);
  bool GetStatus(proto::audio::AudioStatus* status, std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::audio::AudioControl::Stub> stub_;
};

}  // namespace audio
}  // namespace cockpit

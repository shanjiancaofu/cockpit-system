#include "cockpit/driver/camera/encode/h264_encoder.h"

namespace cockpit::camera {
namespace {
void Unsupported(std::string* error) {
  if (error) *error = "H264 encoder dependencies are unavailable";
}
}  // namespace
struct H264Encoder::Impl {};
H264Encoder::H264Encoder() : impl_(std::make_unique<Impl>()) {
}
H264Encoder::~H264Encoder() = default;
bool H264Encoder::IsSupported() {
  return false;
}
bool H264Encoder::Start(const H264EncoderConfig&, std::string* error) {
  Unsupported(error);
  return false;
}
bool H264Encoder::Encode(const CpuVideoFrame&, std::vector<H264Packet>*, std::string* error) {
  Unsupported(error);
  return false;
}
bool H264Encoder::Flush(std::vector<H264Packet>*, std::string* error) {
  Unsupported(error);
  return false;
}
void H264Encoder::Stop() {
}
bool H264Encoder::running() const {
  return false;
}
H264EncoderStats H264Encoder::stats() const {
  return {};
}

struct H264FileWriter::Impl {};
H264FileWriter::H264FileWriter() : impl_(std::make_unique<Impl>()) {
}
H264FileWriter::~H264FileWriter() = default;
bool H264FileWriter::Open(const std::string&, std::string* error) {
  Unsupported(error);
  return false;
}
bool H264FileWriter::Write(const H264Packet&, std::string* error) {
  Unsupported(error);
  return false;
}
bool H264FileWriter::Close(std::string*) {
  return true;
}
std::uint64_t H264FileWriter::bytes_written() const {
  return 0;
}
struct H264UdpPublisher::Impl {};
H264UdpPublisher::H264UdpPublisher() : impl_(std::make_unique<Impl>()) {}
H264UdpPublisher::~H264UdpPublisher() = default;
bool H264UdpPublisher::Open(const H264UdpPublisherConfig&, std::string* error) {
  Unsupported(error);
  return false;
}
bool H264UdpPublisher::Publish(const H264Packet&, std::string* error) {
  Unsupported(error);
  return false;
}
void H264UdpPublisher::Close() {}
H264UdpPublisherStats H264UdpPublisher::stats() const { return {}; }
}  // namespace cockpit::camera

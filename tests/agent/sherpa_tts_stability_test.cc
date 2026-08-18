#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "agent/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.h"
#include "cockpit/core/config/system_config.h"

namespace {

std::optional<std::uint64_t> ReadProcessMemoryKib(const std::string& field) {
  std::ifstream status("/proc/self/status");
  std::string line;
  const std::string prefix = field + ":";
  while (std::getline(status, line)) {
    if (line.rfind(prefix, 0U) != 0U) {
      continue;
    }
    std::istringstream value_stream(line.substr(prefix.size()));
    std::uint64_t value = 0U;
    std::string unit;
    if (value_stream >> value >> unit && unit == "kB") {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> ParseIterations(const char* value) {
  try {
    std::size_t parsed = 0U;
    const auto iterations = std::stoull(value, &parsed);
    if (value[parsed] != '\0' || iterations < 4U || iterations > 256U) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(iterations);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "usage: " << argv[0] << " [iterations]\n";
    return 2;
  }
  const auto iterations = argc == 2 ? ParseIterations(argv[1]) : std::optional<std::size_t>(16U);
  if (!iterations.has_value()) {
    std::cerr << "iterations must be an integer from 4 through 256\n";
    return 2;
  }

  constexpr std::size_t kWarmupIterations = 3U;
  constexpr std::uint64_t kMaxPostWarmupGrowthKib = 64U * 1024U;
  constexpr const char* kText =
      "摄像头已经打开。前方道路状态正常，请注意安全。Camera is ready for use.";

  cockpit::config::TtsConfig config;
  config.provider = "sherpa-kokoro";
  config.speaker_id = 3;
  config.speed = 1.0;

  try {
    auto synthesizer = cockpit::voice::CreateSherpaKokoroSpeechSynthesizer(config);
    const auto initial_rss = ReadProcessMemoryKib("VmRSS");
    std::optional<std::uint64_t> warmup_rss;
    std::uint64_t measured_peak_rss = 0U;
    std::uint64_t total_samples = 0U;
    std::chrono::milliseconds total_elapsed{0};

    for (std::size_t iteration = 1U; iteration <= *iterations; ++iteration) {
      const auto started = std::chrono::steady_clock::now();
      {
        const auto result = synthesizer->Synthesize(
            kText, std::chrono::steady_clock::now() + std::chrono::seconds(30));
        if (!result.success || result.audio.samples.empty() ||
            result.audio.format.sample_rate_hz != 24000 || result.audio.format.channels != 1) {
          std::cerr << "Kokoro stability synthesis failed at iteration " << iteration << ": "
                    << result.error << '\n';
          return 1;
        }
        total_samples += result.audio.samples.size();
      }
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      total_elapsed += elapsed;
      const auto rss = ReadProcessMemoryKib("VmRSS");
      if (!rss.has_value()) {
        std::cerr << "failed to read VmRSS at iteration " << iteration << '\n';
        return 1;
      }
      measured_peak_rss = std::max(measured_peak_rss, *rss);
      if (iteration == kWarmupIterations) {
        warmup_rss = rss;
      }
      std::cout << "iteration=" << iteration << " elapsed_ms=" << elapsed.count()
                << " rss_kib=" << *rss << '\n'
                << std::flush;
    }

    const auto final_rss = ReadProcessMemoryKib("VmRSS");
    const auto high_water_rss = ReadProcessMemoryKib("VmHWM");
    if (!initial_rss.has_value() || !warmup_rss.has_value() || !final_rss.has_value() ||
        !high_water_rss.has_value()) {
      std::cerr << "failed to read final process memory metrics\n";
      return 1;
    }
    const std::uint64_t post_warmup_growth =
        *final_rss > *warmup_rss ? *final_rss - *warmup_rss : 0U;
    std::cout << "Kokoro TTS stability passed: iterations=" << *iterations
              << " total_samples=" << total_samples
              << " average_ms=" << total_elapsed.count() / static_cast<long long>(*iterations)
              << " initial_rss_kib=" << *initial_rss << " warmup_rss_kib=" << *warmup_rss
              << " final_rss_kib=" << *final_rss << " measured_peak_rss_kib=" << measured_peak_rss
              << " vmhwm_kib=" << *high_water_rss
              << " post_warmup_growth_kib=" << post_warmup_growth << '\n';
    if (post_warmup_growth > kMaxPostWarmupGrowthKib) {
      std::cerr << "Kokoro RSS grew more than " << kMaxPostWarmupGrowthKib
                << " KiB after warm-up\n";
      return 1;
    }
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "Kokoro TTS stability setup failed: " << exception.what() << '\n';
    return 1;
  }
}

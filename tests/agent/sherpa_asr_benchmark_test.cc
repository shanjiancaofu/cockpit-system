#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "agent/speech/providers/sherpa/sherpa_sensevoice_recognizer.h"
#include "cockpit/modules/audio/wav/wav_file.h"
#include "cockpit/modules/voice/assistant/deterministic_command_router.h"
#include "cockpit/modules/voice/assistant/transcript_normalizer.h"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkCase {
  std::string name;
  std::filesystem::path relative_path;
  std::optional<std::string> reference;
  cockpit::voice::VoiceAction expected_action = cockpit::voice::VoiceAction::kNone;
};

struct LoadedCase {
  BenchmarkCase definition;
  cockpit::audio::SpeechSegment segment;
  double audio_ms = 0.0;
  std::optional<std::string> normalized_reference;
  std::optional<std::string> first_normalized_result;
};

std::filesystem::path AiRoot() {
  const char* root = std::getenv("COCKPIT_AI_ROOT");
  return root != nullptr && root[0] != '\0' ? std::filesystem::path(root)
                                            : std::filesystem::path("_output") / "ai";
}

std::optional<std::size_t> ParseRepetitions(const char* value) {
  try {
    std::size_t parsed = 0U;
    const auto repetitions = std::stoull(value, &parsed);
    if (value[parsed] != '\0' || repetitions == 0U || repetitions > 20U) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(repetitions);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

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

std::vector<std::uint32_t> ComparableCodePoints(const std::string& text) {
  std::vector<std::uint32_t> result;
  for (std::size_t offset = 0U; offset < text.size();) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::size_t size = 1U;
    std::uint32_t value = lead;
    if ((lead & 0xE0U) == 0xC0U && offset + 1U < text.size()) {
      size = 2U;
      value = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U && offset + 2U < text.size()) {
      size = 3U;
      value = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U && offset + 3U < text.size()) {
      size = 4U;
      value = lead & 0x07U;
    }
    for (std::size_t index = 1U; index < size; ++index) {
      value = (value << 6U) | (static_cast<unsigned char>(text[offset + index]) & 0x3FU);
    }
    offset += size;
    if (value != static_cast<std::uint32_t>(' ')) {
      result.push_back(value);
    }
  }
  return result;
}

std::size_t EditDistance(const std::vector<std::uint32_t>& expected,
                         const std::vector<std::uint32_t>& actual) {
  std::vector<std::size_t> previous(actual.size() + 1U);
  std::iota(previous.begin(), previous.end(), 0U);
  std::vector<std::size_t> current(actual.size() + 1U);
  for (std::size_t expected_index = 1U; expected_index <= expected.size(); ++expected_index) {
    current[0] = expected_index;
    for (std::size_t actual_index = 1U; actual_index <= actual.size(); ++actual_index) {
      const std::size_t substitution =
          previous[actual_index - 1U] +
          (expected[expected_index - 1U] == actual[actual_index - 1U] ? 0U : 1U);
      current[actual_index] =
          std::min({previous[actual_index] + 1U, current[actual_index - 1U] + 1U, substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

double Percentile95(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t index =
      std::min(values.size() - 1U, static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1U);
  return values[index];
}

bool LoadCase(const BenchmarkCase& definition, LoadedCase* loaded) {
  cockpit::audio::PcmBuffer buffer;
  std::string error;
  const auto path = AiRoot() / "fixtures" / definition.relative_path;
  if (!cockpit::audio::ReadPcm16Wav(path.string(), &buffer, &error)) {
    std::cerr << "failed to read ASR fixture " << path << ": " << error << '\n';
    return false;
  }
  if (buffer.format.sample_rate_hz != 16000 || buffer.format.channels != 1 ||
      buffer.samples.empty()) {
    std::cerr << "ASR fixture must be non-empty 16 kHz mono PCM16: " << path << '\n';
    return false;
  }
  loaded->definition = definition;
  loaded->segment.samples = std::move(buffer.samples);
  loaded->segment.end_sequence = static_cast<std::uint64_t>(loaded->segment.FrameCount());
  loaded->segment.end_time_ns =
      static_cast<std::int64_t>(loaded->segment.DurationMs() * 1'000'000U);
  loaded->audio_ms = static_cast<double>(loaded->segment.samples.size()) * 1000.0 / 16000.0;
  if (definition.reference.has_value()) {
    loaded->normalized_reference =
        cockpit::voice::TranscriptNormalizer::Normalize(*definition.reference);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "usage: " << argv[0] << " [repetitions]\n";
    return 2;
  }
  const auto repetitions = argc == 2 ? ParseRepetitions(argv[1]) : std::optional<std::size_t>(3U);
  if (!repetitions.has_value()) {
    std::cerr << "repetitions must be an integer from 1 through 20\n";
    return 2;
  }

  const std::vector<BenchmarkCase> definitions{
      {"open_camera_zh", "open-camera-zh.wav", "打开相机",
       cockpit::voice::VoiceAction::kOpenCamera},
      {"open_camera_en", "open-camera-en.wav", "open camera",
       cockpit::voice::VoiceAction::kOpenCamera},
      {"wake_and_command", "live/segment-02-wake-open-camera.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
      {"multi_command", "live/segment-03-multi-command.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
      {"vehicle_phrase", "live/segment-04-vehicle-status.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
      {"mixed_commands", "live/segment-05-mixed-commands.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
      {"negative_commands", "live/segment-06-negative-commands.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
      {"unsupported_stop", "live/segment-07-stop.wav", std::nullopt,
       cockpit::voice::VoiceAction::kNone},
  };
  std::vector<LoadedCase> cases(definitions.size());
  for (std::size_t index = 0U; index < definitions.size(); ++index) {
    if (!LoadCase(definitions[index], &cases[index])) {
      return 1;
    }
  }

  try {
    auto recognizer = cockpit::voice::CreateSherpaSenseVoiceRecognizer();
    const auto warmup =
        recognizer->Recognize(cases.front().segment, Clock::now() + std::chrono::seconds(30));
    if (!warmup.success || warmup.text.empty()) {
      std::cerr << "SenseVoice benchmark warm-up failed: " << warmup.error << '\n';
      return 1;
    }

    std::vector<double> latencies_ms;
    std::vector<double> real_time_factors;
    std::uint64_t sentence_correct = 0U;
    std::uint64_t sentence_total = 0U;
    std::uint64_t character_edits = 0U;
    std::uint64_t reference_characters = 0U;
    std::uint64_t route_correct = 0U;
    std::uint64_t route_total = 0U;
    std::uint64_t positive_route_correct = 0U;
    std::uint64_t positive_route_total = 0U;
    std::uint64_t safety_false_positives = 0U;
    std::uint64_t peak_rss_kib = 0U;
    std::optional<std::uint64_t> warmup_rss_kib;

    const cockpit::voice::DeterministicCommandRouter router;
    for (std::size_t repetition = 1U; repetition <= *repetitions; ++repetition) {
      for (auto& benchmark_case : cases) {
        const auto started = Clock::now();
        const auto result =
            recognizer->Recognize(benchmark_case.segment, Clock::now() + std::chrono::seconds(30));
        const double latency_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        if (!result.success || result.text.empty()) {
          std::cerr << "SenseVoice benchmark failed case=" << benchmark_case.definition.name
                    << " repetition=" << repetition << " error=" << result.error << '\n';
          return 1;
        }
        const std::string normalized = cockpit::voice::TranscriptNormalizer::Normalize(result.text);
        const auto route = router.Route(normalized);
        if (!benchmark_case.first_normalized_result.has_value()) {
          benchmark_case.first_normalized_result = normalized;
          std::cout << "case=" << benchmark_case.definition.name
                    << " audio_ms=" << std::lround(benchmark_case.audio_ms) << " transcript=\""
                    << result.text << "\" normalized=\"" << normalized
                    << "\" action=" << cockpit::voice::ToString(route.action) << '\n';
        } else if (*benchmark_case.first_normalized_result != normalized) {
          std::cerr << "SenseVoice produced non-deterministic normalized text for case="
                    << benchmark_case.definition.name << '\n';
          return 1;
        }

        latencies_ms.push_back(latency_ms);
        real_time_factors.push_back(latency_ms / benchmark_case.audio_ms);
        ++route_total;
        if (route.action == benchmark_case.definition.expected_action) {
          ++route_correct;
        } else {
          std::cerr << "route mismatch case=" << benchmark_case.definition.name << " expected="
                    << cockpit::voice::ToString(benchmark_case.definition.expected_action)
                    << " actual=" << cockpit::voice::ToString(route.action) << '\n';
          return 1;
        }
        if (benchmark_case.definition.expected_action == cockpit::voice::VoiceAction::kNone) {
          if (route.action != cockpit::voice::VoiceAction::kNone) {
            ++safety_false_positives;
          }
        } else {
          ++positive_route_total;
          if (route.action == benchmark_case.definition.expected_action) {
            ++positive_route_correct;
          }
        }

        if (benchmark_case.normalized_reference.has_value()) {
          ++sentence_total;
          if (normalized == *benchmark_case.normalized_reference) {
            ++sentence_correct;
          }
          const auto reference_points = ComparableCodePoints(*benchmark_case.normalized_reference);
          const auto actual_points = ComparableCodePoints(normalized);
          character_edits += EditDistance(reference_points, actual_points);
          reference_characters += reference_points.size();
        }
        const auto rss = ReadProcessMemoryKib("VmRSS");
        if (!rss.has_value()) {
          std::cerr << "failed to read SenseVoice benchmark RSS\n";
          return 1;
        }
        peak_rss_kib = std::max(peak_rss_kib, *rss);
      }
      if (repetition == 1U) {
        warmup_rss_kib = ReadProcessMemoryKib("VmRSS");
      }
    }

    const auto final_rss_kib = ReadProcessMemoryKib("VmRSS");
    const auto high_water_rss_kib = ReadProcessMemoryKib("VmHWM");
    if (!warmup_rss_kib.has_value() || !final_rss_kib.has_value() ||
        !high_water_rss_kib.has_value()) {
      std::cerr << "failed to read final SenseVoice memory metrics\n";
      return 1;
    }
    const std::uint64_t post_warmup_growth_kib =
        *final_rss_kib > *warmup_rss_kib ? *final_rss_kib - *warmup_rss_kib : 0U;
    if (post_warmup_growth_kib > 64U * 1024U) {
      std::cerr << "SenseVoice RSS grew more than 64 MiB after the first dataset pass\n";
      return 1;
    }

    cockpit::audio::SpeechSegment empty_segment;
    const auto empty = recognizer->Recognize(empty_segment, Clock::now() + std::chrono::seconds(1));
    const auto expired = recognizer->Recognize(cases.front().segment, Clock::now());
    if (empty.success || empty.error.empty() || expired.success || expired.error.empty()) {
      std::cerr << "SenseVoice invalid input or expired deadline was not rejected clearly\n";
      return 1;
    }

    const double average_latency = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0) /
                                   static_cast<double>(latencies_ms.size());
    const double average_rtf =
        std::accumulate(real_time_factors.begin(), real_time_factors.end(), 0.0) /
        static_cast<double>(real_time_factors.size());
    const double character_error_rate =
        reference_characters == 0U
            ? 0.0
            : static_cast<double>(character_edits) / static_cast<double>(reference_characters);
    std::cout << "SenseVoice ASR benchmark passed: repetitions=" << *repetitions
              << " cases=" << cases.size() << " measurements=" << latencies_ms.size()
              << " latency_avg_ms=" << std::lround(average_latency)
              << " latency_p95_ms=" << std::lround(Percentile95(latencies_ms))
              << " rtf_avg=" << average_rtf << " sentence_accuracy=" << sentence_correct << '/'
              << sentence_total << " cer=" << character_error_rate
              << " route_accuracy=" << route_correct << '/' << route_total
              << " positive_route_accuracy=" << positive_route_correct << '/'
              << positive_route_total << " safety_false_positives=" << safety_false_positives
              << " warmup_rss_kib=" << *warmup_rss_kib << " final_rss_kib=" << *final_rss_kib
              << " peak_rss_kib=" << peak_rss_kib << " vmhwm_kib=" << *high_water_rss_kib
              << " post_warmup_growth_kib=" << post_warmup_growth_kib << '\n';
    std::cout << "dataset gaps: exact transcript references=2, play_music=missing, "
                 "allowlisted_vehicle_status=missing, numbers_units=missing\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "SenseVoice ASR benchmark setup failed: " << exception.what() << '\n';
    return 1;
  }
}

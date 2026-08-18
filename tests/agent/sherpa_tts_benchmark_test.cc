#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "agent/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.h"
#include "agent/speech/tts/speech_text_segmenter.h"
#include "cockpit/core/config/system_config.h"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkCase {
  std::string name;
  std::string text;
};

struct Measurement {
  double synthesis_ms = 0.0;
  double audio_ms = 0.0;
  double rtf = 0.0;
};

std::optional<std::size_t> ParseRepetitions(const char* value) {
  try {
    std::size_t parsed = 0U;
    const auto repetitions = std::stoull(value, &parsed);
    if (value[parsed] != '\0' || repetitions == 0U || repetitions > 10U) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(repetitions);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool IsValidAudio(const cockpit::voice::SpeechSynthesisResult& result) {
  return result.success && !result.audio.samples.empty() &&
         result.audio.format.sample_rate_hz == 24000 && result.audio.format.channels == 1;
}

std::optional<Measurement> Measure(cockpit::voice::SpeechSynthesizer* synthesizer,
                                   const std::string& text, std::string* error) {
  const auto started = Clock::now();
  const auto result = synthesizer->Synthesize(text, Clock::now() + std::chrono::seconds(30));
  const double synthesis_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  if (!IsValidAudio(result)) {
    if (error != nullptr) {
      *error = result.error.empty() ? "Kokoro returned invalid audio" : result.error;
    }
    return std::nullopt;
  }
  const double audio_ms = static_cast<double>(result.audio.samples.size()) * 1000.0 /
                          static_cast<double>(result.audio.format.sample_rate_hz);
  return Measurement{synthesis_ms, audio_ms, synthesis_ms / audio_ms};
}

double Average(const std::vector<double>& values) {
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double Percentile95(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t index =
      std::min(values.size() - 1U, static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1U);
  return values[index];
}

void PrintSummary(const std::string& name, const std::vector<Measurement>& measurements) {
  std::vector<double> synthesis;
  std::vector<double> audio;
  std::vector<double> rtf;
  synthesis.reserve(measurements.size());
  audio.reserve(measurements.size());
  rtf.reserve(measurements.size());
  for (const auto& measurement : measurements) {
    synthesis.push_back(measurement.synthesis_ms);
    audio.push_back(measurement.audio_ms);
    rtf.push_back(measurement.rtf);
  }
  std::cout << "case=" << name << " samples=" << measurements.size()
            << " synthesis_avg_ms=" << std::lround(Average(synthesis))
            << " synthesis_p95_ms=" << std::lround(Percentile95(synthesis))
            << " audio_avg_ms=" << std::lround(Average(audio)) << " rtf_avg=" << Average(rtf)
            << " rtf_p95=" << Percentile95(rtf) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "usage: " << argv[0] << " [repetitions]\n";
    return 2;
  }
  const auto repetitions = argc == 2 ? ParseRepetitions(argv[1]) : std::optional<std::size_t>(3U);
  if (!repetitions.has_value()) {
    std::cerr << "repetitions must be an integer from 1 through 10\n";
    return 2;
  }

  const std::vector<BenchmarkCase> cases{
      {"short_zh", "摄像头已经打开。"},
      {"short_en", "Camera view is open."},
      {"mixed", "摄像头已经打开，camera is ready."},
      {"numbers_units", "当前温度 25 摄氏度，电量 86%。"},
      {"punctuation", "请稍候，系统正在检查；检查完成！"},
      {"long_response",
       "摄像头已经打开。前方道路状态正常，请注意安全。车辆电量为百分之八十六。Camera "
       "view is ready."},
  };

  cockpit::config::TtsConfig config;
  config.provider = "sherpa-kokoro";
  config.speaker_id = 3;
  config.speed = 1.0;

  try {
    auto synthesizer = cockpit::voice::CreateSherpaKokoroSpeechSynthesizer(config);
    std::string error;
    if (!Measure(synthesizer.get(), cases.front().text, &error).has_value()) {
      std::cerr << "Kokoro benchmark warm-up failed: " << error << '\n';
      return 1;
    }

    std::vector<Measurement> all_measurements;
    for (const auto& benchmark_case : cases) {
      std::vector<Measurement> measurements;
      for (std::size_t repetition = 0U; repetition < *repetitions; ++repetition) {
        auto measurement = Measure(synthesizer.get(), benchmark_case.text, &error);
        if (!measurement.has_value()) {
          std::cerr << "Kokoro benchmark failed case=" << benchmark_case.name
                    << " repetition=" << repetition + 1U << " error=" << error << '\n';
          return 1;
        }
        measurements.push_back(*measurement);
        all_measurements.push_back(*measurement);
      }
      PrintSummary(benchmark_case.name, measurements);
    }
    PrintSummary("overall", all_measurements);

    const auto& long_text = cases.back().text;
    const auto segments = cockpit::voice::SplitSpeechText(long_text);
    if (segments.size() < 2U) {
      std::cerr << "long benchmark response was not segmented\n";
      return 1;
    }
    std::vector<Measurement> full_measurements;
    std::vector<Measurement> first_segment_measurements;
    for (std::size_t repetition = 0U; repetition < *repetitions; ++repetition) {
      auto full = Measure(synthesizer.get(), long_text, &error);
      auto first = Measure(synthesizer.get(), segments.front(), &error);
      if (!full.has_value() || !first.has_value()) {
        std::cerr << "segmented latency benchmark failed: " << error << '\n';
        return 1;
      }
      full_measurements.push_back(*full);
      first_segment_measurements.push_back(*first);
    }
    PrintSummary("whole_long_response", full_measurements);
    PrintSummary("first_playable_segment", first_segment_measurements);
    std::vector<double> full_latency;
    std::vector<double> first_latency;
    for (std::size_t index = 0U; index < full_measurements.size(); ++index) {
      full_latency.push_back(full_measurements[index].synthesis_ms);
      first_latency.push_back(first_segment_measurements[index].synthesis_ms);
    }
    if (Average(first_latency) >= Average(full_latency) ||
        first_segment_measurements.front().audio_ms >= full_measurements.front().audio_ms) {
      std::cerr << "segmentation did not reduce first playable response latency or audio span\n";
      return 1;
    }

    const auto empty = synthesizer->Synthesize("", Clock::now() + std::chrono::seconds(1));
    if (empty.success || empty.error.empty()) {
      std::cerr << "empty TTS input was not rejected clearly\n";
      return 1;
    }
    const auto expired = synthesizer->Synthesize("系统状态正常。", Clock::now());
    if (expired.success || expired.error.empty()) {
      std::cerr << "expired TTS deadline was not rejected clearly\n";
      return 1;
    }
    const auto abnormal = synthesizer->Synthesize("系统状态正常，符号测试：@#%🙂。",
                                                  Clock::now() + std::chrono::seconds(30));
    if (!abnormal.success && abnormal.error.empty()) {
      std::cerr << "abnormal TTS input failed without a diagnostic\n";
      return 1;
    }
    if (abnormal.success && !IsValidAudio(abnormal)) {
      std::cerr << "abnormal TTS input returned invalid PCM\n";
      return 1;
    }

    const std::string cancellation_text = long_text + long_text + long_text + long_text;
    cockpit::voice::SpeechSynthesisResult cancelled;
    const auto cancellation_started = Clock::now();
    std::thread synthesis_thread([&] {
      cancelled =
          synthesizer->Synthesize(cancellation_text, Clock::now() + std::chrono::seconds(30));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    synthesizer->Cancel();
    synthesis_thread.join();
    const auto cancellation_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - cancellation_started);
    if (cancelled.success || cancelled.error.empty() ||
        cancellation_elapsed > std::chrono::seconds(5)) {
      std::cerr << "real Kokoro cancellation was not bounded and explicit\n";
      return 1;
    }
    const auto recovery = Measure(synthesizer.get(), cases.front().text, &error);
    if (!recovery.has_value()) {
      std::cerr << "Kokoro did not recover after cancellation: " << error << '\n';
      return 1;
    }
    std::cout << "cancellation_elapsed_ms=" << cancellation_elapsed.count()
              << " recovery_synthesis_ms=" << std::lround(recovery->synthesis_ms) << '\n';
    std::cout << "Kokoro TTS benchmark passed: repetitions=" << *repetitions
              << " cases=" << cases.size() << " segments=" << segments.size() << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "Kokoro TTS benchmark setup failed: " << exception.what() << '\n';
    return 1;
  }
}

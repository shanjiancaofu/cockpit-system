#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "agent/audio/audio_stream_client.h"
#include "agent/interaction/voice_interaction_service.h"
#include "agent/speech/asr/mock_speech_recognizer.h"
#include "agent/speech/pipeline/speech_pipeline.h"
#include "agent/speech/vad/mock_voice_activity_detector.h"
#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

int main() {
  const std::string socket_path =
      "/tmp/cockpit-voice-pcm-e2e-" + std::to_string(static_cast<long long>(getpid())) + ".sock";
  cockpit::audio::AudioStreamPublisher publisher;
  std::string error;
  if (!publisher.Start(socket_path, &error)) {
    std::cerr << "failed to start PCM publisher: " << error << '\n';
    return 1;
  }

  cockpit::agent::AudioStreamClient client;
  if (!client.Connect(socket_path, &error)) {
    std::cerr << "failed to connect PCM client: " << error << '\n';
    return 1;
  }

  cockpit::voice::VoiceInteractionService interaction(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(), nullptr);
  if (!interaction.Start()) {
    std::cerr << "failed to start voice interaction service\n";
    return 1;
  }

  cockpit::config::AudioConfig audio_config;
  cockpit::config::SpeechSegmentConfig segment_config;
  segment_config.pre_roll_ms = 0;
  segment_config.max_segment_ms = 100;
  cockpit::agent::SpeechPipeline pipeline(
      audio_config, segment_config, std::make_unique<cockpit::agent::MockVoiceActivityDetector>(),
      std::make_unique<cockpit::voice::MockSpeechRecognizer>());
  if (!pipeline.Start(
          [&interaction](const cockpit::voice::SpeechTranscript& transcript) {
            interaction.SubmitTranscript(transcript);
          },
          &error)) {
    std::cerr << "failed to start speech pipeline: " << error << '\n';
    return 1;
  }

  std::thread receiver([&client, &pipeline] {
    for (int count = 0; count < 5; ++count) {
      auto received = client.ReceiveFrame(1000);
      if (received.status != cockpit::agent::AudioStreamReceiveStatus::kFrame ||
          !received.frame.has_value()) {
        return;
      }
      pipeline.Submit(*received.frame);
    }
  });

  for (std::uint64_t sequence = 0; sequence < 5; ++sequence) {
    cockpit::audio::AudioFrame::Samples samples{};
    samples.fill(10000);
    publisher.Publish(cockpit::audio::AudioFrame(sequence,
                                                 static_cast<std::int64_t>(sequence * 20000000ULL),
                                                 cockpit::audio::AudioFrameFlag::kNone, samples));
  }
  receiver.join();

  cockpit::voice::VoiceResponse response;
  const bool completed = interaction.WaitForResponse(0, std::chrono::seconds(1), &response);
  pipeline.Stop();
  interaction.Stop();
  client.Close();
  publisher.Stop();

  const auto speech_metrics = pipeline.metrics();
  const auto interaction_status = interaction.status();
  if (!completed || response.transcript_text != "mock transcript duration_ms=100" ||
      speech_metrics.frames_processed != 5 || speech_metrics.segments_completed != 1 ||
      speech_metrics.transcripts_published != 1 ||
      interaction_status.metrics.transcripts_received != 1 ||
      interaction_status.metrics.responses_published != 1) {
    std::cerr << "PCM to VAD/ASR/Agent path did not complete\n";
    return 1;
  }
  std::cout << "voice PCM end-to-end test passed\n";
  return 0;
}

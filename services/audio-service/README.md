# audio-service

Jetson-local microphone and speaker owner with a gRPC control plane.

```text
audio-service/
├── capture/     # capture lifecycle module
├── processing/  # VAD, segmentation and ASR orchestration
├── playback/    # TTS queue and speaker output
├── grpc/        # control/status API
└── main.cc      # process assembly
```

The service owns `AudioCaptureStream`, its ALSA source, and the ring's single VAD consumer. gRPC
exposes start, stop, status, and metrics only; raw PCM remains in the local SPSC data plane. ASR
providers consume speech-segment output from the VAD pipeline, not the frame ring as a second
consumer.

The VAD worker also owns `SpeechSegmenter`. Completed segments include configurable pre-roll and
are placed in an eight-entry local SPSC queue. `TryPopSpeechSegment()` is reserved for the single
in-process ASR consumer.

When `features.voice.enabled` is true, the configured recognizer consumes that segment queue.
Transcripts are retained in a bounded 32-event in-memory history and streamed through gRPC;
segment PCM remains local.

`mock` is the default recognizer. The optional `whisper_cpp` provider requires a build with
`BUILD_WHISPER_CPP_ASR=ON`, a local whisper.cpp source checkout, and a deployed model configured by
`features.voice.asr_model_path`.

`Speak(text)` accepts response text over gRPC. A bounded worker queue runs the configured TTS
provider and sends generated PCM to the ALSA playback adapter. PCM never crosses the service
boundary. `audio-probe --speak` and audio status expose the playback path and its metrics.

# audio-service

Jetson-local microphone capture owner and gRPC control plane.

The service owns `AudioCaptureStream`, its ALSA source, and the ring's single VAD consumer. gRPC
exposes start, stop, status, and metrics only; raw PCM remains in the local SPSC data plane. Future
ASR integration should consume speech-segment output from the VAD pipeline, not read the ring as a
second consumer.

The VAD worker also owns `SpeechSegmenter`. Completed segments include configurable pre-roll and
are placed in an eight-entry local SPSC queue. `TryPopSpeechSegment()` is reserved for the single
in-process ASR consumer.

When `features.voice.enabled` is true, the configured recognizer consumes that segment queue.
Transcripts are retained in a bounded 32-event in-memory history and streamed through gRPC;
segment PCM remains local.

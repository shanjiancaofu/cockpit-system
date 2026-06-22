# audio-service

Jetson-local microphone capture owner and gRPC control plane.

The service owns `AudioCaptureStream`, its ALSA source, and the ring's single VAD consumer. gRPC
exposes start, stop, status, and metrics only; raw PCM remains in the local SPSC data plane. Future
ASR integration should consume speech-segment output from the VAD pipeline, not read the ring as a
second consumer.

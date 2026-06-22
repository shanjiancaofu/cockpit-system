# audio-service

Jetson-local microphone capture owner and gRPC control plane.

The service owns `AudioCaptureStream` and its ALSA source. gRPC exposes start, stop, status, and
metrics only; raw PCM remains in the local SPSC data plane. The ring consumer is intentionally
reserved for the next VAD/ASR pipeline instead of discarding captured frames in a placeholder.
In-process consumers use `TryPopFrame()` and must preserve the single-consumer contract.

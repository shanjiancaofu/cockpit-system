# audio

Platform-independent audio types and file handling.

Current scope:

- PCM16 little-endian format validation.
- Bytes-per-frame and frames-per-period calculation.
- RIFF/WAVE PCM16 read and write.
- Bounded WAV input allocation and malformed-file errors.
- Immutable 16 kHz mono 20 ms voice frames.
- Fixed-capacity lock-free SPSC frame transport with overflow metrics.
- Threaded `AudioCaptureStream` with explicit timeout, xrun, stop, and device-error states.
- Pluggable voice-activity interface and stateful energy VAD with debounce and hangover.
- Speech segmentation with pre-roll, endpoint flush, discontinuity handling, and duration limits.

`AudioFrame` is immutable after construction. `SpscRingBuffer` requires exactly one producer and
one consumer; `Available()` is an approximate concurrent snapshot, not a synchronization API.

This module does not open microphones or speakers. Linux ALSA handles belong in `drivers/alsa`;
service lifecycle belongs in `services/audio-service`.

The capture data plane has one producer thread and one consumer. It accumulates partial PCM reads
into fixed 20 ms frames, marks discontinuities after xrun or overflow, and never blocks the
consumer. Platform drivers implement `AudioCaptureSource`; raw PCM does not cross gRPC.

`EnergyVad` is the dependency-free baseline for WSL and Jetson bring-up. It reports RMS dBFS and
stable speech/silence transitions. A future WebRTC implementation should implement the same
`VoiceActivityDetector` interface.

`SpeechSegmenter` converts per-frame VAD decisions into contiguous PCM16 speech segments. The
audio service publishes completed segments to a bounded in-process SPSC queue for the future ASR
consumer.

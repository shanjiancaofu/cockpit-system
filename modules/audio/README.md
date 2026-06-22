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

`AudioFrame` is immutable after construction. `SpscRingBuffer` requires exactly one producer and
one consumer; `Available()` is an approximate concurrent snapshot, not a synchronization API.

This module does not open microphones or speakers. Linux ALSA handles belong in `drivers/alsa`;
service lifecycle belongs in `services/audio-service`.

The capture data plane has one producer thread and one consumer. It accumulates partial PCM reads
into fixed 20 ms frames, marks discontinuities after xrun or overflow, and never blocks the
consumer. Platform drivers implement `AudioCaptureSource`; raw PCM does not cross gRPC.

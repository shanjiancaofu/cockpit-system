# audio

Platform-independent audio types and file handling.

Current scope:

- PCM16 little-endian format validation.
- Bytes-per-frame and frames-per-period calculation.
- RIFF/WAVE PCM16 read and write.
- Bounded WAV input allocation and malformed-file errors.

This module does not open microphones or speakers. Linux ALSA handles belong in `drivers/alsa`;
service lifecycle belongs in `services/audio-service`.

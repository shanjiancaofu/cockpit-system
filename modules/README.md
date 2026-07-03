# modules

Reusable product and domain capabilities. Modules may depend on individual targets under `core/`
and driver interfaces, but must not depend on services or applications.

- `vehicle`: vehicle state domain model.
- `can`: platform-independent CAN frame model.
- `camera`: camera frame model and optional GStreamer preview pipeline boundary.
- `audio`: platform-independent PCM format, frames, stream, VAD, speech segment, and WAV helpers.
- `voice`: ASR/TTS/intent/action interfaces plus deterministic mock providers.

Larger modules are split by concrete responsibility and give each subdirectory its own CMake
target. `audio` uses `frames`, `capture`, `vad`, `playback`, and `wav`; `camera` uses `frames`,
`capture`, and `shared_memory`; `voice` uses `asr`, `tts`, `assistant`, `actions`, and `responses`.
Small modules such as `can` and `vehicle` remain flat until they contain multiple real
responsibilities. Do not create generic `base`, `common`, or `misc` directories.

Future modules may include `media`, `storage`, and model-provider adapters when real requirements
appear. Hardware APIs still belong in `drivers/`, and daemon ownership belongs in `services/`.

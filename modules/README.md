# modules

Reusable product and domain capabilities. Modules may depend on individual targets under `core/`
and driver interfaces, but must not depend on services or applications.

- `vehicle`: vehicle state domain model.
- `can`: platform-independent CAN frame model.
- `audio`: platform-independent PCM format, frames, stream, VAD, speech segment, and WAV helpers.
- `voice`: ASR/TTS/intent/action interfaces plus deterministic mock providers.

Future modules may include `media`, `storage`, and model-provider adapters when real requirements
appear. Hardware APIs still belong in `drivers/`, and daemon ownership belongs in `services/`.

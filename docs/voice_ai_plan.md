# Voice and AI Plan

This project will support microphone, speaker, and AI-powered cockpit interaction on Jetson.

## Goal

Add a local voice interaction layer without turning the project into a cloud platform.

The first target is:

```text
push-to-talk
  -> record microphone audio
  -> speech-to-text
  -> intent / LLM processing
  -> local cockpit action or answer
  -> text-to-speech
  -> speaker playback
```

## Proposed Modules

```text
services/audio-service
services/voice-interaction-service
modules/audio
modules/ai
drivers/alsa
tools/audio-probe
```

### audio-service

Responsibilities:

- Detect microphone and speaker devices.
- Capture PCM audio.
- Play PCM/WAV/TTS audio.
- Manage volume and mute state.
- Expose audio status to cockpit gateway.

Backend options:

- ALSA for low-level Jetson/Linux control.
- PulseAudio or PipeWire if the target Jetson image already uses them.
- GStreamer for media pipeline integration.

### voice-interaction-service

Responsibilities:

- Push-to-talk or wake-word state machine.
- Subscribe to text transcript events from `audio-service`.
- Dialog state.
- LLM/tool-call orchestration.
- TTS output.
- Route commands to local services.

The first version should prefer push-to-talk. Wake word can come later.

### modules/ai

Responsibilities:

- Model provider abstraction.
- Prompt templates.
- Tool/action schema.
- Safety rules for local vehicle commands.

Provider modes:

- `mock`: deterministic local responses for smoke tests.
- `local`: offline/local model when available.
- `remote`: API-based model when network is available.

## Voice Commands

Good first commands:

- "Show vehicle status"
- "Open camera"
- "Play music"
- "Show logs"
- "What's the battery level?"
- User voice interaction should not include developer data-package recording commands.

Avoid in the first version:

- Direct shell command execution.
- Safety-critical vehicle control.
- Always-listening wake word.

## Config Sketch

```yaml
audio:
  capture_backend: alsa
  playback_backend: alsa
  input_device: default
  output_device: default
  sample_rate_hz: 16000
  channels: 1
  frame_ms: 20

voice:
  enabled: false
  mode: push_to_talk
  asr_provider: mock
  tts_provider: mock

ai:
  provider: mock
  model: local-demo
  request_timeout_ms: 10000
```

## Build Order

1. `modules/audio`: PCM format and WAV handling. Completed.
2. `drivers/alsa` and `tools/audio-probe`: list devices and record/play a short test file. Completed.
3. `AudioFrame` and lock-free SPSC ring buffer. Completed.
4. ALSA poll/status results and `AudioCaptureStream`. Completed.
5. `audio-service`: microphone capture boundary and status reporting. Completed.
6. Local frame consumer and dependency-free energy VAD boundary. Completed.
7. Speech segment aggregation with pre-roll and bounded local ASR queue. Completed.
8. Mock ASR consumer and transcript event boundary. Completed.
9. `voice-interaction-service`: mock intent and response pipeline. Completed.
10. Add a typed action dispatcher with explicit execution results. Completed.
11. Add text-only Speak RPC, mock TTS, and asynchronous speaker playback. Completed.
12. Connect query-vehicle-status through the gateway local service API. Completed.
13. Keep user voice actions separate from developer recording/data-package controls. Next action
    providers should be user-facing, such as media or camera preview, while recording belongs to a
    separate diagnostics boundary.
14. Replace mock providers with local or remote models and optionally add WebRTC VAD.

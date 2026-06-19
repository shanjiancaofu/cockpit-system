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
common/audio
common/ai
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
- ASR input.
- Dialog state.
- LLM/tool-call orchestration.
- TTS output.
- Route commands to local services.

The first version should prefer push-to-talk. Wake word can come later.

### common/ai

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
- "Start recording"
- "Stop recording"

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

1. `tools/audio-probe`: list input/output devices and record/play a short test file.
2. `common/audio`: RAII wrappers for audio device/session.
3. `audio-service`: service boundary and status reporting.
4. `voice-interaction-service`: mock ASR/TTS/LLM pipeline.
5. Replace mock provider with local or remote model.

# voice-interaction-service

Consumes text transcripts from `audio-service`, maps them to allowlisted cockpit actions,
dispatches typed actions, and publishes response events with a separate execution status. It
never reads raw PCM and never executes shell commands.

The current assistant and dispatcher are deterministic mocks. Real actions must use explicit
local service clients behind the `ActionDispatcher` boundary. Response text is sent to
`audio-service` through `Speak(text)`; this service never opens an ALSA device or transports PCM.

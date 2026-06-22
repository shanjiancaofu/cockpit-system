# voice-interaction-service

Consumes text transcripts from `audio-service`, maps them to allowlisted cockpit actions, and
publishes response events. It never reads raw PCM and never executes shell commands.

The current assistant is deterministic and mock-only. Real local/remote LLM and TTS providers are
future adapters behind the same interaction boundary.

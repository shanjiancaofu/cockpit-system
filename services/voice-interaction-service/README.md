# voice-interaction-service

Consumes text transcripts from `audio-service`, maps them to allowlisted cockpit actions,
dispatches typed actions, and publishes response events with a separate execution status. It
never reads raw PCM and never executes shell commands.

The current assistant is deterministic. Actions use explicit local service clients behind the
`ActionDispatcher` boundary. Response text is sent to
`audio-service` through `Speak(text)`; this service never opens an ALSA device or transports PCM.

`query_vehicle_status` is the first real action. It queries the latest fresh snapshot from
`cockpit-gateway-service`. Camera, media, and recorder actions remain explicitly not implemented.

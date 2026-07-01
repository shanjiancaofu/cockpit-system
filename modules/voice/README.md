# voice

Platform-independent speech and interaction boundaries.

The current implementation provides speech recognition and voice-assistant boundaries,
deterministic mock providers, and a typed `ActionDispatcher`. Recognizers consume completed local
speech segments; they never access ALSA or the AudioFrame ring directly.

The default ASR provider is deterministic `mock`. An optional `whisper.cpp` adapter is available
when the project is configured with `BUILD_WHISPER_CPP_ASR=ON` and `WHISPER_CPP_DIR` points to a
local whisper.cpp checkout. The adapter consumes completed 16 kHz mono PCM segments in process;
model files are deployment assets and are not committed to this repository.

Actions are dispatched as enum values. The dispatcher never accepts shell command strings. The
mock dispatcher simulates allowlisted user-facing actions for tests; real camera, media, and
vehicle-status handlers must replace it through service APIs.

Recording and data-package capture are developer/diagnostic workflows. They do not belong to the
user voice action list.

User-facing app actions such as camera preview or music playback are expressed through the
`HmiCommandProvider` boundary. The C++ voice module does not implement Android music playback or UI;
it only hands the typed command to a future Qt/Android/HMI bridge.

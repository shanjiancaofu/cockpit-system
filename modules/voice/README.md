# voice

Platform-independent speech and interaction boundaries.

The current implementation provides `SpeechRecognizer`, `SpeechTranscript`, and a deterministic
`MockSpeechRecognizer`. Recognizers consume completed local speech segments; they never access
ALSA or the AudioFrame ring directly.

# audio-probe

ALSA microphone and speaker diagnostic tool for WSL and Jetson.

```bash
build/bin/audio-probe --list --config configs/config.yaml
build/bin/audio-probe --capture build/microphone-test.wav --seconds 3
build/bin/audio-probe --play build/microphone-test.wav
```

Override the configured input or output device with `--device NAME`.

The probe records and plays interleaved PCM16 WAV files. It is intentionally a hardware
diagnostic tool, not the long-running audio service. On systems without physical audio hardware,
the ALSA `null` device can verify the software path:

```bash
build/bin/audio-probe --capture build/null-test.wav --seconds 1 --device null
build/bin/audio-probe --play build/null-test.wav --device null
```

# voice-ctl

voice-ctl is the local gRPC diagnostic client for voice-interaction-service.

    build/bin/voice-ctl --status --config configs/config.yaml
    build/bin/voice-ctl --process "open camera" --config configs/config.yaml
    build/bin/voice-ctl --process "play music" --config configs/config.yaml
    build/bin/voice-ctl --responses --count 1 --timeout-ms 10000 \
      --config configs/config.yaml

The process command only invokes the service's allowlisted mock intent path.
It never executes shell commands.
Camera and music commands currently return a local HMI handoff placeholder. They do not launch an
Android app or a C++ media player until a real HMI bridge is connected.

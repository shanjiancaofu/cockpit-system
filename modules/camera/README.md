# camera

Camera-domain primitives and preview pipeline boundaries.

```text
camera/
├── frames/         # frame model, sink, latest-frame buffer
├── capture/        # preview source and optional GStreamer pipeline
└── shared_memory/  # cross-process double buffer
```

The matching targets are `camera_frames`, `camera_capture`, and `camera_shm`. The parent `camera`
target remains a lightweight aggregate.

- `CameraFrame`: copied preview frame for UI/debug consumers.
- `CameraPreviewSource`: common preview source interface.
- `CameraFrameSink`: local frame-consumer boundary.
- `LatestFrameBuffer`: thread-safe one-frame buffer for consumers that only need the newest image.
- `SharedFrameWriter` / `SharedFrameReader`: POSIX shared-memory double buffer for local processes.
- `GstreamerPreviewPipeline`: optional GStreamer implementation using `v4l2src` and `appsink`.

The preview callback transfers frame ownership by move into a sink. `LatestFrameBuffer` never queues
an unbounded video backlog: a new frame replaces the previous frame, and consumers copy only when
they call `ReadLatest()`.

The shared-memory transport uses two frame slots with process-shared read/write locks. The writer
publishes into the inactive slot and atomically switches the active slot; camera pixels never pass
through gRPC.

The base frame/capture interfaces have no GStreamer dependency. `gstreamer_camera` is built only when
`gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0` development packages are installed.

Recording and research data capture should stay in a future recording/diagnostics boundary, not in
voice interaction.

When GStreamer development packages are installed, run a USB camera preview smoke manually:

```bash
build/bin/camera-preview-probe --device /dev/video0 --frames 30 --config configs/config.yaml
```

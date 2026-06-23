# camera

Camera-domain primitives and preview pipeline boundaries.

- `CameraFrame`: copied preview frame for UI/debug consumers.
- `CameraPreviewSource`: common preview source interface.
- `GstreamerPreviewPipeline`: optional GStreamer implementation using `v4l2src` and `appsink`.

The base `camera` target has no GStreamer dependency. `gstreamer_camera` is built only when
`gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0` development packages are installed.

Recording and research data capture should stay in a future recording/diagnostics boundary, not in
voice interaction.

When GStreamer development packages are installed, run a USB camera preview smoke manually:

```bash
build/bin/camera-preview-probe --device /dev/video0 --frames 30 --config configs/config.yaml
```

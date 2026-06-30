# camera-service

Local camera control service for Jetson-side cockpit camera features.

Current scope:

- List V4L2 camera devices.
- Expose preview start/stop/status through gRPC control plane.
- Own the local GStreamer `v4l2src -> appsink` preview pipeline when GStreamer is available.
- Publish captured frames through `CameraFrameSink`; the default sink keeps only the latest frame.
- The process entry injects a POSIX shared-memory double-buffer sink configured under
  `services.camera`.
- Report received and invalid frame counters through the status control plane.
- Keep frame data out of gRPC; future frame delivery should use local pipeline/shared memory.

`CameraService` depends on the `CameraPreviewSource` interface, so tests and future Jetson-specific
capture implementations do not need to depend directly on GStreamer. The independent frame-sink
boundary is the extension point for the future Qt bridge or shared-memory transport.

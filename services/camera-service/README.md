# camera-service

Local camera control service for Jetson-side cockpit camera features.

Current scope:

- List V4L2 camera devices.
- Expose preview start/stop/status through gRPC control plane.
- Own the local GStreamer `v4l2src -> appsink` preview pipeline when GStreamer is available.
- Report received and invalid frame counters through the status control plane.
- Keep frame data out of gRPC; future frame delivery should use local pipeline/shared memory.

`CameraService` depends on the `CameraPreviewSource` interface, so tests and future Jetson-specific
capture implementations do not need to depend directly on GStreamer.

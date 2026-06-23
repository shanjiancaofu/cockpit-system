# camera-service

Local camera control service for Jetson-side cockpit camera features.

Current scope:

- List V4L2 camera devices.
- Expose preview start/stop/status through gRPC control plane.
- Keep frame data out of gRPC; future frame delivery should use local pipeline/shared memory.

The first implementation tracks preview lifecycle only. GStreamer frame capture already exists in
`camera-preview-probe` and will be connected to this service in a later batch.

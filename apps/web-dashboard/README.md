# web-dashboard

Local Jetson browser debug dashboard placeholder.

This is not the cloud management frontend. The cloud management frontend lives in
`../../cloud-frontend` at the project workspace level.

The architecture keeps this separate from the Qt cockpit UI:

- WebSocket + JSON for status and log debug streams.
- WebRTC for browser video preview.
- No video frames over WebSocket.

The existing `../zelos/car_cloud_front_end` React + Vite project is the nearest
frontend reference for API mapping, tests, and build scripts.

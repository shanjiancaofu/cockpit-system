# drivers

Platform and hardware adapters used by modules, services, and diagnostic tools.

- `socketcan`: Linux SocketCAN user-space adapter.
- `alsa`: Linux ALSA capture and playback adapters.
- Future adapters may include GPIO, I2C, IIO, camera, and radar devices.

Kernel modules and device-tree sources belong under their device directory, but are excluded from
the default user-space build unless explicitly enabled.

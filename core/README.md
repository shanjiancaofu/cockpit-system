# core

Process-independent infrastructure shared by applications, services, tools, modules, and drivers.

- `config`: runtime configuration.
- `event`: bounded in-process queues for low-rate typed events.
- `ipc`: process-independent local IPC primitives such as POSIX shared-memory mappings.
- `logging`: unified logging.
- `runtime`: service lifecycle and command-line handling.
- `utils`: small foundational helpers.

`core` must not contain vehicle-domain models, hardware access, or product features.
There is intentionally no umbrella `core` CMake target; consumers link only the libraries they use.

`ipc::SharedMemoryRegion` owns only generic POSIX mapping lifecycle. Creators use exclusive names
and unlink their mapping on destruction; domain-specific headers, slots, and synchronization stay
inside the consuming module.

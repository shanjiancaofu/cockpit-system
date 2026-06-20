# core

Process-independent infrastructure shared by applications, services, tools, modules, and drivers.

- `config`: runtime configuration.
- `logging`: unified logging.
- `runtime`: service lifecycle and command-line handling.
- `utils`: small foundational helpers.

`core` must not contain vehicle-domain models, hardware access, or product features.
There is intentionally no umbrella `core` CMake target; consumers link only the libraries they use.

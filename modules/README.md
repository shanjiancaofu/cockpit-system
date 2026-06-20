# modules

Reusable product and domain capabilities. Modules may depend on individual targets under `core/`
and driver interfaces, but must not depend on services or applications.

- `vehicle`: vehicle state domain model.
- `can`: platform-independent CAN frame model.
- Future modules: `audio`, `ai`, `media`, and `storage` when real implementations exist.

# common

Shared infrastructure for local services.

## Modules

- `config`: runtime configuration reader. It currently supports the subset used by
  `configs/config.yaml`; replace it with `yaml-cpp` before nested production config grows.
- `logging`: service-local rolling file logger with stderr mirroring.
- `proto`: protobuf and gRPC contract drafts.
- `runtime`: command-line parsing, config loading, logger initialization, and signal handling.
- `vehicle`: shared vehicle state model used before generated protobuf classes are enabled.
- `utils`: small shared data types and helpers.
- `generated`: reserved for generated protobuf/gRPC sources.

## Rules

- Business services must use `common/logging` instead of ad hoc terminal output.
- Runtime values must come from config with code defaults as fallback.
- Generated code should stay in the build directory or `common/generated`, not beside hand-written
  sources.

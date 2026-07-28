# Navigator module entries

This directory owns the dynamic-library boundary used by Navigator. Each
`<module>_entry.cc` file is intentionally small:

- construct the module runtime during `start`;
- release it during `stop`;
- forward health polling to the runtime;
- expose the versioned `CockpitModuleApi`.

Module behavior belongs under `cockpit/library`; process supervision, loading,
and ABI adaptation belong under `cockpit/navigator`.

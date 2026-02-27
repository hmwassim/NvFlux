# Contributing

- Keep privileged code minimal and auditable.
- Add a unit test in `tests/test_nvflux.c` for every logic change; tests must not require root or a GPU.
- Run `ctest` from the build directory before submitting.
- C99, no non-standard extensions.
- Document security rationale for any change touching privilege paths or subprocess execution.
- Each source module (gpu, state, exec) should remain independently testable.

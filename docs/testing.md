# Testing

```bash
tests/run_tests.sh          # quick tests, asserts and exits non-zero on failure
tests/run_tests.sh --zex    # adds zexdoc, zexall and 8080exm
make -C src unit            # 8080-mode CPU unit tests, under a second
make -C src test            # three quick tests, eyeball only, never fails
```

`tests/run_tests.sh` is the one that can fail. 156 checks assemble their guests
at test time and skip unless `um80` and `ul80` are on `PATH`
(`pip install um80`); with `x86_64-w64-mingw32-g++` present the suite also
cross-compiles the Windows half of the platform layer. Both skip quietly and
exit 0, so `tests/run_tests.sh --require` turns any fixable skip into a failure
and names what to install - which is how `.github/workflows/ci.yml` runs it.

The terminal layer is unreachable through a pipe and has harnesses of its own:
`tests/pty_console.cc` (everywhere but Windows) and `tests/win_console.cc`
(Windows only). [`tests/README.md`](../tests/README.md) documents the whole suite;
what no test can reach is in [`MANUAL_CHECKS.md`](../MANUAL_CHECKS.md).

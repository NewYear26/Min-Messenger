# Tests

Run on Linux or WSL from the repository root:

```sh
sh tests/run_tests.sh
```

The suite covers every reusable protocol function (`send_Secret`,
`read_Secret`, and `send_Message`), including binary payloads and malformed
frames. The script also compiles every translation unit and performs the
configured application build, which covers the three entry points and exposes
link/configuration defects.

The tests intentionally describe the required safe behavior. Some tests fail
or time out on the current implementation; those failures are regressions
documented in the review report and should not be weakened.

# Tests

Run from the repository root on Linux:

```sh
sh tests/run_tests.sh
```

The source uses POSIX sockets, `fork`, pthreads, and `<unistd.h>`, so the test
suite requires Linux (or an installed WSL distribution), a C++20 compiler,
Python 3, and optionally CMake.

## Coverage

`secret_tests.cpp` tests all reusable protocol functions:

- binary, empty, and embedded-NUL payloads;
- exact buffer boundaries and `uint16_t` length overflow;
- null input and disconnected peers;
- fragmented TCP header and body delivery;
- empty, truncated, undersized, and oversized frames;
- portable network byte order;
- termination instead of hanging after peer shutdown.

Every unit case runs in a separate process with a 750 ms deadline. A memory
error, `SIGPIPE`, or infinite receive loop therefore fails only that case and
does not prevent the remaining tests from running.

`integration_tests.py` exercises the application entry points over real TCP:

- legacy server binary relay, no sender echo, and a full 1024-byte read;
- Secret server handshake, invalid handshake, fragmented handshake, and relay;
- interactive client handshake plus traffic in both directions.

The runner also compiles every translation unit independently and attempts the
project's unmodified CMake build. Failures are intentional evidence of current
defects; production code and build configuration are not modified by tests.

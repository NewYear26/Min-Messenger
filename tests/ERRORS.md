# Potential defects and recommended fixes

No production source was changed. This report describes what should be fixed.

## Critical

1. **Unbounded write in `read_Secret` (`secret.cpp`).** `max_numbits` is never
   checked. A peer-controlled length can write beyond `data`, causing memory
   corruption or remote code execution. Reject lengths below 3 and payloads
   larger than `max_numbits` before reading the body.

2. **Infinite receive loops (`secret.cpp`).** The body loop handles `-1` but
   ignores `recv() == 0`; it also loops forever for a valid frame of length 3
   because it calls `recv(..., 0)`. Return `false` on EOF and return `true`
   immediately once the header and code constitute the complete frame.

3. **TCP fragmentation is treated as an error (`secret.cpp`).** A single
   `recv(..., 2)` is not guaranteed to return two bytes. Implement a
   `recv_exact` loop for both fixed header fields and the body, handling
   `EINTR`.

4. **Invalid code read continues (`secret.cpp`).** If reading the code returns
   fewer than one byte, the function prints an error but continues. Return
   `false` immediately.

5. **Length truncation in `send_Secret` (`secret.cpp`).** `3 + numbits` is
   silently narrowed to `uint16_t`. Reject payloads greater than
   `UINT16_MAX - 3` before allocating or sending.

6. **Null pointer dereference (`secret.cpp`).** A nonzero length with
   `data == nullptr` crashes. Validate the pointer before copying.

7. **Dangling pointer passed to pthread (`main.cpp`).** `pthread_create`
   receives `&newUser`, a loop-local object that is destroyed/reused while the
   thread accesses it. Pass an owned heap object, a stable container element,
   or copy the value through a safe C++ thread capture.

8. **Out-of-bounds terminator (`main.cpp`).** When `recv` returns 1024,
   `buff[bits] = 0` writes at index 1024 of a 1024-byte array. The relay is
   binary and does not need termination; otherwise reserve one extra byte or
   receive at most `size - 1`.

9. **Data races on users (`main.cpp`, `server.cpp`).** Connection threads
   iterate global vectors while the accept thread may modify them. Protect all
   accesses with one mutex or use a connection registry with stable lifetime.
   Do not hold its mutex during blocking sends.

10. **Secret server never registers clients (`server.cpp`).** `User` is passed
    to `ClientCom` but never inserted into `users`, so code-2 relay has no
    recipients. Register after successful handshake and remove on every exit.

11. **Broken configured build (`CMakeLists.txt`).** Both executable targets
    include `server.cpp`; each then has two `main` functions and duplicate
    globals. Define separate targets: legacy server from `main.cpp`, Secret
    server from `server.cpp + secret.cpp`, and client from
    `client.cpp + secret.cpp`.

## High severity and portability

12. **Wrong socket failure checks.** `socket()` failure is `-1`, but all entry
    points test `< -1`. Change to `< 0` and avoid calling networking functions
    with an invalid descriptor.

13. **Native-endian wire length.** Direct `uint16_t` stores make peers with
    different byte order incompatible. Serialize with `htons` and parse with
    `ntohs`; use `memcpy` rather than an unaligned `reinterpret_cast` store.

14. **Process termination through `SIGPIPE`.** Sending to a disconnected peer
    can terminate the whole process on POSIX. Use `MSG_NOSIGNAL` where
    available or ignore `SIGPIPE` and handle `EPIPE`.

15. **Busy retry loop in `send_Secret`.** Permanent failures are retried 100
    times without distinguishing `EINTR`, `EAGAIN`, and fatal errors. Retry
    `EINTR`; wait for writability on `EAGAIN`; return immediately for fatal
    errors.

16. **Partial sends ignored in legacy relay (`main.cpp`).** `send()` may write
    fewer bytes than requested. Use a bounded send-all loop and handle errors.

17. **Disconnected users are never removed.** Closed descriptors remain in
    the global registry and may later be reused by the OS for unrelated
    clients. Remove entries atomically before closing the socket.

18. **Detached thread lifetime is uncontrolled.** Both implementations detach
    workers, preventing orderly shutdown and resource accounting. Prefer
    joinable workers managed by the server or a bounded thread pool.

19. **Client spins after stdin EOF (`client.cpp`).** `std::cin >> msg` is not
    checked. On EOF the loop repeatedly sends stale/empty state. Break when
    extraction fails and shut down the socket cleanly.

20. **Concurrent client shutdown is racy (`client.cpp`).** The detached reader
    can close `servsk` while the main thread sends through it. Coordinate
    ownership, signal shutdown, and join the reader.

21. **No protocol/resource timeouts.** A peer can send only part of a frame and
    occupy a worker forever. Add socket deadlines or poll/select timeouts and a
    maximum number of concurrent clients.

22. **No authentication, integrity, or encryption.** Any local process can
    impersonate a client and payloads are plaintext. If the messenger handles
    sensitive data, use TLS and an authenticated session protocol; the name
    “Secret” does not provide security.

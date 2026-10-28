# NetStore

NetStore is a learning project that will grow from a small command-line
key-value store into a high-performance, multithreaded TCP server written in
modern C++.

The point is not simply to produce another database. The project is a compact
way to practice the systems-programming skills that often appear together in
real C++ work and interviews:

- modern C++ syntax, types, object lifetime, and resource ownership;
- RAII, smart pointers, and copy/move semantics;
- TCP/IP, sockets, file descriptors, and connection handling;
- threads, synchronization, thread pools, and race conditions;
- CMake, testing, sanitizers, debugging, and profiling;
- measuring performance instead of guessing about it.

## What is a key-value store?

A key-value store associates a unique key with a value. It is similar to a
dictionary in Python, a `Map` in Java, or a hash table in C.

For example:

```text
SET language C++
GET language
C++
```

The first version runs in one terminal and keeps its data in memory. Later,
the same core storage logic will be used by a TCP server so clients can send
commands over a network connection.

## Why build it in stages?

Each stage introduces one category of difficulty. This lets us understand and
test a concept before combining it with networking and concurrency.

1. **CLI key-value store** — basic modern C++, classes, standard containers,
   parsing, ownership, and unit tests.
2. **Single-client TCP server** — sockets, addresses, file descriptors,
   blocking I/O, and RAII cleanup.
3. **Multi-client server** — multiple connections and the limitations of a
   simple blocking design.
4. **Thread pool** — worker threads, task queues, mutexes, condition
   variables, and shutdown behavior.
5. **Thread-safe store** — shared state, invariants, locking, and race
   conditions.
6. **Persistence and TTL** — file I/O, serialization, clocks, and background
   cleanup.
7. **Benchmarking and optimization** — latency, throughput, contention, and
   evidence-based optimization.
8. **Testing and diagnostics** — unit and integration tests, sanitizers,
   debugging, and profiling.

## Current status

Milestone 1, the command-line key-value store, is complete. It supports:

```text
SET <key> <value>
GET <key>
DELETE <key>
EXISTS <key>
SIZE
EXIT
```

Command parsing, execution, and data storage are separate components. This
matters because the CLI and TCP server share the same behavior, and
because each component can be tested independently.

The shared command flow is:

```text
input -> command parser -> command executor -> key-value store
                              |
output <- owned response <----+
```

Milestones 1–4 are implemented. The TCP server listens on `127.0.0.1:8080`
and serves clients using a fixed C++20 `std::jthread` pool (four workers by
default) and a bounded FIFO queue (64 waiting connections by default).
The main thread accepts and queues connections while reusable workers receive
and process commands. A full queue rejects and closes a newly accepted socket.
A shared mutex protects command execution against the shared in-memory store;
network waits happen outside that lock. `KeyValueStore` itself does not yet
provide internal locking, so callers must coordinate access.

Disconnection, `EXIT`, or a session error ends only that session. The shared
store survives between clients, while each session owns its unfinished input.
Socket ownership moves from the accept thread into the queue, then to a worker.
Each active session also has a shared owner in the pool's active-slot registry
so shutdown can safely access its descriptor. On session completion the slot
is cleared, the socket closes, and the same worker takes the next connection.

Ctrl+C requests shutdown using a lock-free atomic flag. The main thread polls
the nonblocking listener at 200 ms intervals, stops accepting, shuts down client
connections to wake blocked I/O, discards queued clients, then joins workers
before destroying the store. The pool destructor also stops the pool, including
cleanup if starting the fixed set of workers fails partway through construction.
Client sockets use blocking I/O. Receives and sends retry interruptions, and
sends handle partial progress. SIGPIPE is ignored so broken sends report errors.
Commands end with `\n` and may contain at most 4,096 bytes before the newline.
Oversized commands close the connection with a server-side error. An unfinished
command is discarded on disconnect. Data is in memory and is lost on exit.

Start the server with `./build/netstore_server`, then connect from another
terminal with `nc 127.0.0.1 8080`. Responses appear in the client terminal.
Open two client terminals to try simultaneous sessions. `EXIT` closes only the
client that sends it; Ctrl+C in the server terminal stops the server.

Optional positional arguments configure worker count and waiting-queue capacity:

```bash
./build/netstore_server 4 64
```

Worker count must be positive. Queue capacity may be zero, which rejects all
submissions. The queue limit is separate from the kernel's TCP listen backlog.
See [the thread-pool walkthrough](docs/thread_pool.md) for ownership, locking,
shutdown, and a manual queue-saturation exercise.

This is a learning implementation, not a production database. Workers handle
whole blocking client sessions: four idle clients can occupy all four workers,
leaving later clients queued. There are no idle timeouts, authentication, or TLS,
and logs from different workers can interleave.
Graceful shutdown currently handles SIGINT (Ctrl+C), not SIGTERM. The tests
have been run on macOS; Linux portability has not yet been verified by CI.
The next milestone moves synchronization into the store itself. Persistence,
TTL, and performance measurements are still future work.

## Using the CLI

Start the CLI and enter one command per line:

```bash
./build/netstore
```

Example:

```text
netstore> SET greeting Hello world
OK
netstore> GET greeting
Hello world
netstore> SIZE
Size: 1
netstore> EXIT
Goodbye!
```

## Building

Requirements:

- CMake 3.20 or newer;
- a compiler with C++20 support, such as a current Clang or GCC.
- Git, which CMake uses to fetch the pinned Catch2 test dependency on the
  first test-enabled configuration.
- Python 3 for TCP integration tests (when `BUILD_TESTING` is enabled).
- Linux or macOS for the POSIX socket implementation.

Configure and build the project from its root:

```bash
cmake -S . -B build
cmake --build build
```

The first configuration downloads Catch2 `v3.15.3` into the ignored build
directory. Application code has no third-party runtime dependencies.

## Testing

Build and run the complete test suite:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Catch2 provides named unit tests for storage, parsing, command execution,
socket ownership, the bounded queue, and the thread pool. Pool tests exercise
overflow rejection, worker reuse, destructor cleanup, repeated/concurrent stop,
and shutdown with active and queued connections. Test timeouts detect hangs.
A separate CTest integration test runs the actual CLI with scripted input and
compares its complete output against the expected transcript.

The `netstore_tcp_test` CTest entry runs fourteen Python standard-library
integration cases against the actual server: command responses and `EXIT`,
fragmented commands, disconnects with unfinished input, the exact line-length
boundary, many small commands, oversized input with and without a newline,
storage shared across successive clients, simultaneous clients, concurrent
writers, SIGINT shutdown with and without connected clients, bounded-pool
saturation, shutdown with queued clients, and invalid pool arguments.
Tests verify that the server accepts another client after a session ends.
These tests do not force interrupted system calls, partial sends, or allocation
and thread-start failures; passing them is not proof of race freedom.

Port 8080 must be free: stop any manually running server before testing. The
test script starts and cleans up its own server processes and uses timeouts to
avoid hanging. Run only the TCP suite with:

```bash
ctest --test-dir build -R netstore_tcp_test --output-on-failure
```

An AddressSanitizer/UndefinedBehaviorSanitizer build can be checked with:

```bash
cmake -S . -B build-sanitize -DBUILD_TESTING=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-sanitize
UBSAN_OPTIONS=halt_on_error=1 python3 tests/server_integration_test.py \
  ./build-sanitize/netstore_server
```

This checks the running server; it does not run Catch2 under sanitizers or
replace ThreadSanitizer testing. No throughput or latency claims are made yet.

## Learning workflow

The project is intentionally developed one small exercise at a time:

1. Understand the behavior and the C++ concepts needed for it.
2. Implement the exercise without receiving a complete solution.
3. Build and test it.
4. Review correctness, style, ownership, and lifetime.
5. Discuss relevant interview questions before moving forward.

The approved milestone design is documented in
[`docs/superpowers/specs/2026-09-29-netstore-milestone-1-design.md`](docs/superpowers/specs/2026-09-29-netstore-milestone-1-design.md).

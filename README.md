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
matters because the future CLI and TCP server can share the same behavior, and
because each component can be tested independently.

The planned flow is:

```text
input -> command parser -> command executor -> key-value store
                              |
output <- owned response <----+
```

The project is now beginning milestone 2: a blocking TCP server that serves
one connected client at a time on `127.0.0.1:5555`. The server will introduce
POSIX sockets, file descriptors, line-based TCP framing, and RAII resource
management while reusing the completed milestone-1 core.

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

Catch2 provides named unit tests for storage, parsing, and command execution.
A separate CTest integration test runs the actual CLI with scripted input and
compares its complete output against the expected transcript.

## Learning workflow

The project is intentionally developed one small exercise at a time:

1. Understand the behavior and the C++ concepts needed for it.
2. Implement the exercise without receiving a complete solution.
3. Build and test it.
4. Review correctness, style, ownership, and lifetime.
5. Discuss relevant interview questions before moving forward.

The approved milestone design is documented in
[`docs/superpowers/specs/2026-09-29-netstore-milestone-1-design.md`](docs/superpowers/specs/2026-09-29-netstore-milestone-1-design.md).

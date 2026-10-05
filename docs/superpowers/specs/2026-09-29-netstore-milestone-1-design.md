# NetStore Milestone 1 Design

## Purpose

NetStore is a learning and portfolio project that will grow into a high-performance, multithreaded TCP key-value server in modern C++. Milestone 1 establishes a small command-line key-value store before networking and concurrency are introduced.

The project is intended to teach modern C++ incrementally. Each step will introduce only the concepts required for a small exercise, which the learner implements and submits for review. Reviews will cover correctness, ownership, object lifetime, idiomatic C++, and relevant interview questions.

## Milestone 1 Scope

The command-line program will eventually support:

- `SET <key> <value>`
- `GET <key>`
- `DELETE <key>`
- `EXISTS <key>`
- `SIZE`
- `EXIT`

Milestone 1 excludes networking, concurrency, persistence, TTL, benchmarking, and production-oriented abstractions. Those belong to later milestones.

## Language and Dependencies

- C++20
- CMake as the build-system generator
- Linux and macOS support
- The C++ standard library only for application code
- GCC or Clang warning flags: `-Wall -Wextra -Wpedantic`

A unit-testing dependency may be selected when the first independently testable component is introduced. It is not needed for the initial input-loop exercise.

## Project Structure

The intended milestone structure is:

```text
NetStore/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── src/
│   ├── main.cpp
│   ├── key_value_store.hpp
│   ├── key_value_store.cpp
│   ├── command_parser.hpp
│   └── command_parser.cpp
└── tests/
    ├── CMakeLists.txt
    ├── key_value_store_test.cpp
    └── command_parser_test.cpp
```

Files will be introduced only when their responsibilities are needed. The initial exercise requires only the root `CMakeLists.txt` and `src/main.cpp`.

## Components

### CLI entry point

`src/main.cpp` owns terminal interaction. It reads complete input lines, sends them to the parser, dispatches valid commands to the store, and prints results. It should not eventually contain parsing rules or storage implementation details.

### Command parser

`src/command_parser.hpp` and `src/command_parser.cpp` convert an input line into a structured command or a parse error. Parsing has no terminal-I/O or storage side effects so it can be tested independently.

Keys are non-empty, whitespace-delimited tokens. For milestone 1, a `SET`
value is the non-empty remainder of the input after the key and required
separator, so a value may contain spaces.

### Key-value store

`src/key_value_store.hpp` and `src/key_value_store.cpp` own the in-memory key-value data and implement storage operations. The public API will hide the choice of standard container from its callers. Thread safety is explicitly deferred until the thread-safe-store milestone.

### Tests

Storage and parsing will have separate unit tests. The CLI loop will remain thin enough that most behavior can be verified through the two testable components rather than terminal-I/O tests.

## Data Flow

```text
terminal input -> command parser -> structured command -> key-value store
                                                           |
terminal output <- operation result <-----------------------+
```

The parser reports malformed input without changing storage. Storage operations return results without printing them. The CLI layer converts those results into user-facing text.

## Error Handling

Milestone 1 will handle expected user errors without exceptions where a normal result can express the outcome. Examples include unknown commands, missing arguments, extra arguments, and missing keys.

Unexpected programming errors should not be hidden by broad exception handlers. Error messages and exact command behavior will be specified alongside the parser exercise.

End-of-file on standard input is a normal request to leave the CLI and must terminate the process cleanly.

## Build Design

The initial CMake configuration creates one executable target named `netstore` from `src/main.cpp`, requests C++20, disables compiler-specific language extensions, and enables strict warnings.

When the first storage or parsing component is added, those components will
form a small `netstore_core` library. The CLI and tests can both link to this
target. A future TCP executable can reuse the same core without depending on
terminal I/O.

The standard build workflow is:

```bash
cmake -S . -B build
cmake --build build
./build/netstore
```

Generated build files stay in the ignored `build/` directory.

## First Learner Exercise

The learner will implement `src/main.cpp`. The program will:

1. Print a `netstore> ` prompt and flush it.
2. Read one complete line at a time.
3. Exit when the line is exactly `EXIT`.
4. Print `Unknown command` for any other non-empty line.
5. Continue prompting until exit.
6. Treat end-of-file as normal termination.

This exercise introduces automatic object lifetime, `std::string`, standard streams, `std::getline`, stream state, loops, and explicit `std::` namespace qualification. It does not implement parsing, storage, or networking.

The mentor may create the explanatory `README.md` and the initial `CMakeLists.txt`, but will not create or supply the complete `main.cpp` implementation. The learner will submit that file for review before the next feature is designed.

## Success Criteria

- The project configures and builds with CMake using GCC or Clang on Linux/macOS.
- The initial CLI exercise handles prompts, `EXIT`, unknown non-empty input, and end-of-file as specified.
- Parsing and storage become separate components as milestone 1 progresses.
- Every learner submission receives a review focused on correctness, modern C++ practice, ownership, lifetime, and recently introduced interview concepts.
- No later milestone is implemented prematurely.

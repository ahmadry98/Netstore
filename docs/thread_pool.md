# Milestone 4: how the thread pool works

The server previously created a new thread for each connection. It now starts
a fixed number of workers and reuses them for successive client sessions.
Defaults are four workers and 64 waiting connections.

```text
accept thread -> bounded connection queue -> available worker -> client session
                                                  |
                                           repeat after EXIT/disconnect
```

## What each file does

- `server_main.cpp` sets up the listener, handles SIGINT, polls for connections,
  and submits accepted sockets to the pool. It no longer creates client threads.
- `connection_queue.hpp/.cpp` owns waiting sockets. Its mutex protects its FIFO,
  capacity check, and stopped flag. Its condition variable wakes idle workers.
- `thread_pool.hpp/.cpp` owns the fixed worker threads and active-socket registry.
  It starts, reuses, and joins workers, and coordinates server-wide shutdown.
- `client_session.hpp/.cpp` implements the existing receive/frame/parse/execute/
  send loop. It borrows a descriptor and never closes it itself.

## Ownership and lifetime

1. The accept thread wraps a descriptor in `Socket`, taking responsibility for
   closing it. The operating system still manages the underlying connection.
2. `submit(std::move(client))` moves that responsibility into the queue. The
   original owner holds `-1`. A rejected submission destroys its parameter,
   closing the connection without another thread being created.
3. A worker calls `pop()`. The queue moves its front socket out before removing
   the moved-from entry. An empty optional means the stopped queue is empty.
4. The worker makes one shared `Socket` owner, held locally and in its active
   slot. This lets `stop()` safely call `shutdown()` without risking a stale,
   reused descriptor number. Shared ownership does not duplicate the Socket.
5. At normal session completion, `SHUT_WR` finishes the sending side. This also
   works after a client half-close on macOS. The worker clears its active slot;
   destruction of the last owner calls `close()`. The worker then takes more work.

The pool borrows `KeyValueStore&`. The server constructs the store before the
pool, so the store outlives every worker. The pool is noncopyable because it
owns threads and synchronization objects.

## Mutexes protect different things

| Mutex | Protected state | Why |
|---|---|---|
| Queue mutex | Pending sockets, capacity check, queue stop flag | Prevent two workers taking the same socket and prevent overfilling |
| `store_mutex_` | Command execution against the shared store | Prevent concurrent unsafe access to the underlying map |
| `active_mutex_` | Active slots and pool stop flag | Coordinate worker registration with shutdown |
| `stop_mutex_` | The complete stop/join operation | Make repeated and simultaneous `stop()` calls safe |

No queue or store lock is held while waiting for client network traffic.
The pool releases `active_mutex_` before joining: workers need that mutex to
clear their slots. Joining while holding it would deadlock.

`std::unique_lock` can unlock and relock, allowing condition-variable waiting.
`wait(lock, predicate)` releases the mutex while asleep and reacquires it before
checking the predicate again. The predicate handles notifications that arrive
before a worker sleeps and spurious wakeups. Workers do not busy-spin.

`std::jthread` joins on destruction, but does not automatically interrupt a
blocking socket operation. We explicitly stop the queue and shut down active
connections before destroying the thread owners.

## Shutdown and the registration race

On Ctrl+C, the signal handler sets a lock-free atomic flag. Normal main-thread
code notices it, exits the accept loop, and calls `pool.stop()`.

1. Mark the pool as stopping under `active_mutex_`, reject new submissions,
   stop the queue, and shut down registered active connections.
2. A worker that already popped a socket must acquire that same mutex before
   registering it. If shutdown won the race, the worker closes it without
   entering a new session. Otherwise shutdown sees its registered slot.
3. Release the active lock, then join all workers. Empty-queue workers wake
   because stopping the queue notifies them; active workers wake from network I/O.
4. Discard any remaining queued sockets. Do not start fresh client sessions
   during shutdown, because those clients could remain idle indefinitely.

The standalone queue's `stop()` permits draining existing entries. The pool's
`stop()` adds cancellation of active/queued client sessions. These are different
policies serving different responsibilities.

If constructing a worker thread throws, the constructor stops and joins any
workers already started before rethrowing. Exceptions in a client session are
caught so they do not escape a thread function and terminate the process.

## Try saturation manually

Start a deliberately small server:

```bash
cmake --build build
./build/netstore_server 1 1
```

In a second terminal, connect with `nc 127.0.0.1 8080`, issue `SIZE`, and leave
the session open. This occupies the only worker.

In a third terminal, connect and issue `GET name`. It waits in the queue.
A fourth client will be closed when accepted because the waiting queue is full.
Send `EXIT` from the first client: the worker sends `Goodbye!`, closes that
session, and handles the queued client. Its `GET` now receives a response.

Press Ctrl+C in the server terminal with clients still connected. It should
exit without requiring those clients to send anything. Queued clients close too.

## Limits and next steps

The pool bounds worker count and accepted waiting connections, not stored data
or the kernel backlog. A worker serves a whole blocking connection, so idle
clients can occupy the entire pool. Idle timeouts or event-driven I/O would be
separate improvements. This milestone makes no performance claims.

Milestone 5 will make the store internally thread-safe rather than requiring
the pool to serialize calls externally. Persistence, TTL, benchmarking, and
more diagnostics remain later milestones.

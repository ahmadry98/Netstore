"""Exercise the real server over TCP using only Python's standard library.

Port 8080 must be free. Only server processes launched by this script are stopped.
"""

import contextlib
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import socket
import signal
import subprocess
import sys
import tempfile
import time
import unittest


SERVER = str(Path(sys.argv.pop(1)).resolve())


@contextlib.contextmanager
def running_server(connect_client=True, worker_count=4, queue_capacity=64):
    # A file drains verbose server output without risking a full stdout pipe.
    with tempfile.TemporaryFile(mode="w+b") as output:
        process = subprocess.Popen(
            [SERVER, str(worker_count), str(queue_capacity)],
            stdout=output, stderr=subprocess.STDOUT
        )

        def logs():
            # Use a separate read offset: seeking output would move the child's
            # shared write offset and could overwrite diagnostic messages.
            return os.pread(output.fileno(), 1_000_000, 0).decode(errors="replace")

        try:
            deadline = time.monotonic() + 5
            while "Waiting for a client" not in logs():
                if process.poll() is not None:
                    raise AssertionError(f"Server failed to start:\n{logs()}")
                if time.monotonic() >= deadline:
                    raise AssertionError(f"Server startup timed out:\n{logs()}")
                time.sleep(0.01)

            # Use the first connection for the test rather than consuming a
            # session merely to probe readiness.
            if connect_client:
                with socket.create_connection(("127.0.0.1", 8080), timeout=3) as client:
                    yield process, client, logs
            else:
                yield process, None, logs
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
            diagnostics = logs()
            for marker in ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:"):
                if marker in diagnostics:
                    raise AssertionError(f"Sanitizer reported a failure:\n{diagnostics}")


def receive_line(client):
    data = bytearray()
    while not data.endswith(b"\n"):
        chunk = client.recv(1)
        if not chunk:
            raise AssertionError(f"Connection closed before a full response: {data!r}")
        data.extend(chunk)
    return bytes(data)


class ServerIntegrationTests(unittest.TestCase):
    def test_bounded_pool_queues_rejects_overflow_and_reuses_worker(self):
        with running_server(worker_count=1, queue_capacity=1) as (process, active, logs):
            active.sendall(b"SET name Ahmad\n")
            self.assertEqual(receive_line(active), b"OK\n")
            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as queued:
                queued.sendall(b"GET name\n")
                queued.settimeout(0.15)
                with self.assertRaises(socket.timeout):
                    queued.recv(1)
                queued.settimeout(3)
                with socket.create_connection(("127.0.0.1", 8080), timeout=3) as overflow:
                    self.assertEqual(overflow.recv(1), b"")

                active.sendall(b"EXIT\n")
                self.assertEqual(receive_line(active), b"Goodbye!\n")
                self.assertEqual(active.recv(1), b"")
                self.assertEqual(receive_line(queued), b"Ahmad\n")
                queued.sendall(b"EXIT\n")
                self.assertEqual(receive_line(queued), b"Goodbye!\n")
                self.assertEqual(queued.recv(1), b"")
            self.assert_session_closed(process, active, logs)

    def test_shutdown_discards_idle_queued_clients(self):
        with running_server(worker_count=1, queue_capacity=2) as (process, active, logs):
            active.sendall(b"SIZE\n")
            self.assertEqual(receive_line(active), b"Size: 0\n")
            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as queued:
                # No data: draining this as a normal session would hang shutdown.
                process.send_signal(signal.SIGINT)
                self.assertEqual(process.wait(timeout=3), 0, logs())
                self.assertEqual(active.recv(1), b"")
                try:
                    self.assertEqual(queued.recv(1), b"")
                except ConnectionResetError:
                    pass # A connection still in the kernel backlog can reset.

    def test_invalid_pool_arguments_fail(self):
        for arguments in (("0",), ("-1",), ("abc",), ("4", "abc"), ("4", "64", "extra")):
            with self.subTest(arguments=arguments):
                result = subprocess.run([SERVER, *arguments], capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"Server failed:", result.stderr)

    def test_concurrent_writers_preserve_all_keys(self):
        with running_server(connect_client=False) as (process, _, logs):
            def write_keys(worker):
                with socket.create_connection(("127.0.0.1", 8080), timeout=3) as client:
                    for index in range(25):
                        key = f"worker{worker}_key{index}"
                        client.sendall(f"SET {key} value\nGET {key}\n".encode())
                        self.assertEqual(receive_line(client), b"OK\n")
                        self.assertEqual(receive_line(client), b"value\n")
                    client.sendall(b"EXIT\n")
                    self.assertEqual(receive_line(client), b"Goodbye!\n")
                    self.assertEqual(client.recv(1), b"")

            with ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(write_keys, range(8)))

            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as client:
                client.sendall(b"SIZE\nEXIT\n")
                self.assertEqual(receive_line(client), b"Size: 200\n")
                self.assertEqual(receive_line(client), b"Goodbye!\n")
                self.assert_session_closed(process, client, logs)

    def test_sigint_stops_server_without_clients(self):
        with running_server(connect_client=False) as (process, _, logs):
            process.send_signal(signal.SIGINT)
            self.assertEqual(process.wait(timeout=3), 0, logs())

    def test_sigint_stops_server_with_idle_clients(self):
        with running_server() as (process, first, logs):
            first.sendall(b"SIZE\n")
            self.assertEqual(receive_line(first), b"Size: 0\n")
            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as second:
                second.sendall(b"SIZE\n")
                self.assertEqual(receive_line(second), b"Size: 0\n")
                first.sendall(b"SET unfinished")

                process.send_signal(signal.SIGINT)
                self.assertEqual(process.wait(timeout=3), 0, logs())
                self.assertEqual(first.recv(1), b"")
                self.assertEqual(second.recv(1), b"")

    def test_idle_client_does_not_block_another_client(self):
        with running_server() as (process, first, logs):
            # Leave the first worker waiting on an unfinished command.
            first.sendall(b"SET name ")
            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as second:
                second.sendall(b"SIZE\n")
                self.assertEqual(receive_line(second), b"Size: 0\n")

                first.sendall(b"Ahmad\n")
                self.assertEqual(receive_line(first), b"OK\n")
                second.sendall(b"GET name\nEXIT\n")
                self.assertEqual(receive_line(second), b"Ahmad\n")
                self.assertEqual(receive_line(second), b"Goodbye!\n")
                self.assertEqual(second.recv(1), b"")

            # Ending the second session must not close the first session.
            first.sendall(b"GET name\nEXIT\n")
            self.assertEqual(receive_line(first), b"Ahmad\n")
            self.assertEqual(receive_line(first), b"Goodbye!\n")
            self.assert_session_closed(process, first, logs)

    def assert_session_closed(self, process, client, logs, allow_reset=False):
        try:
            self.assertEqual(client.recv(1), b"")
        except ConnectionResetError:
            # Rejecting oversized input may leave unread bytes in the socket,
            # which can cause TCP to reset rather than gracefully close it.
            if not allow_reset:
                raise
        self.assertIsNone(process.poll(), logs())

        # A live process alone isn't enough: prove it can serve another client.
        with socket.create_connection(("127.0.0.1", 8080), timeout=3) as next_client:
            next_client.sendall(b"EXIT\n")
            self.assertEqual(receive_line(next_client), b"Goodbye!\n")
            self.assertEqual(next_client.recv(1), b"")
        self.assertIsNone(process.poll(), logs())

    def test_commands_invalid_input_and_exit(self):
        with running_server() as (process, client, logs):
            client.sendall(
                b"INVALID\nSIZE\nSET name Ahmad\nGET name\nEXISTS name\n"
                b"SET name Rayan\nGET name\nSIZE\nDELETE name\n"
                b"EXISTS name\nGET name\nDELETE name\nEXIT\nSET ignored value\n"
            )
            expected = [
                b"Invalid command\n", b"Size: 0\n", b"OK\n", b"Ahmad\n",
                b"Yes\n", b"OK\n", b"Rayan\n", b"Size: 1\n", b"Deleted\n",
                b"No\n", b"Key not found\n", b"Key not found\n", b"Goodbye!\n",
            ]
            self.assertEqual([receive_line(client) for _ in expected], expected)
            self.assert_session_closed(process, client, logs)

    def test_fragmented_command_waits_for_newline(self):
        with running_server() as (process, client, logs):
            client.sendall(b"SET na")
            client.settimeout(0.15)
            with self.assertRaises(socket.timeout):
                client.recv(1)
            client.settimeout(3)
            client.sendall(b"me Ahmad\nGET name\nEXIT\n")
            self.assertEqual(receive_line(client), b"OK\n")
            self.assertEqual(receive_line(client), b"Ahmad\n")
            self.assertEqual(receive_line(client), b"Goodbye!\n")
            self.assert_session_closed(process, client, logs)

    def test_disconnect_discards_unfinished_command(self):
        with running_server() as (process, client, logs):
            client.sendall(b"SET unfinished")
            client.shutdown(socket.SHUT_WR)
            self.assert_session_closed(process, client, logs)

    def test_exact_line_limit_and_large_response(self):
        with running_server() as (process, client, logs):
            value = b"x" * (4096 - len(b"SET key "))
            client.sendall(b"SET key " + value + b"\nGET key\nEXIT\n")
            self.assertEqual(receive_line(client), b"OK\n")
            self.assertEqual(receive_line(client), value + b"\n")
            self.assertEqual(receive_line(client), b"Goodbye!\n")
            self.assert_session_closed(process, client, logs)

    def test_many_small_commands_can_exceed_limit_in_total(self):
        with running_server() as (process, client, logs):
            client.sendall(b"SIZE\n" * 1000 + b"EXIT\n")
            for _ in range(1000):
                self.assertEqual(receive_line(client), b"Size: 0\n")
            self.assertEqual(receive_line(client), b"Goodbye!\n")
            self.assert_session_closed(process, client, logs)

    def test_oversized_commands_are_rejected(self):
        for suffix in (b"", b"\n"):
            with self.subTest(newline=bool(suffix)):
                with running_server() as (process, client, logs):
                    client.sendall(b"x" * 4097 + suffix)
                    self.assert_session_closed(process, client, logs, allow_reset=True)
                    self.assertIn("Command too long", logs())

    def test_store_survives_sessions_but_pending_input_does_not(self):
        with running_server() as (process, first, logs):
            first.sendall(b"SET name Ahmad\nSET unfinished")
            self.assertEqual(receive_line(first), b"OK\n")
            first.shutdown(socket.SHUT_WR)
            self.assertEqual(first.recv(1), b"")

            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as second:
                second.sendall(b"GET name\nEXISTS unfinished\nSIZE\nEXIT\n")
                self.assertEqual(receive_line(second), b"Ahmad\n")
                self.assertEqual(receive_line(second), b"No\n")
                self.assertEqual(receive_line(second), b"Size: 1\n")
                self.assertEqual(receive_line(second), b"Goodbye!\n")
                self.assertEqual(second.recv(1), b"")

            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as third:
                third.sendall(b"GET name\nEXIT\n")
                self.assertEqual(receive_line(third), b"Ahmad\n")
                self.assertEqual(receive_line(third), b"Goodbye!\n")
                self.assertEqual(third.recv(1), b"")
            self.assertIsNone(process.poll(), logs())


if __name__ == "__main__":
    unittest.main()

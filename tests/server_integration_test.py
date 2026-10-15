"""Exercise the real server over TCP using only Python's standard library.

Port 8080 must be free. Only server processes launched by this script are stopped.
"""

import contextlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest


SERVER = str(Path(sys.argv.pop(1)).resolve())


@contextlib.contextmanager
def running_server():
    # A file drains verbose server output without risking a full stdout pipe.
    with tempfile.TemporaryFile(mode="w+b") as output:
        process = subprocess.Popen(
            [SERVER], stdout=output, stderr=subprocess.STDOUT
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

            # Do not probe readiness with a connection: the server accepts only
            # one client, so this must be the connection used by the test.
            with socket.create_connection(("127.0.0.1", 8080), timeout=3) as client:
                yield process, client, logs
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)


def receive_line(client):
    data = bytearray()
    while not data.endswith(b"\n"):
        chunk = client.recv(1)
        if not chunk:
            raise AssertionError(f"Connection closed before a full response: {data!r}")
        data.extend(chunk)
    return bytes(data)


class ServerIntegrationTests(unittest.TestCase):
    def assert_exit(self, process, logs, expected=0):
        self.assertEqual(process.wait(timeout=3), expected, logs())

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
            self.assertEqual(client.recv(1), b"")
            self.assert_exit(process, logs)

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
            self.assert_exit(process, logs)

    def test_disconnect_discards_unfinished_command(self):
        with running_server() as (process, client, logs):
            client.sendall(b"SET unfinished")
            client.shutdown(socket.SHUT_WR)
            self.assertEqual(client.recv(1), b"")
            self.assert_exit(process, logs)
            self.assertIn("Client disconnected", logs())

    def test_exact_line_limit_and_large_response(self):
        with running_server() as (process, client, logs):
            value = b"x" * (4096 - len(b"SET key "))
            client.sendall(b"SET key " + value + b"\nGET key\nEXIT\n")
            self.assertEqual(receive_line(client), b"OK\n")
            self.assertEqual(receive_line(client), value + b"\n")
            self.assertEqual(receive_line(client), b"Goodbye!\n")
            self.assert_exit(process, logs)

    def test_many_small_commands_can_exceed_limit_in_total(self):
        with running_server() as (process, client, logs):
            client.sendall(b"SIZE\n" * 1000 + b"EXIT\n")
            for _ in range(1000):
                self.assertEqual(receive_line(client), b"Size: 0\n")
            self.assertEqual(receive_line(client), b"Goodbye!\n")
            self.assert_exit(process, logs)

    def test_oversized_commands_are_rejected(self):
        for suffix in (b"", b"\n"):
            with self.subTest(newline=bool(suffix)):
                with running_server() as (process, client, logs):
                    client.sendall(b"x" * 4097 + suffix)
                    self.assert_exit(process, logs, expected=1)
                    self.assertIn("Command too long", logs())


if __name__ == "__main__":
    unittest.main()

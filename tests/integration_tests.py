#!/usr/bin/env python3
"""Black-box TCP tests for both servers and the interactive client.

The Secret frame length is encoded as an unsigned 16-bit integer in network
byte order, matching htons()/ntohs() in secret.cpp.
"""

import os
import select
import signal
import socket
import struct
import subprocess
import threading
import time
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "tests", "bin")
NETWORK_U16 = "!H"
CONNECT_TIMEOUT = 1.5
SOCKET_TIMEOUT = 0.5


def recv_exact(sock, size):
    """Receive exactly size bytes or report an early connection close."""
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise ConnectionError("peer closed connection")
        data.extend(part)
    return bytes(data)


def encode_frame(code, payload=b""):
    """Encode one Secret frame using the portable wire representation."""
    frame_size = len(payload) + 3
    if not 0 <= code <= 0xFF:
        raise ValueError("code must fit in uint8_t")
    if frame_size > 0xFFFF:
        raise ValueError("Secret frame is too large")
    return struct.pack(NETWORK_U16, frame_size) + bytes([code]) + payload


def recv_frame(sock):
    """Receive and validate one complete Secret frame."""
    frame_size = struct.unpack(NETWORK_U16, recv_exact(sock, 2))[0]
    if frame_size < 3:
        raise ValueError(f"invalid Secret frame length: {frame_size}")
    code = recv_exact(sock, 1)[0]
    return code, recv_exact(sock, frame_size - 3)


def connect(port):
    """Connect to a server that may still be starting."""
    deadline = time.monotonic() + CONNECT_TIMEOUT
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = socket.create_connection(
                ("127.0.0.1", port), timeout=SOCKET_TIMEOUT
            )
            result.settimeout(SOCKET_TIMEOUT)
            return result
        except OSError as error:
            last_error = error
            time.sleep(0.02)
    raise RuntimeError(f"server on port {port} did not start: {last_error}")


def read_process_output(process, timeout=1.0):
    """Read currently available child output without waiting for process exit."""
    ready, _, _ = select.select([process.stdout], [], [], timeout)
    if not ready:
        return b""
    return os.read(process.stdout.fileno(), 4096)


class Process:
    def __init__(self, executable, stdin=None):
        self.process = subprocess.Popen(
            [executable],
            cwd=ROOT,
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

    def stop(self):
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=1)
        if self.process.stdin is not None:
            self.process.stdin.close()
        if self.process.stdout is not None:
            self.process.stdout.close()


class LegacyServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Process(os.path.join(BIN, "legacy_server"))
        probe = connect(4551)
        probe.close()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_relays_binary_data_to_another_client(self):
        first, second = connect(4551), connect(4551)
        self.addCleanup(first.close)
        self.addCleanup(second.close)
        time.sleep(0.05)

        first.sendall(b"a\x00b")

        self.assertEqual(recv_exact(second, 3), b"a\x00b")

    def test_does_not_echo_to_sender(self):
        sender = connect(4551)
        self.addCleanup(sender.close)

        sender.sendall(b"private")

        with self.assertRaises(socket.timeout):
            sender.recv(1)

    def test_survives_full_1024_byte_receive_buffer(self):
        first, second = connect(4551), connect(4551)
        self.addCleanup(first.close)
        self.addCleanup(second.close)
        time.sleep(0.05)

        first.sendall(b"x" * 1024)

        self.assertEqual(recv_exact(second, 1024), b"x" * 1024)
        self.assertIsNone(self.server.process.poll(), "server crashed")


class SecretServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Process(os.path.join(BIN, "secret_server"))
        probe = connect(4552)
        recv_frame(probe)
        probe.close()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def handshake(self):
        sock = connect(4552)
        self.assertEqual(recv_frame(sock), (0, b"ok"))
        sock.sendall(encode_frame(1, b"ok"))
        return sock

    def test_valid_handshake(self):
        sock = self.handshake()
        sock.close()

    def test_rejects_wrong_handshake_code(self):
        sock = connect(4552)
        self.addCleanup(sock.close)
        self.assertEqual(recv_frame(sock), (0, b"ok"))

        sock.sendall(encode_frame(99, b"no"))

        self.assertEqual(sock.recv(1), b"")

    def test_accepts_fragmented_handshake(self):
        sock = connect(4552)
        self.addCleanup(sock.close)
        self.assertEqual(recv_frame(sock), (0, b"ok"))
        reply = encode_frame(1, b"ok")

        for byte in reply:
            sock.sendall(bytes([byte]))
            time.sleep(0.01)

        time.sleep(0.05)
        self.assertIsNone(self.server.process.poll(), "server crashed")

    def test_relays_message_between_authenticated_clients(self):
        first, second = self.handshake(), self.handshake()
        self.addCleanup(first.close)
        self.addCleanup(second.close)

        first.sendall(encode_frame(2, b"hello"))

        self.assertEqual(recv_frame(second), (2, b"hello"))


class ClientTests(unittest.TestCase):
    def test_client_handshake_send_and_receive(self):
        ready = threading.Event()
        observed = {}

        def fake_server():
            listener = socket.socket()
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                listener.bind(("127.0.0.1", 4552))
                listener.listen(1)
                ready.set()
                conn, _ = listener.accept()
                try:
                    conn.settimeout(1)
                    conn.sendall(encode_frame(0, b"ok"))
                    observed["ack"] = recv_frame(conn)
                    observed["message"] = recv_frame(conn)
                    conn.sendall(encode_frame(2, b"from-server"))
                    time.sleep(0.1)
                finally:
                    conn.close()
            except Exception as error:  # propagated through observed below
                observed["error"] = repr(error)
                ready.set()
            finally:
                listener.close()

        thread = threading.Thread(target=fake_server, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(1), "fake server did not start")

        client = Process(
            os.path.join(BIN, "client"),
            stdin=subprocess.PIPE,
        )
        try:
            client.process.stdin.write(b"from-client\n")
            client.process.stdin.flush()
            thread.join(timeout=2)

            self.assertFalse(thread.is_alive(), "fake server did not finish")
            self.assertNotIn("error", observed, observed.get("error"))
            self.assertEqual(observed.get("ack"), (1, b"ok"))
            self.assertEqual(observed.get("message"), (2, b"from-client"))
            self.assertIn(
                b"from-server",
                read_process_output(client.process),
            )
        finally:
            client.stop()


if __name__ == "__main__":
    unittest.main(verbosity=2)

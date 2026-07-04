#!/usr/bin/env python3
"""Black-box TCP tests for both servers and the interactive client."""

import os
import signal
import socket
import struct
import subprocess
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "tests", "bin")
NATIVE_U16 = "=H"


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise ConnectionError("peer closed connection")
        data.extend(part)
    return bytes(data)


def encode_frame(code, payload=b""):
    return struct.pack(NATIVE_U16, len(payload) + 3) + bytes([code]) + payload


def recv_frame(sock):
    length = struct.unpack(NATIVE_U16, recv_exact(sock, 2))[0]
    if length < 3:
        raise ValueError("invalid frame length")
    code = recv_exact(sock, 1)[0]
    return code, recv_exact(sock, length - 3)


def connect(port, attempts=50):
    for _ in range(attempts):
        try:
            result = socket.create_connection(("127.0.0.1", port), timeout=0.25)
            result.settimeout(0.5)
            return result
        except OSError:
            time.sleep(0.02)
    raise RuntimeError(f"server on port {port} did not start")


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
        first = connect(4551)
        self.addCleanup(first.close)
        first.sendall(b"private")
        with self.assertRaises(socket.timeout):
            first.recv(1)

    def test_survives_full_1024_byte_receive_buffer(self):
        first, second = connect(4551), connect(4551)
        self.addCleanup(first.close)
        self.addCleanup(second.close)
        time.sleep(0.05)
        first.sendall(b"x" * 1024)
        self.assertEqual(len(recv_exact(second, 1024)), 1024)
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
        sock.sendall(reply[:1])
        time.sleep(0.05)
        sock.sendall(reply[1:])
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
            listener.bind(("127.0.0.1", 4552))
            listener.listen(1)
            ready.set()
            conn, _ = listener.accept()
            conn.settimeout(1)
            conn.sendall(encode_frame(0, b"ok"))
            observed["ack"] = recv_frame(conn)
            observed["message"] = recv_frame(conn)
            conn.sendall(encode_frame(2, b"from-server"))
            time.sleep(0.1)
            conn.close()
            listener.close()

        thread = threading.Thread(target=fake_server, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(1))
        client = Process(
            os.path.join(BIN, "client"),
            stdin=subprocess.PIPE,
        )
        try:
            client.process.stdin.write(b"from-client\n")
            client.process.stdin.flush()
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive(), "fake server did not finish")
            self.assertEqual(observed.get("ack"), (1, b"ok"))
            self.assertEqual(observed.get("message"), (2, b"from-client"))
            time.sleep(0.1)
            output = client.process.stdout.read1(4096)
            self.assertIn(b"from-server", output)
        finally:
            client.stop()


if __name__ == "__main__":
    unittest.main(verbosity=2)

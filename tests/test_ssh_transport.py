#!/usr/bin/env python3
"""Build/test the embedded SSH engine with SDK mbedTLS, sanitizers and stock ssh.

Usage: python3 tests/test_ssh_transport.py --sdk /path/to/pico-sdk
Requires clang/clang++, Python 3 and OpenSSH; installs no packages.
All credentials and host keys here are test-only, on localhost.
"""
import argparse
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def build(sdk, directory):
    mbed = sdk / "lib/mbedtls"
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             "-I" + str(mbed / "include"), "-I" + str(ROOT / "tests"),
             '-DMBEDTLS_CONFIG_FILE="ssh_mbedtls_config.h"']
    objects = []
    for name in ("aes", "cipher", "cipher_wrap", "constant_time", "gcm",
                 "platform_util", "platform", "sha256", "block_cipher"):
        output = directory / (name + ".o")
        run(["clang", *flags, "-c", str(mbed / "library" / (name + ".c")), "-o", str(output)])
        objects.append(str(output))
    common = [str(ROOT / p) for p in (
        "third_party/ssh/staticnet/crypt/CryptoEngine.cpp",
        "third_party/ssh/staticnet/contrib/tweetnacl_25519.cpp",
        "third_party/ssh/staticnet/contrib/base64.cpp")]
    for target, sources in {
        "ssh-host": ["src/ssh_transport.cpp", "tests/ssh_transport_host.cpp"],
        "ssh-fuzz": ["tests/ssh_transport_fuzz.cpp"],
    }.items():
        run(["clang++", "-std=c++17", *flags, "-I" + str(ROOT / "src"),
             *[str(ROOT / p) for p in sources], *common, *objects,
             "-o", str(directory / target)])


def build_adapter(sdk, directory):
    lwip = sdk / "lib/lwip/src"
    run(["clang", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-I" + str(ROOT / "tests/ssh_adapter_stubs"), "-I" + str(ROOT / "src"),
         "-I" + str(lwip / "include"), str(ROOT / "tests/ssh_transport_lwip_test.c"),
         *[str(lwip / "core" / (name + ".c")) for name in ("pbuf", "mem", "memp", "def")],
         "-o", str(directory / "ssh-adapter")])


class Client:
    def __init__(self, port, askpass, password="test-pass-123", **options):
        env = dict(os.environ, SSH_ASKPASS=str(askpass), SSH_ASKPASS_REQUIRE="force",
                   DISPLAY=":0", APPLE2_TEST_PASSWORD=password)
        command = ["ssh", "-F", "/dev/null", "-tt", "-p", str(port), "-o", "ConnectTimeout=5",
                   "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                   "-o", "PreferredAuthentications=password", "-o", "LogLevel=ERROR"]
        for name, value in options.items():
            command.extend(["-o", f"{name}={value}"])
        self.process = subprocess.Popen(command + ["apple@127.0.0.1"], env=env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.received = b""

    def until(self, marker, timeout=10):
        until = time.monotonic() + timeout
        while marker not in self.received and time.monotonic() < until:
            if select.select([self.process.stdout], [], [], .1)[0]:
                data = os.read(self.process.stdout.fileno(), 65536)
                if not data:
                    break
                self.received += data
        assert marker in self.received, ("SSH output missing", marker[:80], self.received[-200:])

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def close(self):
        self.process.terminate()
        self.process.communicate(timeout=5)


def check_wire_rejection(port):
    for length in (0, 1, 5, 8192, 0xffffffff):
        with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
            s.recv(128)
            for byte in b"SSH-2.0-test-split\r\n":
                s.sendall(bytes([byte]))
            s.sendall(struct.pack(">I", length))
            reply = s.recv(1024)
            assert reply, ("expected protocol disconnect", length)
        time.sleep(.01)


class CorruptEncryptedPacket:
    """Flip one bit after NEWKEYS; framing length stays intact, GCM must reject."""
    def __init__(self, target_port):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.target_port = target_port
        self.corrupted = False
        self.thread = threading.Thread(target=self.forward, daemon=True)
        self.thread.start()

    def forward(self):
        local, _ = self.listener.accept()
        with local, socket.create_connection(("127.0.0.1", self.target_port), timeout=3) as remote:
            pending = b""
            banner = True
            encrypted = False
            deadline = time.monotonic() + 15
            try:
                while time.monotonic() < deadline:
                    readable, _, _ = select.select([local, remote], [], [], .1)
                    for connection in readable:
                        data = connection.recv(65536)
                        if not data:
                            return
                        if connection is remote:
                            local.sendall(data)
                            continue
                        pending += data
                        if banner and b"\n" in pending:
                            header, pending = pending.split(b"\n", 1)
                            remote.sendall(header + b"\n")
                            banner = False
                        while not banner and len(pending) >= 4:
                            length = struct.unpack(">I", pending[:4])[0] + 4 + (16 if encrypted else 0)
                            if len(pending) < length:
                                break
                            packet, pending = pending[:length], pending[length:]
                            if encrypted and not self.corrupted:
                                packet = packet[:-1] + bytes([packet[-1] ^ 1])
                                self.corrupted = True
                            elif not encrypted and packet[5] == 21:
                                encrypted = True
                            remote.sendall(packet)
            except (ConnectionError, OSError):
                pass

    def close(self):
        self.listener.close()
        self.thread.join(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--adapter-only", action="store_true", help="Only test raw lwIP callbacks/pbuf ownership")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="apple2-ssh-test-") as temporary:
        directory = Path(temporary)
        if not args.adapter_only:
            build(args.sdk.resolve(), directory)
        build_adapter(args.sdk.resolve(), directory)
        env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1",
                   ASAN_OPTIONS="detect_leaks=0" if sys.platform == "darwin" else "detect_leaks=1")
        run([str(directory / "ssh-adapter")], env=env, timeout=30)
        if args.adapter_only:
            return
        run([str(directory / "ssh-fuzz")], env=env, timeout=60)
        askpass = directory / "askpass"
        askpass.write_text('#!/bin/sh\nprintf %s "$APPLE2_TEST_PASSWORD"\n')
        askpass.chmod(0o700)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        errors = directory / "server-errors.log"
        with errors.open("w+") as error_log:
            server = subprocess.Popen([str(directory / "ssh-host"), str(port), "slow", "fast-clock"],
                                      env=env, stdout=subprocess.PIPE, stderr=error_log)
            try:
                ready = server.stdout.readline().decode().strip()
                assert ready.startswith("READY "), ready
                print(ready)
                check_wire_rejection(port)
                first = Client(port, askpass)
                try:
                    first.until(b"APPLE II SSH TEST\r\n]")
                    # The host adapter runs its clock 20x: this is 120 seconds
                    # of server time with no user activity.
                    time.sleep(6)
                    first.send(b"IDLE-CONNECTION-STILL-ALIVE")
                    first.until(b"IDLE-CONNECTION-STILL-ALIVE")
                    first.send(b"PRINT 2+2\r")
                    first.until(b"PRINT 2+2\r")
                    first.send(b"\x03\x1b[A\x7f")
                    first.until(b"\x03\x1b[A\x7f")
                    # A second client must not steal or corrupt the first session.
                    second = Client(port, askpass)
                    _, error = second.process.communicate(timeout=10)
                    assert second.process.returncode != 0, error
                    first.send(b"FIRST-STILL-CONNECTED")
                    first.until(b"FIRST-STILL-CONNECTED")
                    # Paste far beyond the 2 KiB window while application consumes 7 bytes/tick.
                    pasted = (b"0123456789ABCDEF" * 4096) + b"END-OF-PASTE"
                    os.set_blocking(first.process.stdin.fileno(), False)
                    sent = 0
                    end = time.monotonic() + 70
                    while b"END-OF-PASTE" not in first.received and time.monotonic() < end:
                        readable, writable, _ = select.select([first.process.stdout], [first.process.stdin] if sent < len(pasted) else [], [], .1)
                        if writable:
                            try:
                                sent += os.write(first.process.stdin.fileno(), pasted[sent:sent+2048])
                            except BlockingIOError:
                                pass
                        if readable:
                            chunk = os.read(first.process.stdout.fileno(), 65536)
                            assert chunk, "SSH ended during paste"
                            first.received += chunk
                    assert pasted in first.received, (sent, len(first.received))
                    print("Stock OpenSSH: auth, PTY, keystrokes, busy rejection and 64 KiB bounded paste passed")
                finally:
                    first.close()
                time.sleep(.03)
                wrong = Client(port, askpass, password="wrong-password", NumberOfPasswordPrompts=3)
                _, error = wrong.process.communicate(timeout=10)
                assert wrong.process.returncode != 0
                assert b"Authentication failed" in error or b"Permission denied" in error, error
                time.sleep(.03)
                again = Client(port, askpass)
                try:
                    again.until(b"APPLE II SSH TEST\r\n]")
                    again.send(b"RECONNECTED")
                    again.until(b"RECONNECTED")
                finally:
                    again.close()
                print("Wrong password rejected; clean reconnect after failure passed")
                time.sleep(.03)
                piped = Client(port, askpass)
                output, error = piped.process.communicate(b"REDIRECTED-INPUT-END", timeout=10)
                assert b"REDIRECTED-INPUT-END" in output, (output, error)
                assert piped.process.returncode == 0, error
                time.sleep(.03)
                corrupt = CorruptEncryptedPacket(port)
                try:
                    tampered = Client(corrupt.port, askpass)
                    _, error = tampered.process.communicate(timeout=10)
                    assert corrupt.corrupted and tampered.process.returncode != 0, error
                    assert b"authentication tag mismatch" in error, error
                finally:
                    corrupt.close()
                time.sleep(.03)
                rekey = Client(port, askpass, RekeyLimit="1K")
                try:
                    _, error = rekey.process.communicate(b"X" * 4096, timeout=10)
                    assert rekey.process.returncode != 0 and b"rekey requested" in error, error
                finally:
                    if rekey.process.poll() is None:
                        rekey.close()
                print("Redirected input drained with exit status 0; tampered GCM and rekey handled cleanly")
                assert server.poll() is None, "Sanitizer or server crash"
            finally:
                server.terminate()
                server.wait(timeout=5)
            error_log.flush()
            assert not errors.read_text(), errors.read_text()
        print("All SSH transport checks passed (AddressSanitizer + UndefinedBehaviorSanitizer)")


if __name__ == "__main__":
    main()

"""Client for the engine's test bridge (--test-bridge <port>).

Protocol: one JSON object per line, both ways, on 127.0.0.1.
    request:  {"id": 7, "cmd": "state", ...arguments}
    response: {"id": 7, "ok": true, ...data}   or   {"id": 7, "ok": false, "error": "..."}
Commands are run on the engine's main thread between frames, so a response
arrives one frame or more after the request.
"""

import json
import socket
import time


class BridgeError(RuntimeError):
    pass


class Bridge:
    def __init__(self, port, host="127.0.0.1"):
        self.port = port
        self.host = host
        self.sock = None
        self.buf = b""
        self.next_id = 1

    def connect(self, timeout=120.0, alive=None):
        """Wait for the engine to open its bridge (it starts during engine init)."""
        end = time.time() + timeout

        while time.time() < end:
            if alive is not None and not alive():
                raise BridgeError(f"instance exited before its bridge opened (port {self.port})")
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=2.0)
                self.sock.settimeout(None)
                return self
            except OSError:
                time.sleep(0.5)

        raise BridgeError(f"bridge on port {self.port} did not open within {timeout:.0f} s")

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def call(self, cmd, timeout=30.0, **args):
        if self.sock is None:
            raise BridgeError("not connected")
        req_id = self.next_id
        self.next_id += 1
        payload = dict(args, id=req_id, cmd=cmd)
        self.sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        end = time.time() + timeout

        while True:
            line = self._read_line(end)
            msg = json.loads(line)
            if msg.get("id") != req_id:
                continue  # an answer to a request that timed out earlier
            if not msg.get("ok", False):
                raise BridgeError(f"{cmd}: {msg.get('error', 'failed')}")
            return msg

    def _read_line(self, end):
        while b"\n" not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise BridgeError("timed out waiting for the engine")
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                raise BridgeError("timed out waiting for the engine")
            if not chunk:
                raise BridgeError("bridge closed by the engine")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", errors="replace")

    # Convenience wrappers ------------------------------------------------------------

    def wait(self, predicate, timeout, step=0.25, what="condition"):
        end = time.time() + timeout
        while time.time() < end:
            value = predicate()
            if value:
                return value
            time.sleep(step)
        raise BridgeError(f"timed out waiting for {what}")

    def status(self):
        return self.call("status")

    def state(self):
        return self.call("state")["characters"]

    def me(self):
        """This machine's own character (the engine knows which one, online too)."""
        r = self.call("state")
        return next((c for c in r["characters"] if c["id"] == r["my_avatar"]), None)

    def hold(self, steps=-1, **bindings):
        """Hold bindings (e.g. up=1.0, attack=1.0) for a number of 120 Hz
        steps, then release them; steps=-1 holds until released."""
        return self.call("input", hold=bindings, steps=steps)

    def release(self, *bindings):
        return self.call("input", release=list(bindings))

    def release_all(self):
        return self.call("input", release_all=True)

#!/usr/bin/env python3
"""Exercise the QMP runner against a host-only Unix-socket protocol peer."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import socket
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT / "userspace/waydroid/tests/waydroid_qa_host_runner.py"
SPEC = importlib.util.spec_from_file_location("openwave_qmp_runner", RUNNER)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RunningProcess:
    @staticmethod
    def poll():
        return None


def serve_connection(connection: socket.socket, active: set[socket.socket], lock: threading.Lock,
                     errors: list[BaseException]) -> None:
    with lock:
        active.add(connection)
    try:
        connection.sendall(b'{"QMP":{"version":{"qemu":{"major":9}},"capabilities":[]}}\r\n')
        stream = connection.makefile("rb")
        for line in stream:
            request = json.loads(line)
            command = request["execute"]
            response = {"return": {}, "id": request.get("id")}
            if command == "query-status":
                response["return"] = {"status": "running", "running": True}
            elif command == "system_powerdown":
                connection.sendall(b'{"event":"SHUTDOWN","data":{"guest":true}}\r\n')
            connection.sendall((json.dumps(response) + "\r\n").encode())
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors.append(error)
    finally:
        with lock:
            active.discard(connection)
        connection.close()


def wait_for(predicate, message: str) -> None:
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError(message)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="openwave-qmp-test-") as directory:
        path = Path(directory) / "qmp.sock"
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(path))
        listener.listen(4)
        active: set[socket.socket] = set()
        lock = threading.Lock()
        errors: list[BaseException] = []
        accepted = 0

        def accept_loop() -> None:
            nonlocal accepted
            while accepted < 3:
                try:
                    connection, _ = listener.accept()
                except OSError:
                    return
                accepted += 1
                threading.Thread(target=serve_connection,
                                 args=(connection, active, lock, errors), daemon=True).start()

        accept_thread = threading.Thread(target=accept_loop, daemon=True)
        accept_thread.start()
        clients = []
        try:
            first = MODULE.connect_qmp(path, RunningProcess(), 2)
            clients.append(first)
            second = MODULE.connect_qmp(path, RunningProcess(), 2)
            clients.append(second)
            wait_for(lambda: len(active) == 2, "concurrent QMP clients did not both receive greetings")
            assert first.query_status()["return"]["status"] == "running"
            first.close()
            wait_for(lambda: len(active) == 1, "closing the first QMP client did not release its connection")

            third = MODULE.connect_qmp(path, RunningProcess(), 2)
            clients.append(third)
            wait_for(lambda: len(active) == 2, "released QMP slot could not be reused")
            assert second.request_powerdown()["return"] == {}
            assert second.wait_event("SHUTDOWN", 2) is not None
            second.close()
            third.close()
            wait_for(lambda: len(active) == 0, "QMP clients remained connected after close")
            if errors:
                raise AssertionError(f"protocol peer errors: {errors}")
        finally:
            for client in clients:
                client.close()
            listener.close()
        print("QMP_PROTOCOL_TEST PASS greeting capabilities concurrent_clients shutdown_event release")


if __name__ == "__main__":
    main()

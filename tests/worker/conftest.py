import os
import socket
import subprocess
import sys
import time
from contextlib import closing
from pathlib import Path

import psutil
import pytest

ROOT = Path(__file__).resolve().parents[2]
PY = os.environ.get("PYTHON_BIN", sys.executable)
WORKER = ROOT / "src" / "worker" / "worker.py"


def pick_free_udp_port() -> int:
    # Bind a TCP socket to get a free port number; reuse it for UDP.
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_udp_is_listening(port: int, timeout: float = 5.0) -> None:
    # UDP has no handshake; give the process a moment to bind.
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            # If the process printed its banner, it likely bound already.
            time.sleep(0.05)
            return
        except OSError:
            time.sleep(0.05)
    # We don't hard-fail here; the first send/recv will tell us anyway.


@pytest.fixture(scope="session")
def udp_worker_proc():
    if not WORKER.exists():
        pytest.skip(f"worker.py not found at {WORKER}")

    port = pick_free_udp_port()
    env = os.environ.copy()
    # start worker: python worker.py <port>
    proc = subprocess.Popen(
        [PY, str(WORKER), str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=ROOT,
    )

    # small wait to let it bind; also drain a line if printed
    wait_udp_is_listening(port, timeout=2.0)
    time.sleep(0.1)

    yield {"proc": proc, "port": port}

    # teardown: terminate process tree
    if proc.poll() is None:
        parent = psutil.Process(proc.pid)
        for child in parent.children(recursive=True):
            child.terminate()
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except Exception:
            proc.kill()

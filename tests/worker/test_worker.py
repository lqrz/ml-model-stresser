import json
import socket
import time
from pathlib import Path
import importlib.util


def send_udp(port: int, payload: bytes, timeout: float = 2.0) -> bytes:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    s.sendto(payload, ("127.0.0.1", port))
    data, _ = s.recvfrom(4096)
    return data


def test_udp_roundtrip_fixed_reply(udp_worker_proc):
    """End-to-end: the worker should reply with the fixed message."""
    port = udp_worker_proc["port"]
    data = send_udp(port, b"hello")
    assert data == b"Response from worker"


def test_process_request_unit():
    """Pure unit test of the helper function in worker.py."""
    worker_path = Path("src/worker/worker.py").resolve()
    spec = importlib.util.spec_from_file_location("worker_mod", worker_path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)  # type: ignore[attr-defined]

    out = mod.process_request("abc")
    # It should include the message and the PID; be lenient about exact format.
    assert "Processed message:" in out
    assert "abc" in out
    assert "PID" in out

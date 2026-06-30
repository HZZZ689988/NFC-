from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass
from typing import Callable

import serial
from serial.tools import list_ports


LineCallback = Callable[[str], None]


@dataclass(frozen=True)
class PortInfo:
    device: str
    description: str


def available_ports() -> list[PortInfo]:
    return [PortInfo(port.device, port.description) for port in list_ports.comports()]


class SerialClient:
    def __init__(self, on_line: LineCallback | None = None) -> None:
        self._serial: serial.Serial | None = None
        self._reader: threading.Thread | None = None
        self._stop = threading.Event()
        self._line_queue: queue.Queue[str] = queue.Queue()
        self._on_line = on_line
        self._lock = threading.Lock()
        self._transaction_lock = threading.Lock()

    @property
    def is_open(self) -> bool:
        return bool(self._serial and self._serial.is_open)

    def open(self, port: str, baudrate: int = 115200) -> None:
        self.close()
        self._stop.clear()
        self._serial = serial.Serial(port=port, baudrate=baudrate, timeout=0.1)
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        if self._reader and self._reader.is_alive():
            self._reader.join(timeout=0.5)
        if self._serial and self._serial.is_open:
            self._serial.close()
        self._serial = None
        self._reader = None

    def send(self, command: str) -> None:
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("串口未打开")
        data = command.encode("ascii")
        with self._lock:
            self._serial.write(data)
            self._serial.flush()

    def transact(self, command: str, timeout: float = 2.0) -> list[str]:
        with self._transaction_lock:
            self._drain_lines()
            self.send(command)
            deadline = time.monotonic() + timeout
            lines: list[str] = []
            while time.monotonic() < deadline:
                try:
                    line = self._line_queue.get(timeout=0.05)
                except queue.Empty:
                    continue
                lines.append(line)
                stripped = line.strip().upper()
                if (
                    stripped == "OK"
                    or stripped.startswith("OK:")
                    or stripped.startswith("UID:")
                    or stripped.startswith("ERR:")
                    or stripped == "LIST:END"
                ):
                    break
            return lines

    def _drain_lines(self) -> None:
        while True:
            try:
                self._line_queue.get_nowait()
            except queue.Empty:
                break

    def _read_loop(self) -> None:
        assert self._serial is not None
        buffer = bytearray()
        while not self._stop.is_set() and self._serial and self._serial.is_open:
            try:
                chunk = self._serial.read(128)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buffer.extend(chunk)
            while b"\n" in buffer:
                raw, _, rest = buffer.partition(b"\n")
                buffer = bytearray(rest)
                line = raw.decode("utf-8", errors="replace").strip("\r")
                self._line_queue.put(line)
                if self._on_line:
                    self._on_line(line)

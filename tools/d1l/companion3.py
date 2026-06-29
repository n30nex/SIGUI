"""MeshCore 3-byte companion transport helpers.

The transport frame is one direction byte followed by a uint16 little-endian
payload length and then the raw MeshCore companion protocol payload.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable


HEADER_SIZE = 3
MAX_FRAME_SIZE = 512
APP_TO_RADIO = ord("<")
RADIO_TO_APP = ord(">")
VALID_FRAME_TYPES = {APP_TO_RADIO, RADIO_TO_APP}


def _frame_type_value(frame_type: int | bytes | bytearray | str) -> int:
    if isinstance(frame_type, int):
        value = frame_type
    elif isinstance(frame_type, str):
        if len(frame_type) != 1:
            raise ValueError("frame_type must be one byte")
        value = ord(frame_type)
    else:
        if len(frame_type) != 1:
            raise ValueError("frame_type must be one byte")
        value = frame_type[0]
    if value not in VALID_FRAME_TYPES:
        raise ValueError("frame_type must be '<' or '>'")
    return value


def encode_frame(payload: bytes | bytearray | memoryview = b"", frame_type: int | bytes | bytearray | str = RADIO_TO_APP) -> bytes:
    payload_bytes = bytes(payload)
    if len(payload_bytes) > MAX_FRAME_SIZE:
        raise ValueError(f"payload exceeds {MAX_FRAME_SIZE} bytes")
    value = _frame_type_value(frame_type)
    return bytes((value, len(payload_bytes) & 0xFF, len(payload_bytes) >> 8)) + payload_bytes


@dataclass
class Companion3Decoder:
    max_frame_size: int = MAX_FRAME_SIZE
    _state: str = "idle"
    _frame_type: int = 0
    _frame_len: int = 0
    _drop_remaining: int = 0
    _buf: bytearray = field(default_factory=bytearray)
    dropped_frames: int = 0

    def feed(self, data: bytes | bytearray | memoryview | Iterable[int]) -> list[tuple[int, bytes]]:
        frames: list[tuple[int, bytes]] = []
        for raw in data:
            byte = raw if isinstance(raw, int) else int(raw)
            if self._state == "idle":
                if byte in VALID_FRAME_TYPES:
                    self._frame_type = byte
                    self._frame_len = 0
                    self._buf.clear()
                    self._state = "len_lsb"
            elif self._state == "len_lsb":
                self._frame_len = byte
                self._state = "len_msb"
            elif self._state == "len_msb":
                self._frame_len |= byte << 8
                if self._frame_len > self.max_frame_size:
                    self.dropped_frames += 1
                    self._drop_remaining = self._frame_len
                    self._state = "drop_payload"
                elif self._frame_len == 0:
                    frames.append((self._frame_type, b""))
                    self._reset()
                else:
                    self._state = "payload"
            elif self._state == "payload":
                self._buf.append(byte)
                if len(self._buf) >= self._frame_len:
                    frames.append((self._frame_type, bytes(self._buf)))
                    self._reset()
            elif self._state == "drop_payload":
                self._drop_remaining -= 1
                if self._drop_remaining <= 0:
                    self._reset()
            else:
                self._reset()
        return frames

    def _reset(self) -> None:
        self._state = "idle"
        self._frame_type = 0
        self._frame_len = 0
        self._drop_remaining = 0
        self._buf.clear()

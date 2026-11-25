"""NEXUS 1.0 envelope validation and stream framing."""

from __future__ import annotations

import asyncio
import json
import re
import struct
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any, Callable

PROTOCOL_VERSION = "1.0"
MAX_FRAME_BYTES = 65_536
_HEADER = struct.Struct(">I")
_MESSAGE_TYPE = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$")
_ENDPOINT_KINDS = {"device", "host", "backend", "simulator", "dashboard", "broadcast"}
_ENVELOPE_KEYS = {
    "protocol_version",
    "message_id",
    "correlation_id",
    "timestamp",
    "source",
    "destination",
    "message_type",
    "payload",
    "auth",
}


class ProtocolError(ValueError):
    """The peer sent data that cannot be accepted as a NEXUS message."""


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _parse_uuid(value: Any, field: str, *, nullable: bool = False) -> str | None:
    if value is None and nullable:
        return None
    if not isinstance(value, str):
        raise ProtocolError(f"{field} must be a UUID string")
    try:
        return str(uuid.UUID(value))
    except ValueError as exc:
        raise ProtocolError(f"{field} is not a valid UUID") from exc


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


@dataclass(frozen=True, slots=True)
class Endpoint:
    kind: str
    id: str

    def __post_init__(self) -> None:
        if self.kind not in _ENDPOINT_KINDS:
            raise ProtocolError(f"unsupported endpoint kind: {self.kind}")
        if not isinstance(self.id, str) or not 1 <= len(self.id) <= 128:
            raise ProtocolError("endpoint id must contain 1-128 characters")

    @classmethod
    def from_dict(cls, value: Any) -> "Endpoint":
        if not isinstance(value, dict) or set(value) != {"kind", "id"}:
            raise ProtocolError("endpoint must contain exactly kind and id")
        return cls(kind=value["kind"], id=value["id"])


@dataclass(frozen=True, slots=True)
class AuthContext:
    mode: str = "development"
    session_id: str | None = None
    sequence: int = 0

    def __post_init__(self) -> None:
        if self.mode not in {"development", "paired"}:
            raise ProtocolError("unsupported authentication mode")
        if self.session_id is not None and (
            not isinstance(self.session_id, str) or len(self.session_id) > 128
        ):
            raise ProtocolError("invalid session id")
        if not isinstance(self.sequence, int) or isinstance(self.sequence, bool) or self.sequence < 0:
            raise ProtocolError("sequence must be a non-negative integer")

    @classmethod
    def from_dict(cls, value: Any) -> "AuthContext":
        if not isinstance(value, dict) or set(value) != {"mode", "session_id", "sequence"}:
            raise ProtocolError("auth must contain exactly mode, session_id, and sequence")
        return cls(**value)


@dataclass(frozen=True, slots=True)
class Envelope:
    protocol_version: str
    message_id: str
    correlation_id: str | None
    timestamp: str
    source: Endpoint
    destination: Endpoint
    message_type: str
    payload: dict[str, Any]
    auth: AuthContext

    def __post_init__(self) -> None:
        if self.protocol_version != PROTOCOL_VERSION:
            raise ProtocolError(f"unsupported protocol version: {self.protocol_version}")
        _parse_uuid(self.message_id, "message_id")
        _parse_uuid(self.correlation_id, "correlation_id", nullable=True)
        if not isinstance(self.timestamp, str):
            raise ProtocolError("timestamp must be an RFC 3339 string")
        try:
            parsed = datetime.fromisoformat(self.timestamp.replace("Z", "+00:00"))
        except ValueError as exc:
            raise ProtocolError("timestamp is not RFC 3339") from exc
        if parsed.tzinfo is None:
            raise ProtocolError("timestamp must include a timezone")
        if not isinstance(self.message_type, str) or not _MESSAGE_TYPE.fullmatch(self.message_type):
            raise ProtocolError("invalid message_type")
        if len(self.message_type) > 96:
            raise ProtocolError("message_type is too long")
        if not isinstance(self.payload, dict):
            raise ProtocolError("payload must be an object")

    @classmethod
    def new(
        cls,
        message_type: str,
        payload: dict[str, Any],
        *,
        source: Endpoint,
        destination: Endpoint,
        correlation_id: str | None = None,
        auth: AuthContext | None = None,
    ) -> "Envelope":
        return cls(
            protocol_version=PROTOCOL_VERSION,
            message_id=str(uuid.uuid4()),
            correlation_id=correlation_id,
            timestamp=utc_now(),
            source=source,
            destination=destination,
            message_type=message_type,
            payload=payload,
            auth=auth or AuthContext(),
        )

    @classmethod
    def from_dict(cls, value: Any) -> "Envelope":
        if not isinstance(value, dict):
            raise ProtocolError("envelope must be a JSON object")
        if set(value) != _ENVELOPE_KEYS:
            missing = sorted(_ENVELOPE_KEYS - set(value))
            extra = sorted(set(value) - _ENVELOPE_KEYS)
            raise ProtocolError(f"envelope keys mismatch; missing={missing}, extra={extra}")
        return cls(
            protocol_version=value["protocol_version"],
            message_id=_parse_uuid(value["message_id"], "message_id") or "",
            correlation_id=_parse_uuid(value["correlation_id"], "correlation_id", nullable=True),
            timestamp=value["timestamp"],
            source=Endpoint.from_dict(value["source"]),
            destination=Endpoint.from_dict(value["destination"]),
            message_type=value["message_type"],
            payload=value["payload"],
            auth=AuthContext.from_dict(value["auth"]),
        )

    @classmethod
    def decode(cls, raw: bytes) -> "Envelope":
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ProtocolError("payload is not UTF-8") from exc
        try:
            value = json.loads(text, object_pairs_hook=_reject_duplicate_keys)
        except json.JSONDecodeError as exc:
            raise ProtocolError("payload is not valid JSON") from exc
        return cls.from_dict(value)

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        return result

    def encode(self) -> bytes:
        return json.dumps(
            self.to_dict(), separators=(",", ":"), sort_keys=True, ensure_ascii=False
        ).encode("utf-8")


def encode_frame(envelope: Envelope, *, max_bytes: int = MAX_FRAME_BYTES) -> bytes:
    payload = envelope.encode()
    if not payload or len(payload) > max_bytes:
        raise ProtocolError(f"frame payload length {len(payload)} exceeds limit {max_bytes}")
    return _HEADER.pack(len(payload)) + payload


class FrameDecoder:
    """Incrementally decode zero or more length-prefixed envelopes."""

    def __init__(self, max_bytes: int = MAX_FRAME_BYTES) -> None:
        self.max_bytes = max_bytes
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Envelope]:
        if not isinstance(data, bytes):
            raise TypeError("frame input must be bytes")
        self._buffer.extend(data)
        messages: list[Envelope] = []
        while len(self._buffer) >= _HEADER.size:
            (size,) = _HEADER.unpack_from(self._buffer)
            if size == 0 or size > self.max_bytes:
                self._buffer.clear()
                raise ProtocolError(f"invalid frame size: {size}")
            frame_end = _HEADER.size + size
            if len(self._buffer) < frame_end:
                break
            payload = bytes(self._buffer[_HEADER.size:frame_end])
            del self._buffer[:frame_end]
            messages.append(Envelope.decode(payload))
        return messages


async def read_envelope(
    reader: asyncio.StreamReader, *, max_bytes: int = MAX_FRAME_BYTES
) -> Envelope:
    header = await reader.readexactly(_HEADER.size)
    (size,) = _HEADER.unpack(header)
    if size == 0 or size > max_bytes:
        raise ProtocolError(f"invalid frame size: {size}")
    return Envelope.decode(await reader.readexactly(size))


async def write_envelope(
    writer: asyncio.StreamWriter, envelope: Envelope, *, max_bytes: int = MAX_FRAME_BYTES
) -> None:
    writer.write(encode_frame(envelope, max_bytes=max_bytes))
    await writer.drain()

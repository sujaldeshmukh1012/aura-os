"""Loopback development host service and session state machine."""

from __future__ import annotations

import asyncio
import contextlib
import logging
import uuid
from pathlib import Path

from .protocol import (
    AuthContext,
    Endpoint,
    Envelope,
    ProtocolError,
    read_envelope,
    write_envelope,
)
from .telemetry import TelemetrySampler

LOG = logging.getLogger(__name__)


class HostService:
    def __init__(
        self,
        *,
        host: str = "127.0.0.1",
        port: int = 8765,
        project_dir: Path | None = None,
        telemetry_interval: float = 1.0,
    ) -> None:
        if host not in {"127.0.0.1", "::1", "localhost"}:
            raise ValueError("development host service must bind to loopback")
        self.host = host
        self.port = port
        self.telemetry_interval = telemetry_interval
        self.sampler = TelemetrySampler((project_dir or Path.cwd()).resolve())
        self.server: asyncio.Server | None = None

    @property
    def bound_port(self) -> int:
        if not self.server or not self.server.sockets:
            raise RuntimeError("service is not running")
        return int(self.server.sockets[0].getsockname()[1])

    async def start(self) -> None:
        self.server = await asyncio.start_server(self._handle_client, self.host, self.port)
        LOG.info("NEXUS host listening on %s:%d", self.host, self.bound_port)

    async def close(self) -> None:
        if self.server:
            self.server.close()
            await self.server.wait_closed()
            self.server = None

    async def serve_forever(self) -> None:
        await self.start()
        assert self.server is not None
        async with self.server:
            await self.server.serve_forever()

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        peer = writer.get_extra_info("peername")
        try:
            hello = await asyncio.wait_for(read_envelope(reader), timeout=5.0)
            if hello.message_type != "session.hello" or hello.auth.mode != "development":
                raise ProtocolError("session.hello is required before other messages")
            versions = hello.payload.get("supported_versions")
            if not isinstance(versions, list) or "1.0" not in versions:
                raise ProtocolError("peer does not support protocol 1.0")

            session_id = str(uuid.uuid4())
            sequence = 1
            auth = AuthContext(mode="development", session_id=session_id, sequence=sequence)
            host_endpoint = Endpoint("host", "local")
            await write_envelope(
                writer,
                Envelope.new(
                    "session.welcome",
                    {
                        "selected_version": "1.0",
                        "heartbeat_interval_seconds": 5,
                        "capabilities": ["telemetry", "state.snapshot"],
                    },
                    source=host_endpoint,
                    destination=hello.source,
                    correlation_id=hello.message_id,
                    auth=auth,
                ),
            )

            while True:
                sequence += 1
                auth = AuthContext("development", session_id, sequence)
                await write_envelope(
                    writer,
                    Envelope.new(
                        "telemetry.snapshot",
                        self.sampler.sample(),
                        source=host_endpoint,
                        destination=hello.source,
                        auth=auth,
                    ),
                )
                try:
                    incoming = await asyncio.wait_for(
                        read_envelope(reader), timeout=self.telemetry_interval
                    )
                except asyncio.TimeoutError:
                    continue
                if incoming.auth.session_id not in {None, session_id}:
                    raise ProtocolError("message belongs to a different session")
                if incoming.message_type == "heartbeat.ping":
                    sequence += 1
                    await write_envelope(
                        writer,
                        Envelope.new(
                            "heartbeat.pong",
                            {"alive": True},
                            source=host_endpoint,
                            destination=hello.source,
                            correlation_id=incoming.message_id,
                            auth=AuthContext("development", session_id, sequence),
                        ),
                    )
                else:
                    sequence += 1
                    await write_envelope(
                        writer,
                        Envelope.new(
                            "error.unsupported_message",
                            {"rejected_type": incoming.message_type},
                            source=host_endpoint,
                            destination=hello.source,
                            correlation_id=incoming.message_id,
                            auth=AuthContext("development", session_id, sequence),
                        ),
                    )
        except (asyncio.IncompleteReadError, ConnectionError):
            LOG.info("peer disconnected: %s", peer)
        except (ProtocolError, asyncio.TimeoutError) as exc:
            LOG.warning("rejected peer %s: %s", peer, exc)
        finally:
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

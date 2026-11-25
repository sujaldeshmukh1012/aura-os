import asyncio
import unittest
from pathlib import Path

from nexus_core.protocol import AuthContext, Endpoint, Envelope, read_envelope, write_envelope
from nexus_core.service import HostService


class ServiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.service = HostService(
            port=0,
            project_dir=Path.cwd(),
            telemetry_interval=0.05,
        )
        await self.service.start()

    async def asyncTearDown(self) -> None:
        await self.service.close()

    async def test_handshake_and_real_telemetry(self) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.service.bound_port)
        hello = Envelope.new(
            "session.hello",
            {"supported_versions": ["1.0"], "capabilities": ["simulator"]},
            source=Endpoint("simulator", "test-sim"),
            destination=Endpoint("host", "local"),
            auth=AuthContext(),
        )
        await write_envelope(writer, hello)
        welcome = await asyncio.wait_for(read_envelope(reader), timeout=1)
        telemetry = await asyncio.wait_for(read_envelope(reader), timeout=1)
        self.assertEqual(welcome.message_type, "session.welcome")
        self.assertEqual(welcome.correlation_id, hello.message_id)
        self.assertEqual(telemetry.message_type, "telemetry.snapshot")
        self.assertIn("disk_percent", telemetry.payload)
        self.assertEqual(welcome.auth.session_id, telemetry.auth.session_id)
        writer.close()
        await writer.wait_closed()

    async def test_non_hello_message_is_disconnected(self) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.service.bound_port)
        await write_envelope(
            writer,
            Envelope.new(
                "heartbeat.ping",
                {},
                source=Endpoint("simulator", "test-sim"),
                destination=Endpoint("host", "local"),
            ),
        )
        self.assertEqual(await asyncio.wait_for(reader.read(), timeout=1), b"")
        writer.close()
        await writer.wait_closed()


if __name__ == "__main__":
    unittest.main()

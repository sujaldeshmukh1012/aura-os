from __future__ import annotations

import argparse
import asyncio
import json
import logging
import mimetypes
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from nexus_core.protocol import AuthContext, Endpoint, Envelope, read_envelope, write_envelope

from .model import SimulatorState

LOG = logging.getLogger(__name__)
WEB_ROOT = Path(__file__).with_name("web")


def make_handler(state: SimulatorState) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 - stdlib callback name
            if self.path == "/api/state":
                self._send_json(state.snapshot())
                return
            if self.path == "/health":
                self._send_json({"status": "ok"})
                return
            files = {"/": "index.html", "/app.js": "app.js", "/styles.css": "styles.css"}
            filename = files.get(self.path)
            if not filename:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            body = (WEB_ROOT / filename).read_bytes()
            content_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", f"{content_type}; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _send_json(self, value: Any) -> None:
            body = json.dumps(value, separators=(",", ":")).encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: object) -> None:
            LOG.debug(format, *args)

    return Handler


async def connect_to_host(state: SimulatorState, host: str, port: int) -> None:
    backoff = 0.25
    simulator = Endpoint("simulator", "simulator-dev")
    host_endpoint = Endpoint("host", "local")
    while True:
        try:
            state.set_connection("connecting", f"Connecting to {host}:{port}")
            reader, writer = await asyncio.open_connection(host, port)
            await write_envelope(
                writer,
                Envelope.new(
                    "session.hello",
                    {
                        "supported_versions": ["1.0"],
                        "capabilities": ["display.480x320", "touch", "simulator"],
                    },
                    source=simulator,
                    destination=host_endpoint,
                    auth=AuthContext(),
                ),
            )
            backoff = 0.25
            while True:
                state.apply(await read_envelope(reader))
        except (ConnectionError, OSError, asyncio.IncompleteReadError) as exc:
            state.set_connection("offline", f"Host unavailable: {type(exc).__name__}")
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2.0, 5.0)


def load_scenario(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("scenario root must be an object")
    return value


async def run(args: argparse.Namespace) -> None:
    state = SimulatorState()
    scenario = load_scenario(args.scenario)
    if scenario:
        state.apply_scenario(scenario)
    httpd = ThreadingHTTPServer((args.web_host, args.web_port), make_handler(state))
    thread = threading.Thread(target=httpd.serve_forever, name="nexus-simulator-web", daemon=True)
    thread.start()
    LOG.info("Simulator dashboard: http://%s:%d", args.web_host, args.web_port)
    try:
        await connect_to_host(state, args.host, args.port)
    finally:
        httpd.shutdown()
        httpd.server_close()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Run the NEXUS device simulator")
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port", type=int, default=8765)
    result.add_argument("--web-host", default="127.0.0.1")
    result.add_argument("--web-port", type=int, default=8080)
    result.add_argument("--scenario", type=Path)
    result.add_argument("--log-level", default="INFO")
    return result


def main() -> None:
    args = parser().parse_args()
    if args.web_host not in {"127.0.0.1", "::1", "localhost"}:
        raise SystemExit("development simulator must bind to loopback")
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

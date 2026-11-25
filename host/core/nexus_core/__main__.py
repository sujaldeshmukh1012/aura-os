from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

from .service import HostService


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Run the NEXUS local host service")
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port", type=int, default=8765)
    result.add_argument("--project-dir", type=Path, default=Path.cwd())
    result.add_argument("--interval", type=float, default=1.0)
    result.add_argument("--log-level", default="INFO")
    return result


def main() -> None:
    args = parser().parse_args()
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    service = HostService(
        host=args.host,
        port=args.port,
        project_dir=args.project_dir,
        telemetry_interval=args.interval,
    )
    try:
        asyncio.run(service.serve_forever())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

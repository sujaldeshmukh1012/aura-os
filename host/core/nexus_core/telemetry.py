"""Low-overhead, credential-free host telemetry."""

from __future__ import annotations

import os
import platform
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


def _run(argv: list[str], timeout: float = 1.0) -> str | None:
    try:
        result = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def _bounded_percent(value: float | None) -> float | None:
    if value is None:
        return None
    return round(max(0.0, min(100.0, value)), 1)


def cpu_load_percent() -> float | None:
    try:
        one_minute = os.getloadavg()[0]
        cores = os.cpu_count() or 1
    except (AttributeError, OSError):
        return None
    return _bounded_percent(one_minute / cores * 100.0)


def memory_percent() -> float | None:
    if platform.system() == "Darwin":
        output = _run(["vm_stat"])
        total_output = _run(["sysctl", "-n", "hw.memsize"])
        if output and total_output and total_output.isdigit():
            page_size_match = re.search(r"page size of (\d+) bytes", output)
            page_size = int(page_size_match.group(1)) if page_size_match else 4096
            used_pages = 0
            for key in ("Pages active", "Pages wired down", "Pages occupied by compressor"):
                match = re.search(rf"^{key}:\s+(\d+)\.", output, re.MULTILINE)
                if match:
                    used_pages += int(match.group(1))
            total = int(total_output)
            return _bounded_percent(used_pages * page_size / total * 100.0)
    try:
        values: dict[str, int] = {}
        with open("/proc/meminfo", encoding="utf-8") as handle:
            for line in handle:
                key, raw = line.split(":", 1)
                values[key] = int(raw.strip().split()[0])
        total = values["MemTotal"]
        return _bounded_percent((total - values["MemAvailable"]) / total * 100.0)
    except (OSError, KeyError, ValueError):
        return None


def battery() -> tuple[int | None, bool | None]:
    if platform.system() != "Darwin":
        return None, None
    output = _run(["pmset", "-g", "batt"])
    if not output:
        return None, None
    percent = re.search(r"(\d+)%", output)
    lowered = output.lower()
    charging = False if "discharging" in lowered else (
        "charging" in lowered or "charged" in lowered or "ac power" in lowered
    )
    return (int(percent.group(1)) if percent else None), charging


def repository_status(project_dir: Path) -> dict[str, Any]:
    branch = _run(["git", "-C", str(project_dir), "branch", "--show-current"])
    status = _run(["git", "-C", str(project_dir), "status", "--porcelain"])
    return {
        "available": branch is not None and status is not None,
        "branch": branch,
        "dirty": bool(status) if status is not None else None,
    }


@dataclass(slots=True)
class TelemetrySampler:
    project_dir: Path

    def sample(self) -> dict[str, Any]:
        disk = shutil.disk_usage(self.project_dir)
        battery_percent, charging = battery()
        return {
            "cpu_percent": cpu_load_percent(),
            "cpu_count": os.cpu_count(),
            "memory_percent": memory_percent(),
            "disk_percent": _bounded_percent(disk.used / disk.total * 100.0),
            "battery_percent": battery_percent,
            "battery_charging": charging,
            "hostname": platform.node(),
            "os": platform.platform(),
            "repository": repository_status(self.project_dir),
            "freshness": "fresh",
        }

from __future__ import annotations

import copy
import threading
from datetime import datetime, timezone
from typing import Any

from nexus_core.protocol import Envelope


class SimulatorState:
    """Thread-safe device read model fed only by protocol envelopes."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._state: dict[str, Any] = {
            "connection": {
                "status": "offline",
                "detail": "Waiting for host",
                "last_message_at": None,
            },
            "home": {
                "brand": "NEXUS",
                "active_project": "Foundation",
                "work_mode": "Build",
                "next_commitment": "No calendar provider configured",
                "priority_tasks": [
                    "Verify ES3C40P PCB revision",
                    "Test USB framing on hardware",
                ],
                "attention_count": 0,
                "agents": [],
                "pending_approvals": 0,
            },
            "telemetry": {},
            "last_error": None,
        }

    def set_connection(self, status: str, detail: str) -> None:
        with self._lock:
            self._state["connection"]["status"] = status
            self._state["connection"]["detail"] = detail

    def apply(self, envelope: Envelope) -> None:
        now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        with self._lock:
            self._state["connection"].update(
                {"status": "online", "detail": "Host linked", "last_message_at": now}
            )
            if envelope.message_type == "session.welcome":
                self._state["connection"]["session_id"] = envelope.auth.session_id
                self._state["connection"]["protocol_version"] = envelope.payload.get(
                    "selected_version"
                )
            elif envelope.message_type == "telemetry.snapshot":
                self._state["telemetry"] = copy.deepcopy(envelope.payload)
            elif envelope.message_type == "state.snapshot":
                home = envelope.payload.get("home")
                if isinstance(home, dict):
                    self._state["home"].update(copy.deepcopy(home))
            elif envelope.message_type.startswith("error."):
                self._state["last_error"] = copy.deepcopy(envelope.payload)

    def apply_scenario(self, scenario: dict[str, Any]) -> None:
        with self._lock:
            for section in ("home", "telemetry"):
                value = scenario.get(section)
                if isinstance(value, dict):
                    self._state[section].update(copy.deepcopy(value))

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return copy.deepcopy(self._state)

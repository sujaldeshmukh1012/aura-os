import unittest

from nexus_core.protocol import AuthContext, Endpoint, Envelope
from nexus_simulator.model import SimulatorState


class SimulatorStateTests(unittest.TestCase):
    def test_telemetry_envelope_updates_read_model(self) -> None:
        state = SimulatorState()
        state.apply(
            Envelope.new(
                "telemetry.snapshot",
                {"cpu_percent": 42, "hostname": "test-host"},
                source=Endpoint("host", "local"),
                destination=Endpoint("simulator", "test"),
                auth=AuthContext("development", "test-session", 2),
            )
        )
        snapshot = state.snapshot()
        self.assertEqual(snapshot["connection"]["status"], "online")
        self.assertEqual(snapshot["telemetry"]["cpu_percent"], 42)
        self.assertEqual(snapshot["telemetry"]["hostname"], "test-host")

    def test_scenario_updates_only_known_sections(self) -> None:
        state = SimulatorState()
        state.apply_scenario(
            {
                "home": {"active_project": "Robotics"},
                "telemetry": {"disk_percent": 10},
                "ignored": {"unsafe": True},
            }
        )
        snapshot = state.snapshot()
        self.assertEqual(snapshot["home"]["active_project"], "Robotics")
        self.assertEqual(snapshot["telemetry"]["disk_percent"], 10)
        self.assertNotIn("ignored", snapshot)


if __name__ == "__main__":
    unittest.main()

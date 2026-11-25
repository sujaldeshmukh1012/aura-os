import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from nexus_core.telemetry import TelemetrySampler, battery


class TelemetryTests(unittest.TestCase):
    def test_snapshot_is_bounded_and_serializable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            snapshot = TelemetrySampler(Path(directory)).sample()
        for key in ("cpu_percent", "memory_percent", "disk_percent"):
            value = snapshot[key]
            if value is not None:
                self.assertGreaterEqual(value, 0)
                self.assertLessEqual(value, 100)
        self.assertEqual(snapshot["freshness"], "fresh")
        self.assertIn("repository", snapshot)

    @patch("nexus_core.telemetry.platform.system", return_value="Darwin")
    @patch("nexus_core.telemetry._run", return_value="InternalBattery-0 70%; discharging")
    def test_discharging_is_not_classified_as_charging(self, _run, _system) -> None:
        self.assertEqual(battery(), (70, False))


if __name__ == "__main__":
    unittest.main()

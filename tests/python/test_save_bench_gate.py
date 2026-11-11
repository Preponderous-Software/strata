"""modules/save/bench/gate.py, the save core's benchmark gate (RFC 0019 §9).

The gate must fail when the benchmark produced nothing for a size it budgets:
CI pipes strata-save-bench into the results file, so a crashed or failed run
shows up only as a missing line. No library and no benchmark run is needed.
"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "modules" / "save" / "bench" / "gate.py"
BASELINE = ROOT / "modules" / "save" / "bench" / "baseline.json"


def result_for(agents):
    """A results line equal to the stored baseline for `agents`."""
    line = dict(json.loads(BASELINE.read_text(encoding="utf-8"))[str(agents)])
    line["agents"] = agents
    line["round_trip"] = "EXACT"
    return json.dumps(line)


class SaveBenchGateTest(unittest.TestCase):
    def run_gate(self, lines):
        with tempfile.TemporaryDirectory() as tmp:
            results = Path(tmp) / "results.jsonl"
            results.write_text("".join(line + "\n" for line in lines), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(GATE), str(results), str(BASELINE)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                universal_newlines=True,
            )

    def test_empty_results_fail(self):
        proc = self.run_gate([])
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn("::error::1000 agents: no result", proc.stdout)
        self.assertIn("::error::10000 agents: no result", proc.stdout)

    def test_a_missing_size_fails(self):
        proc = self.run_gate([result_for(1000)])
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn("::error::10000 agents: no result", proc.stdout)
        self.assertNotIn("::error::1000 agents: no result", proc.stdout)

    def test_both_sizes_at_baseline_pass(self):
        proc = self.run_gate([result_for(1000), result_for(10000)])
        self.assertEqual(proc.returncode, 0, proc.stdout)
        self.assertNotIn("::error::", proc.stdout)

    def test_a_round_trip_that_is_not_exact_fails(self):
        bad = json.loads(result_for(10000))
        bad["round_trip"] = "MISMATCH"
        proc = self.run_gate([result_for(1000), json.dumps(bad)])
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn("round trip MISMATCH", proc.stdout)


if __name__ == "__main__":
    unittest.main()

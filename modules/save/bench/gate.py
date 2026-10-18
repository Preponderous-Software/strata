#!/usr/bin/env python3
"""Benchmark gate for the save core (RFC 0019 §9).

    gate.py <results.jsonl> <baseline.json>

Each results line is one strata-save-bench JSON object. The gate fails when
  - a §9 budget is exceeded (absolute, independent of the baseline), or
  - a metric regresses by more than 20% against the stored baseline for the
    same agent count (plus a small absolute slack, so sub-millisecond timings
    cannot fail on noise), or
  - a round trip is not EXACT.
To accept a deliberate change, update baseline.json in the same PR with the
numbers the CI run printed.
"""
import json
import sys

TOLERANCE = 0.20
# metric -> absolute slack added to the allowed value
GATED = {"save_ms": 5.0, "load_ms": 5.0, "async_stall_ms": 2.0, "delta_bytes": 1024, "peak_rss_mb": 8.0}
# agents -> metric -> budget (RFC 0019 §9)
BUDGETS = {
    10000: {"async_stall_ms": 50.0, "load_ms": 3000.0, "delta_bytes": 2 * 1024 * 1024, "peak_rss_mb": 500.0},
    1000: {"async_stall_ms": 50.0, "load_ms": 3000.0, "delta_bytes": 2 * 1024 * 1024, "peak_rss_mb": 500.0},
}


def main() -> int:
    results = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
    baseline = json.load(open(sys.argv[2]))
    failures = []
    for r in results:
        n = r["agents"]
        tag = f"{n} agents"
        if r.get("round_trip") != "EXACT":
            failures.append(f"{tag}: round trip {r.get('round_trip')}")
        for metric, budget in BUDGETS.get(n, {}).items():
            status = "ok" if r[metric] <= budget else "OVER BUDGET"
            print(f"{tag:>12} {metric:<15} {r[metric]:>12.2f}  budget {budget:>12.2f}  {status}")
            if r[metric] > budget:
                failures.append(f"{tag}: {metric} {r[metric]} > budget {budget}")
        base = baseline.get(str(n))
        if base is None:
            print(f"{tag:>12} (no stored baseline; budgets only)")
            continue
        for metric, slack in GATED.items():
            allowed = base[metric] * (1 + TOLERANCE) + slack
            status = "ok" if r[metric] <= allowed else "REGRESSED"
            print(f"{tag:>12} {metric:<15} {r[metric]:>12.2f}  baseline {base[metric]:>12.2f}  allowed {allowed:>12.2f}  {status}")
            if r[metric] > allowed:
                failures.append(f"{tag}: {metric} {r[metric]} > {allowed:.2f} (baseline {base[metric]} + 20%)")
    for f in failures:
        print(f"::error::{f}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

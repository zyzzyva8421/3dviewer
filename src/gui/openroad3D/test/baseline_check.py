#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
#
# baseline_check.py — Compare --dump-stats output against a stored baseline
#
# Usage:
#   1. Generate baseline:
#      ./openroad_3d --dump-stats 6_final.odb > baseline.json
#
#   2. Check against baseline:
#      ./openroad_3d --dump-stats 6_final.odb | python3 baseline_check.py baseline.json
#
# The script compares: object/instance/wire/via/pin counts, layer count,
# layer names and zBase values. Differences are reported with exit code 1.

import json
import sys


def load_stats(path_or_fp):
    if hasattr(path_or_fp, "read"):
        return json.load(path_or_fp)
    with open(path_or_fp) as f:
        return json.load(f)


def compare(key, got, expected, tolerance=1e-4):
    """Compare a scalar value, allowing float tolerance."""
    if isinstance(expected, (int, float)) and isinstance(got, (int, float)):
        if abs(got - expected) > tolerance:
            return f"  {key}: {got} != {expected} (diff={abs(got-expected):.4g})"
    elif got != expected:
        return f"  {key}: '{got}' != '{expected}'"
    return None


def check():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <baseline.json> [--update <stats.json>]")
        print()
        print("  Compare mode (default):")
        print("    {viewer} --dump-stats test.odb | {check} baseline.json")
        print()
        print("  Update mode:")
        print("    {viewer} --dump-stats test.odb > current.json")
        print("    {check} --update current.json baseline.json")
        sys.exit(1)

    update_mode = False
    if sys.argv[1] == "--update":
        update_mode = True
        src_path = sys.argv[2]
        dst_path = sys.argv[3]
    else:
        src_path = sys.argv[1]

    # Read baseline
    with open(src_path) as f:
        baseline = json.load(f)

    if update_mode:
        # Copy stats as new baseline
        with open(dst_path, "w") as f:
            json.dump(baseline, f, indent=2)
            f.write("\n")
        print(f"Updated baseline: {dst_path}")
        return

    # Compare mode — read stats from stdin
    stats = json.load(sys.stdin)

    errors = []

    # Compare top-level numeric/string fields
    for key in ["sourceTag", "layerCount", "objectCount",
                "instCount", "wireCount", "viaCount",
                "viaGroupCount", "pinCount"]:
        if key not in stats:
            errors.append(f"  missing key: {key}")
            continue
        err = compare(key, stats[key], baseline.get(key))
        if err:
            errors.append(err)

    # Compare layer list
    blayers = baseline.get("layers", [])
    slabers = stats.get("layers", [])
    if len(blayers) != len(slabers):
        errors.append(
            f"  layer count: {len(slabers)} != baseline {len(blayers)}")
    else:
        for i, (b, s) in enumerate(zip(blayers, slabers)):
            for field in ["name", "layerId", "zBase", "thickness", "visible"]:
                err = compare(f"layers[{i}].{field}",
                              s.get(field), b.get(field))
                if err:
                    errors.append(err)

    if errors:
        print("FAIL:")
        for e in errors:
            print(e)
        sys.exit(1)
    else:
        print("PASS: all fields match baseline")
        sys.exit(0)


if __name__ == "__main__":
    check()

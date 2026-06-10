#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025, The OpenROAD Authors
#
# Smoke test for OpenROAD 3D Viewer
# Runs the viewer headlessly with Xvfb and verifies it doesn't crash.
#
# Usage:
#   export OPENROAD_HOME=/path/to/OpenROAD-flow-scripts
#   export DISPLAY=:99
#   Xvfb :99 -screen 0 1280x1024x24 &
#   ./smoke_test.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY_DIR="${SCRIPT_DIR}/.."   # adjusted by CMake at test time
VIEWER="${OPENROAD_3D_BINARY:-${BINARY_DIR}/openroad_3d}"
ODB_FILE="${OPENROAD_3D_TEST_ODB}"

# Fallback ODB paths
if [ -z "${ODB_FILE}" ]; then
    for candidate in \
        "${OPENROAD_HOME}/flow/results/nangate45/gcd/base/6_final.odb" \
        "${OPENROAD_HOME}/flow/results/nangate45/gcd/base/5_route.odb" \
        ./6_final.odb; do
        if [ -f "$candidate" ]; then
            ODB_FILE="$candidate"
            break
        fi
    done
fi

echo "=== OpenROAD 3D Smoke Test ==="
echo "Viewer: ${VIEWER}"
echo "ODB:    ${ODB_FILE:-<none>}"
echo "Display: ${DISPLAY:-<unset>}"

# 1. Test --help
echo "--- Test: --help ---"
"${VIEWER}" --help 2>&1 | grep -q "Usage:" && echo "  PASS" || { echo "  FAIL"; exit 1; }

# 2. Test --dump-stats with ODB file (if available)
if [ -n "${ODB_FILE}" ] && [ -f "${ODB_FILE}" ]; then
    echo "--- Test: --dump-stats ---"
    STATS=$("${VIEWER}" --dump-stats "${ODB_FILE}" 2>/dev/null) || {
        echo "  FAIL: viewer exited with error"
        exit 1
    }
    # Verify JSON output contains expected fields
    echo "${STATS}" | python3 -c "
import json, sys
data = json.load(sys.stdin)
assert 'sourceTag' in data, 'missing sourceTag'
assert data['objectCount'] > 0, 'zero objects'
assert data['layerCount'] > 0, 'zero layers'
print(f'  PASS: {data[\"objectCount\"]} objects, {data[\"layerCount\"]} layers')
" || { echo "  FAIL: JSON validation error"; exit 1; }
else
    echo "--- Skip: --dump-stats (no ODB file) ---"
fi

# 3. Test GUI launch (requires Xvfb / display)
if [ -n "${DISPLAY}" ] && [ -n "${ODB_FILE}" ] && [ -f "${ODB_FILE}" ]; then
    echo "--- Test: GUI launch (5 sec) ---"
    # Launch viewer, wait 5 seconds, check it's still running
    "${VIEWER}" -odb "${ODB_FILE}" &
    VIEWER_PID=$!
    sleep 5
    if kill -0 "${VIEWER_PID}" 2>/dev/null; then
        echo "  PASS: viewer running after 5s"
        kill "${VIEWER_PID}" 2>/dev/null || true
        wait "${VIEWER_PID}" 2>/dev/null || true
    else
        wait "${VIEWER_PID}" 2>/dev/null || true
        echo "  FAIL: viewer exited prematurely (exit code: $?)"
        exit 1
    fi
else
    echo "--- Skip: GUI launch (no DISPLAY or ODB) ---"
fi

echo "=== All smoke tests PASS ==="

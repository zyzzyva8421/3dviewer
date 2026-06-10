# OpenROAD 3D Viewer — Regression Tests

## Prerequisites

- GTest installed (`find_package(GTest REQUIRED)` must succeed)
- An ODB test file, e.g.:
  ```
  export OPENROAD_3D_TEST_ODB=/path/to/6_final.odb
  ```

## Build & Run

```bash
# From the OpenROAD build directory:
cmake .. -DENABLE_OPENROAD3D_TESTS=ON -DOPENROAD_3D_TEST_ODB=/path/to/6_final.odb
make -j$(nproc)
ctest -R openroad3d
```

Or run directly:

```bash
./test_odb_scene_builder
```

## Test Files

| File | Description |
|------|-------------|
| `test_OdbSceneBuilder.cpp` | GTest unit tests for OdbSceneBuilder |
| `CMakeLists.txt` | CMake config for test target |
| `smoke_test.sh` | End-to-end smoke test (requires Xvfb) |
| `baseline_check.py` | Python script for baseline JSON comparison |

## Test Categories

### Data Layer (GTest, no GPU required)
- `NullDb` / `EmptyDbNoChip` — empty database produces empty snapshot
- `NonEmptySnapshot` — real ODB produces objects and layers
- `LayerZOrdering` — layers sorted by zBase ascending
- `AllObjectLayerIdsValid` — every object references a real layer
- `ViaTopBottomCutStructure` — each via group has 3 parts
- `InstancesHaveBbox` — all instances have valid bounding boxes
- `RepeatedBuildIsDeterministic` — two builds produce identical results
- `LayerColorsAreValid` — RGBA values in [0, 1] range

### End-to-End (Xvfb required)
- `smoke_test.sh` — launch viewer, verify it doesn't crash within 5s

### Baseline Comparison (--dump-stats)
```bash
# Generate baseline:
./openroad_3d --dump-stats 6_final.odb > baseline.json

# Compare:
./openroad_3d --dump-stats 6_final.odb | python3 test/baseline_check.py baseline.json

# Update baseline (after intentional changes):
./openroad_3d --dump-stats 6_final.odb | python3 test/baseline_check.py --update - baseline.json
```

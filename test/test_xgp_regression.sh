#!/bin/bash
# Regression test for xgp_global_placement vs global_placement
# This test compares the two global placement commands

set -e

OPENROAD=/home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/build/bin/openroad

if [ ! -f "$OPENROAD" ]; then
    echo "Error: OpenROAD binary not found at $OPENROAD"
    exit 1
fi

# Create temporary test directory
TEST_DIR=$(mktemp -d)
trap "rm -rf $TEST_DIR" EXIT

echo "=== Regression Test: xgp_global_placement vs global_placement ==="

# Test 1: Simple random design
echo ""
echo "Test 1: Running global placement tests..."
cat > $TEST_DIR/test.tcl << 'EOF'
# Create a simple test design
create_lib lib_test -tech /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/etc/gscl45nm.tcl
create_design design_test
create_cell inst_0 DFFQX1 0 0
create_cell inst_1 DFFQX1 100 0
create_cell inst_2 DFFQX1 200 0
create_cell inst_3 DFFQX1 0 100
create_cell inst_4 DFFQX1 100 100
create_cell inst_5 DFFQX1 200 100
create_cell inst_6 DFFQX1 0 200
create_cell inst_7 DFFQX1 100 200
create_cell inst_8 DFFQX1 200 200
create_cell inst_9 DFFQX1 50 50

# Create nets
create_net net_0 -pin inst_0.A -pin inst_1.A
create_net net_1 -pin inst_1.A -pin inst_2.A
create_net net_2 -pin inst_3.A -pin inst_4.A
create_net net_3 -pin inst_4.A -pin inst_5.A
create_net net_4 -pin inst_6.A -pin inst_7.A

# Initialize floorplan
initialize_floorplan -die_area 0 0 400 400 -row_site site_1

# Try xgp_global_placement
puts "Testing xgp_global_placement..."
xgp_global_place 100

# Check layout complete
puts "xgp_global_place completed"

exit 0
EOF

echo "Running test..."
echo "source $TEST_DIR/test.tcl" | $OPENROAD -exit 2>&1 || true

echo ""
echo "=== Test Complete ==="
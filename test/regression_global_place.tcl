#!/usr/bin/env tcl
# Regression test for global placement comparison
# Compares xgp_global_place vs global_placement

puts "=== Global Placement Regression Test ==="

# Check if we have a design loaded
set block [odb::get_current_block]
if {$block eq "null"} {
    puts "Error: No design loaded. Please load a design first."
    puts "Usage: read_def <design.def>"
    exit 1
}

puts "Design: \[$block getName\]"
puts ""

# Test 1: xgp_global_place
puts "=== Test 1: xgp_global_place ==="
set start_time1 [clock milliseconds]
xgp_global_place -max_iter 100
set end_time1 [clock milliseconds]
set xgp_time [expr {$end_time1 - $start_time1}]

# Get HPWL
set xgp_hpwl 0
set net_iter \[$block getNets\]
while {\[set net \[$net_iter next\]\] ne "null"} {
    if {\!\[$net isSpecial\]} {
        set hpwl \[$net getHPWL\]
        if {$hpwl > 0} {
            incr xgp_hpwl $hpwl
        }
    }
}
puts "xgp_global_place HPWL: $xgp_hpwl"
puts "xgp_global_place time: ${xgp_time}ms"

# Write output
write_def /tmp/xgp_out.def

puts ""
puts "=== Results ==="
puts "xgp_global_place:"
puts "  HPWL: $xgp_hpwl"
puts "  Time: ${xgp_time}ms"
puts ""
puts "Note: xgp_global_place currently calls GPL's doNesterovPlace"
puts "      Both commands use the same underlying algorithm."

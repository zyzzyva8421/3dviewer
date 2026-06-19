# xgp global placement test - minimal version
# Simple proxy - calls C++ after load
proc xgp_global_place { args } {
    puts "  [xgp::globalPlace]"
}

read_lef Nangate45/Nangate45.lef
read_liberty Nangate45/Nangate45_typ.lib

read_verilog gcd_nangate45.v
link_design gcd

initialize_floorplan \
  -site FreePDK45_38x28_10R_NP_162NW_34O \
  -die_area {0 0 100.13 100.8} \
  -core_area {10.07 11.2 90.25 91}

# Run xgp
puts "Running xgp_global_place..."
xgp_global_place -max_iter 100 -verbose

puts "Done"
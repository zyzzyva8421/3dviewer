# xgp vs gpl comparison test
# This script compares xgp and gpl global placement

source "helpers.tcl"
source "flow_helpers.tcl"
source "Nangate45/Nangate45.vars"

set design "aes"
set top_module "aes_cipher_top"
set synth_verilog "aes_nangate45.v"
set sdc_file "aes_nangate45.sdc"
set die_area {0 0 1020 920.8}
set core_area {10 12 1010 911.2}

set cap_margin 20

# Load design only up to floorplan
read_lef Nangate45/Nangate45.lef
read_liberty Nangate45/Nangate45_typ.lib
read_verilog $synth_verilog
link_design $top_module

initialize_floorplan \
  -site FreePDK45_38x28_10R_NP_162NW_34O \
  -die_area $die_area \
  -core_area $core_area

place_pins -random

# Write DEF for reference
write_def /tmp/${design}_fp.def

puts "\n===== XGP Global Placement ====="
xgp::globalPlace -max_iter 500 -verbose
set xgp_wl [sta::wireLength]
set xgp_ovf [xgp::overflow]

write_def /tmp/${design}_xgp.def
write_markers /tmp/${design}_xgp.marker

puts "\n===== GPL Global Placement ====="
global_placement -max_iter 500
set gpl_wl [sta::wireLength]
set gpl_ovf 0.0  ;# GPL doesn't have overflow API

write_def /tmp/${design}_gpl.def

puts "\n===== Comparison ====="
puts "XGP wirelength: $xgp_wl"
puts "XGP overflow: $xgp_ovf"
puts "GPL wirelength: $gpl_wl"
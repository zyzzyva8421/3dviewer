# GPL global placement test
source /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/test/Nangate45/Nangate45.vars

read_lef /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/test/Nangate45/Nangate45.lef
read_liberty /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/test/Nangate45/Nangate45_typ.lib

read_verilog /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/test/gcd_nangate45.v
link_design gcd

initialize_floorplan \
  -site $site \
  -die_area {0 0 100.13 100.8} \
  -core_area {10.07 11.2 90.25 91}

# Make routing tracks for pin placement
source /home/aliu/Desktop/OpenROAD-flow-scripts/tools/OpenROAD/test/Nangate45/Nangate45.tracks

# Place pins properly (need layers for Nangate45)
place_pins -hor_layers $io_placer_hor_layer -ver_layers $io_placer_ver_layer

puts "\n===== GPL Global Placement ====="
# Use -skip_initial_place to speed up, and limit with -overflow
global_placement -skip_initial_place -overflow 0.1
puts "GPL completed"

exit
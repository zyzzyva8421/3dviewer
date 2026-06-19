# xgp test on ariane133
# Use existing floorplan result, run global placement

cd /
source ../test/Nangate45/Nangate45.lef
source ../test/Nangate45/Nangate45_typ.lib

read_def flow/results/nangate45/ariane133/base/2_floorplan.odb

puts "Design loaded"

# Run xgp with macro-aware options  
puts "Running xgp_global_place..."
xgp_global_place -max_iter 50 -verbose

puts "Done"
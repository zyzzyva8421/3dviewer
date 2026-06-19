# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, xdp Authors

sta::define_cmd_args "xdp_detailed_placement" { \
    [-incremental] \
    [-verbose]}

proc xdp_detailed_placement { args } {
  sta::parse_key_args "xdp_detailed_placement" args \
    keys {} flags {-incremental -verbose}

  set incremental [info exists flags(-incremental)]
  set verbose     [info exists flags(-verbose)]

  xdp::xdp_detailed_place_cmd $incremental $verbose
}

sta::define_cmd_args "xdp_global_placement" { \
    [-max_iter max_iter] \
    [-verbose]}

proc xdp_global_placement { args } {
  sta::parse_key_args "xdp_global_placement" args \
    keys {-max_iter -verbose} flags {}

  if {[info exists keys(-max_iter)]} { set max_iter $keys(-max_iter) } else { set max_iter 1000 }
  set verbose [info exists flags(-verbose)]

  xdp::xdp_global_place_cmd $max_iter $verbose
}

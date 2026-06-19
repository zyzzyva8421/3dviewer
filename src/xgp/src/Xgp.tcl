# xgp global placement command

sta::define_cmd_args "xgp_global_place" { \
    [-max_iter max_iter] \
    [-td_update_period td_update_period] \
    [-td_calibrate_period td_calibrate_period] \
    [-td_calibrate_alpha td_calibrate_alpha] \
    [-td_formula td_formula] \
    [-td_weight_scale td_weight_scale] \
    [-td_weight_cap td_weight_cap] \
    [-td_net_cap td_net_cap] \
    [-td_ema_beta td_ema_beta] \
    [-td_rc_per_dbu td_rc_per_dbu] \
    [-td_wire_res_per_dbu td_wire_res_per_dbu] \
    [-td_wire_cap_per_dbu td_wire_cap_per_dbu] \
    [-td_sink_cap_base td_sink_cap_base] \
    [-td_fanout_cap_scale td_fanout_cap_scale] \
    [-td_fanout_delay_scale td_fanout_delay_scale] \
    [-td_base_net_mix td_base_net_mix] \
    [-td_timing_net_mix td_timing_net_mix] \
    [-td_clock_net_mix td_clock_net_mix] \
    [-td_use_clock_net_weight] \
    [-td_clock_net_weight td_clock_net_weight] \
    [-td_clock_net_fanout_threshold td_clock_net_fanout_threshold] \
    [-td_p2p_ratio_limit td_p2p_ratio_limit] \
    [-td_use_sta_net_slack_calibration td_use_sta_net_slack_calibration] \
    [-td_sta_net_sample_limit td_sta_net_sample_limit] \
    [-td_enable] \
    [-td_hybrid_enable] \
    [-verbose]}

proc xgp_global_place { args } {
  sta::parse_key_args "xgp_global_place" args \
    keys {-max_iter -td_update_period -td_calibrate_period -td_calibrate_alpha -td_formula -td_weight_scale -td_weight_cap -td_net_cap -td_ema_beta -td_rc_per_dbu -td_wire_res_per_dbu -td_wire_cap_per_dbu -td_sink_cap_base -td_fanout_cap_scale -td_fanout_delay_scale -td_base_net_mix -td_timing_net_mix -td_clock_net_mix -td_clock_net_weight -td_clock_net_fanout_threshold -td_p2p_ratio_limit -td_use_sta_net_slack_calibration -td_sta_net_sample_limit} flags {-td_enable -td_hybrid_enable -td_use_clock_net_weight -verbose}

  if {[info exists keys(-max_iter)]} { set max_iter $keys(-max_iter) } else { set max_iter 2000 }
  if {[info exists keys(-td_update_period)]} { set td_update_period $keys(-td_update_period) } else { set td_update_period 25 }
  if {[info exists keys(-td_calibrate_period)]} { set td_calibrate_period $keys(-td_calibrate_period) } else { set td_calibrate_period 100 }
  if {[info exists keys(-td_calibrate_alpha)]} { set td_calibrate_alpha $keys(-td_calibrate_alpha) } else { set td_calibrate_alpha 0.30 }
  if {[info exists keys(-td_formula)]} { set td_formula $keys(-td_formula) } else { set td_formula 0 }
  if {[info exists keys(-td_weight_scale)]} { set td_weight_scale $keys(-td_weight_scale) } else { set td_weight_scale 1.0 }
  if {[info exists keys(-td_weight_cap)]} { set td_weight_cap $keys(-td_weight_cap) } else { set td_weight_cap 16.0 }
  if {[info exists keys(-td_net_cap)]} { set td_net_cap $keys(-td_net_cap) } else { set td_net_cap 32.0 }
  if {[info exists keys(-td_ema_beta)]} { set td_ema_beta $keys(-td_ema_beta) } else { set td_ema_beta 0.30 }
  if {[info exists keys(-td_rc_per_dbu)]} { set td_rc_per_dbu $keys(-td_rc_per_dbu) } else { set td_rc_per_dbu 1e-4 }
  if {[info exists keys(-td_wire_res_per_dbu)]} { set td_wire_res_per_dbu $keys(-td_wire_res_per_dbu) } else { set td_wire_res_per_dbu 1e-5 }
  if {[info exists keys(-td_wire_cap_per_dbu)]} { set td_wire_cap_per_dbu $keys(-td_wire_cap_per_dbu) } else { set td_wire_cap_per_dbu 1e-5 }
  if {[info exists keys(-td_sink_cap_base)]} { set td_sink_cap_base $keys(-td_sink_cap_base) } else { set td_sink_cap_base 1.0 }
  if {[info exists keys(-td_fanout_cap_scale)]} { set td_fanout_cap_scale $keys(-td_fanout_cap_scale) } else { set td_fanout_cap_scale 0.05 }
  if {[info exists keys(-td_fanout_delay_scale)]} { set td_fanout_delay_scale $keys(-td_fanout_delay_scale) } else { set td_fanout_delay_scale 1e-2 }
  if {[info exists keys(-td_base_net_mix)]} { set td_base_net_mix $keys(-td_base_net_mix) } else { set td_base_net_mix 0.55 }
  if {[info exists keys(-td_timing_net_mix)]} { set td_timing_net_mix $keys(-td_timing_net_mix) } else { set td_timing_net_mix 0.35 }
  if {[info exists keys(-td_clock_net_mix)]} { set td_clock_net_mix $keys(-td_clock_net_mix) } else { set td_clock_net_mix 0.10 }
  if {[info exists keys(-td_clock_net_weight)]} { set td_clock_net_weight $keys(-td_clock_net_weight) } else { set td_clock_net_weight 1.5 }
  if {[info exists keys(-td_clock_net_fanout_threshold)]} { set td_clock_net_fanout_threshold $keys(-td_clock_net_fanout_threshold) } else { set td_clock_net_fanout_threshold 32 }
  if {[info exists keys(-td_p2p_ratio_limit)]} { set td_p2p_ratio_limit $keys(-td_p2p_ratio_limit) } else { set td_p2p_ratio_limit 0.20 }
  if {[info exists keys(-td_use_sta_net_slack_calibration)]} { set td_use_sta_net_slack_calibration $keys(-td_use_sta_net_slack_calibration) } else { set td_use_sta_net_slack_calibration 1 }
  if {[info exists keys(-td_sta_net_sample_limit)]} { set td_sta_net_sample_limit $keys(-td_sta_net_sample_limit) } else { set td_sta_net_sample_limit 5000 }
  set td_enable [info exists flags(-td_enable)]
  set td_hybrid_enable [info exists flags(-td_hybrid_enable)]
  set td_use_clock_net_weight [info exists flags(-td_use_clock_net_weight)]
  set verbose [info exists flags(-verbose)]

  xgp::xgp_global_place_cmd $max_iter $verbose $td_enable $td_update_period $td_formula $td_weight_scale $td_hybrid_enable $td_calibrate_period $td_calibrate_alpha $td_weight_cap $td_net_cap $td_ema_beta $td_rc_per_dbu $td_wire_res_per_dbu $td_wire_cap_per_dbu $td_sink_cap_base $td_fanout_cap_scale $td_fanout_delay_scale $td_base_net_mix $td_timing_net_mix $td_clock_net_mix $td_use_clock_net_weight $td_clock_net_weight $td_clock_net_fanout_threshold $td_p2p_ratio_limit $td_use_sta_net_slack_calibration $td_sta_net_sample_limit
}

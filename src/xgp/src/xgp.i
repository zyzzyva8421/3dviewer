// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// xgp.i — SWIG interface for xgp module.

%module xgp
%{
#include "ord/OpenRoad.hh"
#include "Xgp.h"
#include "GpPlacer.h"
%}

%inline %{

void
xgp_global_place_cmd(int max_iter,
                     bool verbose,
                     bool td_enable,
                     int td_update_period,
                     int td_formula,
                     double td_weight_scale,
                     bool td_hybrid_enable,
                     int td_calibrate_period,
                     double td_calibrate_alpha,
                     double td_weight_cap,
                     double td_net_cap,
                     double td_ema_beta,
                     double td_rc_per_dbu,
                     double td_wire_res_per_dbu,
                     double td_wire_cap_per_dbu,
                     double td_sink_cap_base,
                     double td_fanout_cap_scale,
                     double td_fanout_delay_scale,
                     double td_base_net_mix,
                     double td_timing_net_mix,
                     double td_clock_net_mix,
                     bool td_use_clock_net_weight,
                     double td_clock_net_weight,
                     int td_clock_net_fanout_threshold,
                     double td_p2p_ratio_limit,
                     bool td_use_sta_net_slack_calibration,
                     int td_sta_net_sample_limit)
{
  xgp::Xgp xgp_placer;
  xgp_placer.globalPlace(max_iter,
                         verbose,
                         td_enable,
                         td_update_period,
                         td_formula,
                         td_weight_scale,
                         td_hybrid_enable,
                         td_calibrate_period,
                         td_calibrate_alpha,
                         td_weight_cap,
                         td_net_cap,
                         td_ema_beta,
                         td_rc_per_dbu,
                         td_wire_res_per_dbu,
                         td_wire_cap_per_dbu,
                         td_sink_cap_base,
                         td_fanout_cap_scale,
                         td_fanout_delay_scale,
                         td_base_net_mix,
                         td_timing_net_mix,
                         td_clock_net_mix,
                         td_use_clock_net_weight,
                         td_clock_net_weight,
                         td_clock_net_fanout_threshold,
                         td_p2p_ratio_limit,
                         td_use_sta_net_slack_calibration,
                         td_sta_net_sample_limit);
}

%}

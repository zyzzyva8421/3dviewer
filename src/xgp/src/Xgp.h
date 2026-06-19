// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// Xgp.h — xgp command declarations.

#pragma once

namespace xgp {

class Xgp {
 public:
  static void globalPlace(int max_iter = 2000,
                          bool verbose = false,
                          bool td_enable = false,
                          int td_update_period = 25,
                          int td_formula = 0,
                          double td_weight_scale = 1.0,
                          bool td_hybrid_enable = false,
                          int td_calibrate_period = 100,
                          double td_calibrate_alpha = 0.30,
                          double td_weight_cap = 16.0,
                          double td_net_cap = 32.0,
                          double td_ema_beta = 0.30,
                          double td_rc_per_dbu = 1e-4,
                          double td_wire_res_per_dbu = 1e-5,
                          double td_wire_cap_per_dbu = 1e-5,
                          double td_sink_cap_base = 1.0,
                          double td_fanout_cap_scale = 0.05,
                          double td_fanout_delay_scale = 1e-2,
                          double td_base_net_mix = 0.55,
                          double td_timing_net_mix = 0.35,
                          double td_clock_net_mix = 0.10,
                          bool td_use_clock_net_weight = true,
                          double td_clock_net_weight = 1.5,
                          int td_clock_net_fanout_threshold = 32,
                          double td_p2p_ratio_limit = 0.20,
                          bool td_use_sta_net_slack_calibration = true,
                          int td_sta_net_sample_limit = 5000);
};

}  // namespace xgp
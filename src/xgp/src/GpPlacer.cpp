// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpPlacer.cpp — Placement orchestration implementation.

#include "GpPlacer.h"
#include "GpSolver.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <iostream>
#include <cstdarg>

namespace xgp {

// ── run ────────────────────────────────────────────────────────────────────────
void GpPlacer::run(const PlacerOptions& opts) {
  // Fill database
  fillGpDb();

  // Initialize grid
  initGrid(opts);

  // Create costs
  WLCost wl(&db_);
  DenCost den(&db_);
  wl.setWeight(opts.wl_weight);
  den.setWeight(opts.den_weight);
  wl.setGamma(opts.wl_gamma);
  den.setTarget(opts.init_density);
  den.setSmooth(true);  // Use Bell-shaped density gradients

  // Create solver
  solver_ = std::make_unique<NesterovSolver>(db_, wl, den);

  // Build lightweight timing-driven configuration.
  TimingDrivenOptions td;
  td.enable = opts.td_enable;
  td.start_iter = opts.td_start_iter;
  td.update_period = opts.td_update_period;
  td.hybrid_enable = opts.td_hybrid_enable;
  td.calibrate_period = opts.td_calibrate_period;
  td.calibrate_alpha = opts.td_calibrate_alpha;
  td.weight_formula = opts.td_formula;
  td.weight_scale = opts.td_weight_scale;
  td.weight_cap = opts.td_weight_cap;
  td.net_weight_cap = opts.td_net_cap;
  td.ema_beta = opts.td_ema_beta;
  td.rc_per_dbu = opts.td_rc_per_dbu;
  td.wire_res_per_dbu = opts.td_wire_res_per_dbu;
  td.wire_cap_per_dbu = opts.td_wire_cap_per_dbu;
  td.sink_cap_base = opts.td_sink_cap_base;
  td.fanout_cap_scale = opts.td_fanout_cap_scale;
  td.fanout_delay_scale = opts.td_fanout_delay_scale;
  td.base_net_mix = opts.td_base_net_mix;
  td.timing_net_mix = opts.td_timing_net_mix;
  td.clock_net_mix = opts.td_clock_net_mix;
  td.use_clock_net_weight = opts.td_use_clock_net_weight;
  td.clock_net_weight = opts.td_clock_net_weight;
  td.clock_net_fanout_threshold = opts.td_clock_net_fanout_threshold;
  td.p2p_ratio_limit = opts.td_p2p_ratio_limit;
  td.use_sta_net_slack_calibration = opts.td_use_sta_net_slack_calibration;
  td.sta_net_sample_limit = opts.td_sta_net_sample_limit;
  td.verbose = opts.verbose;
  td.sta = opts.td_sta;
  solver_->setTimingOptions(td);

  // Run solver
  ConvergenceCriteria crit;
  crit.max_iter = opts.max_iter;
  crit.min_iter = opts.min_iter;
  crit.target_overflow = opts.target_overflow;
  solver_->solve(crit);
  last_overflow_ = solver_->overflow();
  last_raw_overflow_ = solver_->rawOverflow();
  solver_.reset();

  std::cout << "GP done: overflow(norm)=" << last_overflow_
            << " overflow(raw)=" << last_raw_overflow_ << "\n";
}

// ── fillGpDb ──────────────────────────────────────────────────────────────
// Fills GP DB from ODB - simplified for now
void GpPlacer::fillGpDb() {
  // Already filled by Xgp
}

// ── initGrid ──────────────────────────────────────────────────────────
void GpPlacer::initGrid(const PlacerOptions& opts) {
  Box die = db_.die;
  const Flt target = opts.init_density;

  // Auto-compute bin count
  ID2 bx = opts.bin_cnt_x;
  ID2 by = opts.bin_cnt_y;

  if (bx == 0 || by == 0) {
    // Estimate from movable area
    Flt mov_area = 0.0;
    for (const auto& inst : db_.insts_) {
      if (!inst.fixed) mov_area += inst.w * inst.h;
    }
    Flt bin_area = mov_area / target;
    Flt side = std::sqrt(bin_area);
    bx = (ID2)std::ceil(die.width() / side);
    by = (ID2)std::ceil(die.height() / side);
    // Power of 2 (safe for small values)
    bx = std::max(1, bx);
    by = std::max(1, by);
    bx = 1 << (31 - __builtin_clz(bx));
    by = 1 << (31 - __builtin_clz(by));
  }

  // Create grid
  Pt step{die.width() / (Flt)bx, die.height() / (Flt)by};
  Pt origin{die.lo.x, die.lo.y};
  db_.grid_ = Grid(bx, by, step, origin);

  // Set bin targets
  for (auto& bin : db_.grid_.bins) {
    bin.target = bin.box.area() * target;
    bin.area = 0.0;
  }
}

}  // namespace xgp
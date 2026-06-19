// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpPlacer.h — Placement orchestration.
// Mirrors GP placer orchestration from original placement.

#pragma once

#include "GpDb.h"
#include "GpCost.h"
#include "GpSolver.h"
#include <string>
#include <memory>

namespace sta {
class dbSta;
}

namespace xgp {

// ── Placer Options ────────────────────────────────────────────────────────
struct PlacerOptions {
  int  max_iter = 2000;
  int  min_iter = 20;
  Flt  target_overflow = 0.10;
  Flt  init_density = 0.70;
  Flt  wl_weight = 1.0;
  Flt  den_weight = 1.0;
  Flt  wl_gamma = 1.0;
  int  bin_cnt_x = 0;   // 0 = auto
  int  bin_cnt_y = 0;
  bool verbose = false;

  // Lightweight timing-driven options (hello-like spirit).
  bool td_enable = false;
  int td_start_iter = 0;
  int td_update_period = 25;
  bool td_hybrid_enable = false;
  int td_calibrate_period = 100;
  Flt td_calibrate_alpha = 0.30;
  int td_formula = 0;
  Flt td_weight_scale = 1.0;
  Flt td_weight_cap = 16.0;
  Flt td_net_cap = 32.0;
  Flt td_ema_beta = 0.30;
  Flt td_rc_per_dbu = 1e-4;
  Flt td_wire_res_per_dbu = 1e-5;
  Flt td_wire_cap_per_dbu = 1e-5;
  Flt td_sink_cap_base = 1.0;
  Flt td_fanout_cap_scale = 0.05;
  Flt td_fanout_delay_scale = 1e-2;
  Flt td_base_net_mix = 0.55;
  Flt td_timing_net_mix = 0.35;
  Flt td_clock_net_mix = 0.10;
  bool td_use_clock_net_weight = true;
  Flt td_clock_net_weight = 1.5;
  int td_clock_net_fanout_threshold = 32;
  Flt td_p2p_ratio_limit = 0.20;
  bool td_use_sta_net_slack_calibration = true;
  int td_sta_net_sample_limit = 5000;
  sta::dbSta* td_sta = nullptr;
};

// ── GP Placer ────────────────────────────────────────────────────────────────
// Orchestrates: DB → Grid → Cost → Solver → Write-back
class GpPlacer {
 public:
  GpPlacer(DB& db) : db_(db) {}

  // Run global placement
  void run(const PlacerOptions& opts);

  // Accessors
  DB& db() { return db_; }
  Flt overflow() const { return last_overflow_; }
  Flt rawOverflow() const { return last_raw_overflow_; }

 private:
  DB& db_;
  std::unique_ptr<NesterovSolver> solver_;
  Flt last_overflow_ = 0.0;
  Flt last_raw_overflow_ = 0.0;

  // Initialize bin grid
  void initGrid(const PlacerOptions& opts);

  // Fill GP DB from ODB
  void fillGpDb();

  // Log utility
  void log(const char* msg, ...) {}
};

}  // namespace xgp
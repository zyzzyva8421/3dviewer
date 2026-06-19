// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpSolver.h — Nesterov solver for xgp.
// Mirrors original Nesterov solver from placement.

#pragma once

#include "GpCost.h"
#include "GpDb.h"

#include <array>
#include <functional>
#include <unordered_set>

namespace sta {
class dbSta;
}

namespace xgp {

// ── Solver Parameters ─────────────────────────────────────────────────────────────
// Matches OpenROAD global_placement parameters
struct SolverParams {
  Flt target_density = 0.70;        // -density
  Flt init_density_penalty = 0.0;    // -init_density_penalty
  Flt init_wirelength_coef = 0.0;   // -init_wirelength_coef
  Flt min_phi_coef = 0.0;           // -min_phi_coef
  Flt max_phi_coef = 1.0;           // -max_phi_coef
  Flt reference_hpwl = 0.0;      // -reference_hpwl
  int max_iter = 2000;
  int min_iter = 20;
};

// ── Convergence Criteria ─────────────────────────────────────────────────────────
struct ConvergenceCriteria {
  Flt target_overflow = 0.10;
  int max_iter = 2000;
  int min_iter = 20;
};

// ── Solver State ────────────────────────────────────────────────────────────────────────────
struct SolverState {
  int iter = 0;
  bool converged = false;
};

// Lightweight timing-driven options.
//
// This configuration intentionally mirrors the spirit of hello GP:
// 1) periodic timing refresh (not every micro-operation);
// 2) sink-pin based P2P weights;
// 3) selectable slack->weight formula.
struct TimingDrivenOptions {
  bool enable = false;
  int start_iter = 0;
  int update_period = 25;
  bool hybrid_enable = false;
  int calibrate_period = 100;
  Flt calibrate_alpha = 0.30;
  int weight_formula = 0;   // 0..3, aligned with hello formulas.
  Flt weight_scale = 1.0;
  Flt weight_cap = 16.0;
  Flt net_weight_cap = 32.0;
  Flt ema_beta = 0.30;
  Flt rc_per_dbu = 1e-4;    // Very cheap virtual RC slope for delay proxy.
  // RC/load-aware proxy delay parameters.
  Flt wire_res_per_dbu = 1e-5;
  Flt wire_cap_per_dbu = 1e-5;
  Flt sink_cap_base = 1.0;
  Flt fanout_cap_scale = 0.05;
  Flt fanout_delay_scale = 1e-2;
  Flt default_net_weight = 2.0;
  Flt base_net_mix = 0.55;
  Flt timing_net_mix = 0.35;
  Flt clock_net_mix = 0.10;
  bool use_clock_net_weight = true;
  Flt clock_net_weight = 1.5;
  int clock_net_fanout_threshold = 32;
  Flt p2p_ratio_limit = 0.20;
  bool verbose = false;
  sta::dbSta* sta = nullptr;

  // Optional per-net STA slack calibration using fanout buckets.
  bool use_sta_net_slack_calibration = true;
  int sta_net_sample_limit = 5000;
};

// ── Nesterov Solver ────────────────────────────────────────────────────────────────
class NesterovSolver {
 public:
  NesterovSolver(DB& db, WLCost& wl, DenCost& den)
      : db_(db), wl_(wl), den_(den) {}

  // Set parameters (matches OpenROAD global_placement)
  void setParams(const SolverParams& p) {
    params_ = p;
    // Phi coefficient starts at max, decreases during annealing
    phi_coef_ = p.max_phi_coef;
    ref_hpwl_ = p.reference_hpwl;
  }

  void setTimingOptions(const TimingDrivenOptions& td) { td_opt_ = td; }

  using ProgressCb = std::function<void(int, Flt, Flt)>;
  void solve(const ConvergenceCriteria& crit, ProgressCb progress_cb = nullptr);

  // Return normalized overflow evaluated on the final committed placement.
  Flt overflow() const { return final_norm_overflow_; }
  // Raw overflow (sum of overflowed bin area in DBU^2) on final placement.
  Flt rawOverflow() const { return final_raw_overflow_; }
  Flt hpwl() const { return wl_.hpwl(); }
  Flt phiCoef() const { return phi_coef_; }

 private:
  DB& db_;
  WLCost& wl_;
  DenCost& den_;

  SolverParams params_;
  Flt phi_coef_ = 1.0;
  Flt ref_hpwl_ = 0.0;

  // Double buffers for Nesterov's current/previous coordinates.
  ID cur_buf_ = 0;
  ID prev_buf_ = 1;
  PtVec coords_[2];
  PtVec ls_coords_[2];
  PtVec grads_[2];

  Flt curA_ = 1.0;
  Flt coeff_ = 0.0;
  Flt last_step_norm_ = 0.0;
  Flt last_grad_norm_ = 0.0;

  SolverState state_;
  TimingDrivenOptions td_opt_;

  // Cache net -> pin list once, then reuse in all timing updates.
  std::vector<std::vector<ID>> net_pin_ids_;
  std::vector<ID> net_driver_pin_id_;
  std::vector<Flt> base_net_weights_;
  std::vector<Flt> timing_net_weights_;
  std::vector<Flt> clock_net_weights_;
  std::vector<Flt> net_weights_;
  std::vector<Flt> p2p_pin_weights_;
  bool td_cache_ready_ = false;
  bool clock_net_cache_ready_ = false;
  std::unordered_set<odb::dbNet*> clock_nets_;
  Flt td_calib_scale_ = 1.0;
  std::array<Flt, 4> td_bucket_scales_{{1.0, 1.0, 1.0, 1.0}};
  Flt final_raw_overflow_ = 0.0;
  Flt final_norm_overflow_ = 0.0;

  void initBuffers();
  void computeGrads(int buf);
  Flt computeCost(int buf);
  void applyBoundaryClamp(PtVec& coords);
  Pt nesterovStep();
  void updateNesterovMomentum();
  void commitCoords();

  // Timing-driven helpers (lightweight internal timer path).
  void initTimingDrivenCache();
  void initClockNetCache();
  void updateTimingDrivenWeights(int iter);
  void maybeCalibrateWithSta(int iter, Flt proxy_wns);
  int fanoutBucket(int fanout) const;
  Flt estimateProxySlack(const Pt& drv_pt, const Pt& sink_pt, int fanout) const;
  Flt evalWeightFormula(Flt slack, Flt slack_ref, Flt wns) const;

};

}  // namespace xgp
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors

#include "GpSolver.h"

#include "db_sta/dbSta.hh"
#include "sta/MinMax.hh"
#include "sta/Sta.hh"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace xgp {

static Flt vecNorm(const PtVec& vec)
{
  Flt sum = 0.0;
  for (const Pt& pt : vec) {
    sum += pt.x * pt.x + pt.y * pt.y;
  }
  return std::sqrt(sum);
}

static Pt vecDist(const PtVec& lhs, const PtVec& rhs)
{
  assert(lhs.size() == rhs.size());
  Pt dist{0.0, 0.0};
  for (size_t i = 0; i < lhs.size(); ++i) {
    const Pt delta = lhs[i] - rhs[i];
    dist.x += delta.x * delta.x;
    dist.y += delta.y * delta.y;
  }
  return dist.sqrt();
}

// ── initBuffers ───────────────────────────────────────────────────────────────
void NesterovSolver::initBuffers()
{
  cur_buf_ = 0;
  prev_buf_ = 1;
  const ID n = db_.instCnt();
  const PtVec init_coords = db_.coords();

  for (int b = 0; b < 2; ++b) {
    coords_[b].assign(n, Pt{});
    ls_coords_[b].assign(n, Pt{});
    grads_[b].assign(n, Pt{});
  }

  for (ID i = 0; i < n; ++i) {
    coords_[cur_buf_][i] = init_coords[i];
    ls_coords_[cur_buf_][i] = init_coords[i];
  }

  // Initialize previous buffer by a tiny gradient step, matching the standard
  // Nesterov warm-start pattern used in placement solvers.
  coords_[prev_buf_] = coords_[cur_buf_];
  ls_coords_[prev_buf_] = ls_coords_[cur_buf_];

  computeGrads(cur_buf_);
  grads_[prev_buf_] = grads_[cur_buf_];

  const Flt init_step = 0.001;
  for (ID i = 0; i < n; ++i) {
    coords_[prev_buf_][i].x = coords_[cur_buf_][i].x - init_step * grads_[cur_buf_][i].x;
    coords_[prev_buf_][i].y = coords_[cur_buf_][i].y - init_step * grads_[cur_buf_][i].y;
    ls_coords_[prev_buf_][i] = coords_[prev_buf_][i];
  }
  computeGrads(prev_buf_);

  curA_ = 1.0;
  coeff_ = 0.0;
  last_step_norm_ = 0.0;
  last_grad_norm_ = 0.0;
}

// ── computeGrads ──────────────────────────────────────────────────────────────
void NesterovSolver::computeGrads(int buf)
{
  std::fill(grads_[buf].begin(), grads_[buf].end(), Pt{});
  den_(coords_[buf]);
  den_(coords_[buf], grads_[buf]);
  wl_(coords_[buf], grads_[buf]);
}

// ── computeCost ───────────────────────────────────────────────────────────────
Flt NesterovSolver::computeCost(int buf)
{
  return den_(ls_coords_[buf]) + wl_(ls_coords_[buf]);
}

// ── applyBoundaryClamp ────────────────────────────────────────────────────────
void NesterovSolver::applyBoundaryClamp(PtVec& coords)
{
  const Box& die = db_.die;
  for (ID i = 0; i < db_.instCnt(); ++i) {
    const Inst& inst = db_.inst(i);
    if (inst.fixed) {
      continue;
    }

    // Clamp by instance half-size to keep the full cell inside die area.
    const Flt hw = inst.w * 0.5;
    const Flt hh = inst.h * 0.5;
    coords[i].x = std::max(die.lo.x + hw, std::min(die.hi.x - hw, coords[i].x));
    coords[i].y = std::max(die.lo.y + hh, std::min(die.hi.y - hh, coords[i].y));
  }
}

// ── nesterovStep ──────────────────────────────────────────────────────────────
Pt NesterovSolver::nesterovStep()
{
  const ID n = db_.instCnt();
  // Keep a non-trivial minimum step in DBU scale.
  // A fixed tiny value (e.g. 1e-3) is effectively zero for large designs,
  // causing almost no coordinate update. We tie the floor to bin pitch.
  const Flt grid_pitch = std::min(db_.grid().step.x, db_.grid().step.y);
  const Flt min_step = std::max<Flt>(1.0, 0.001 * grid_pitch);

  const Pt dist_ls = vecDist(ls_coords_[cur_buf_], ls_coords_[prev_buf_]);
  const Pt dist_g = vecDist(grads_[cur_buf_], grads_[prev_buf_]);
  Pt length{
      dist_g.x > 1e-20 ? 0.001 * dist_ls.x / dist_g.x : 0.0,
      dist_g.y > 1e-20 ? 0.001 * dist_ls.y / dist_g.y : 0.0,
  };
  if (length.x < min_step || !std::isfinite(length.x)) {
    length.x = min_step;
  }
  if (length.y < min_step || !std::isfinite(length.y)) {
    length.y = min_step;
  }
  last_step_norm_ = length.norm();

  for (ID i = 0; i < n; ++i) {
    const Inst& inst = db_.inst(i);
    if (inst.fixed) {
      continue;
    }

    coords_[cur_buf_][i].x = ls_coords_[prev_buf_][i].x - length.x * grads_[prev_buf_][i].x;
    coords_[cur_buf_][i].y = ls_coords_[prev_buf_][i].y - length.y * grads_[prev_buf_][i].y;

    ls_coords_[cur_buf_][i].x = coords_[cur_buf_][i].x + coeff_ * (coords_[cur_buf_][i].x - coords_[prev_buf_][i].x);
    ls_coords_[cur_buf_][i].y = coords_[cur_buf_][i].y + coeff_ * (coords_[cur_buf_][i].y - coords_[prev_buf_][i].y);
  }
  applyBoundaryClamp(ls_coords_[cur_buf_]);

  const Flt epsilon = 0.95;
  const int max_backtrack = 300;
  for (int bt = 0; bt < max_backtrack; ++bt) {
    computeGrads(cur_buf_);

    const Pt new_dist_ls = vecDist(ls_coords_[cur_buf_], ls_coords_[prev_buf_]);
    const Pt new_dist_g = vecDist(grads_[cur_buf_], grads_[prev_buf_]);
    const Pt new_length{
        new_dist_g.x > 1e-20 ? 0.001 * new_dist_ls.x / new_dist_g.x : 0.0,
        new_dist_g.y > 1e-20 ? 0.001 * new_dist_ls.y / new_dist_g.y : 0.0,
    };
    Pt safe_new_length = new_length;
    if (safe_new_length.x < min_step || !std::isfinite(safe_new_length.x)) {
      safe_new_length.x = min_step;
    }
    if (safe_new_length.y < min_step || !std::isfinite(safe_new_length.y)) {
      safe_new_length.y = min_step;
    }

    if (safe_new_length.norm() * epsilon > length.norm()) {
      break;
    }
    if (std::abs(safe_new_length.norm() - length.norm()) < 1e-7) {
      break;
    }

    length = safe_new_length;
    last_step_norm_ = length.norm();

    for (ID i = 0; i < n; ++i) {
      const Inst& inst = db_.inst(i);
      if (inst.fixed) {
        continue;
      }
      coords_[cur_buf_][i].x = ls_coords_[prev_buf_][i].x - length.x * grads_[prev_buf_][i].x;
      coords_[cur_buf_][i].y = ls_coords_[prev_buf_][i].y - length.y * grads_[prev_buf_][i].y;
      ls_coords_[cur_buf_][i].x = coords_[cur_buf_][i].x + coeff_ * (coords_[cur_buf_][i].x - coords_[prev_buf_][i].x);
      ls_coords_[cur_buf_][i].y = coords_[cur_buf_][i].y + coeff_ * (coords_[cur_buf_][i].y - coords_[prev_buf_][i].y);
    }
    applyBoundaryClamp(ls_coords_[cur_buf_]);
  }

  return length;
}

// ── updateNesterovMomentum ────────────────────────────────────────────────────
void NesterovSolver::updateNesterovMomentum()
{
  Flt prevA = curA_;
  curA_ = (1.0 + std::sqrt(4.0 * prevA * prevA + 1.0)) * 0.5;
  coeff_ = (prevA - 1.0) / curA_;
}

// ── commitCoords ──────────────────────────────────────────────────────────────
void NesterovSolver::commitCoords()
{
  db_.setCoords(coords_[prev_buf_]);
}

// ── timing helpers ────────────────────────────────────────────────────────────
void NesterovSolver::initTimingDrivenCache()
{
  if (td_cache_ready_) {
    return;
  }

  const ID net_cnt = db_.netCnt();
  net_pin_ids_.assign(net_cnt, {});
  net_driver_pin_id_.assign(net_cnt, -1);

  for (ID pid = 0; pid < db_.pinCnt(); ++pid) {
    const Pin& pin = db_.pins_[pid];
    if (pin.net_id < 0 || pin.net_id >= net_cnt) {
      continue;
    }
    if (pin.inst_id < 0 || pin.inst_id >= db_.instCnt()) {
      continue;
    }
    net_pin_ids_[pin.net_id].push_back(pid);
  }

  for (ID net_id = 0; net_id < net_cnt; ++net_id) {
    const auto& pins = net_pin_ids_[net_id];
    ID driver = -1;

    // Prefer output pin as the driver. If net has no explicit output pin,
    // fallback to first pin to keep the model robust.
    for (ID pid : pins) {
      if (!db_.pins_[pid].isInput) {
        driver = pid;
        break;
      }
    }
    if (driver < 0 && !pins.empty()) {
      driver = pins.front();
    }
    net_driver_pin_id_[net_id] = driver;
  }

  base_net_weights_.assign(db_.netCnt(), td_opt_.default_net_weight);
  timing_net_weights_.assign(db_.netCnt(), 1.0);
  clock_net_weights_.assign(db_.netCnt(), 1.0);
  net_weights_.assign(db_.netCnt(), td_opt_.default_net_weight);
  p2p_pin_weights_.assign(db_.pinCnt(), 1.0);
  td_cache_ready_ = true;
}

void NesterovSolver::initClockNetCache()
{
  if (clock_net_cache_ready_) {
    return;
  }
  clock_nets_.clear();
  if (td_opt_.sta != nullptr && td_opt_.use_clock_net_weight) {
    for (auto* clk_net : td_opt_.sta->findClkNets()) {
      if (clk_net != nullptr) {
        clock_nets_.insert(clk_net);
      }
    }
  }
  clock_net_cache_ready_ = true;
}

Flt NesterovSolver::evalWeightFormula(Flt slack, Flt slack_ref, Flt wns) const
{
  // Formula semantics mirror hello's timing-driven net weighting.
  // Inputs are usually negative for violating endpoints.
  switch (td_opt_.weight_formula) {
    case 1:
      return 2.0 / (1.0 + std::pow(slack_ref / slack, 2.0));
    case 2:
      return (-slack / std::sqrt(slack * slack + wns * wns)) * 2.0;
    case 3:
      return (slack / wns) * 2.0;
    default:
      return (slack * slack / (wns * wns)) * 2.0;
  }
}

int NesterovSolver::fanoutBucket(int fanout) const
{
  if (fanout <= 4) {
    return 0;
  }
  if (fanout <= 16) {
    return 1;
  }
  if (fanout <= 64) {
    return 2;
  }
  return 3;
}

Flt NesterovSolver::estimateProxySlack(const Pt& drv_pt,
                                       const Pt& sink_pt,
                                       int fanout) const
{
  const Flt l1 = std::abs(drv_pt.x - sink_pt.x) + std::abs(drv_pt.y - sink_pt.y);

  // RC/load-aware proxy delay model:
  //   delay ~= R*L*(0.5*Cw*L + Cload)
  // with lightweight fanout-dependent penalties.
  const Flt wire_r = td_opt_.wire_res_per_dbu * l1;
  const Flt wire_c = td_opt_.wire_cap_per_dbu * l1;
  const Flt load_c = td_opt_.sink_cap_base + td_opt_.fanout_cap_scale * std::max(0, fanout - 1);
  const Flt elmore = wire_r * (0.5 * wire_c + load_c);
  const Flt delay = td_opt_.rc_per_dbu * l1 + elmore + td_opt_.fanout_delay_scale * std::log1p(std::max(0, fanout - 1));

  return -delay;
}

void NesterovSolver::updateTimingDrivenWeights(int iter)
{
  if (!td_opt_.enable) {
    return;
  }
  if (iter < td_opt_.start_iter) {
    return;
  }
  if (td_opt_.update_period <= 0) {
    return;
  }
  if (iter % td_opt_.update_period != 0) {
    return;
  }

  initTimingDrivenCache();
  initClockNetCache();

  // Base all pins to default net weight first.
  std::vector<Flt> raw_pin_weights(db_.pinCnt(), td_opt_.default_net_weight);
  std::vector<Flt> raw_base_net_weights(db_.netCnt(), td_opt_.default_net_weight);
  std::vector<Flt> raw_timing_net_weights(db_.netCnt(), 1.0);
  std::vector<Flt> raw_clock_net_weights(db_.netCnt(), 1.0);

  // First pass: collect global WNS proxy from all sink slacks.
  // We use a very cheap virtual delay proxy:
  //   net_delay ~= Manhattan(driver, sink) * rc_per_dbu
  // and define slack as -net_delay to emphasize longer timing arcs.
  Flt wns = 0.0;
  bool has_violating_sink = false;
  const PtVec& cur_coords = coords_[cur_buf_];

  for (ID net_id = 0; net_id < db_.netCnt(); ++net_id) {
    const ID driver_pid = net_driver_pin_id_[net_id];
    if (driver_pid < 0) {
      continue;
    }
    const int fanout = std::max<int>(0, static_cast<int>(net_pin_ids_[net_id].size()) - 1);

    const Pin& drv_pin = db_.pins_[driver_pid];
    const Pt drv_pt = cur_coords[drv_pin.inst_id] + drv_pin.offset;

    for (ID sink_pid : net_pin_ids_[net_id]) {
      if (sink_pid == driver_pid) {
        continue;
      }
      const Pin& sink_pin = db_.pins_[sink_pid];
      if (!sink_pin.isInput) {
        continue;
      }

      const Pt sink_pt = cur_coords[sink_pin.inst_id] + sink_pin.offset;
      const Flt slack = estimateProxySlack(drv_pt, sink_pt, fanout);
      if (slack < 0.0) {
        has_violating_sink = true;
      }
      wns = std::min(wns, slack);
    }
  }

  if (!has_violating_sink || std::abs(wns) < 1e-12) {
    wl_.setNetWeights(net_weights_);
    wl_.setPinWeights(raw_pin_weights);
    if (td_opt_.verbose) {
      std::cout << "[XGP-TD] iter=" << iter
                << " no violating sink found, fallback to default weights\n";
    }
    return;
  }

  const Flt slack_ref = wns * 0.5;

  // Hybrid mode: use low-frequency OpenSTA WNS sampling to calibrate the
  // scale of lightweight proxy slacks. This keeps per-iteration runtime low
  // while anchoring the proxy model to real STA trends.
  maybeCalibrateWithSta(iter, wns);

  // Second pass: compute sink-pin weights and apply per-net sum cap.
  for (ID net_id = 0; net_id < db_.netCnt(); ++net_id) {
    const ID driver_pid = net_driver_pin_id_[net_id];
    if (driver_pid < 0) {
      continue;
    }
    const int fanout = std::max<int>(0, static_cast<int>(net_pin_ids_[net_id].size()) - 1);
    const int bucket = fanoutBucket(fanout);
    const bool is_clock_net = (net_id < static_cast<ID>(db_.odb_nets_.size()))
                              && (clock_nets_.find(db_.odb_nets_[net_id]) != clock_nets_.end());

    Flt base_weight = td_opt_.default_net_weight;
    if (fanout > 0) {
      base_weight += 0.25 * static_cast<Flt>(bucket + 1);
      base_weight += 0.05 * std::log1p(static_cast<Flt>(fanout));
    }
    raw_base_net_weights[net_id] = std::clamp(base_weight, 1.0, td_opt_.weight_cap);

    if (is_clock_net && fanout >= td_opt_.clock_net_fanout_threshold) {
      raw_clock_net_weights[net_id] = std::clamp(td_opt_.clock_net_weight, 1.0, td_opt_.weight_cap);
    }

    const Pin& drv_pin = db_.pins_[driver_pid];
    const Pt drv_pt = cur_coords[drv_pin.inst_id] + drv_pin.offset;

    Flt net_sum = 0.0;
    Flt net_best_slack = 0.0;
    bool net_has_violation = false;
    for (ID sink_pid : net_pin_ids_[net_id]) {
      if (sink_pid == driver_pid) {
        continue;
      }

      const Pin& sink_pin = db_.pins_[sink_pid];
      if (!sink_pin.isInput) {
        continue;
      }

      const Pt sink_pt = cur_coords[sink_pin.inst_id] + sink_pin.offset;
      const Flt slack = estimateProxySlack(drv_pt, sink_pt, fanout);
      if (slack < net_best_slack) {
        net_best_slack = slack;
        net_has_violation = true;
      }

      Flt weight = td_opt_.default_net_weight;
      if (slack < 0.0) {
        Flt dyn_weight = evalWeightFormula(slack, slack_ref, wns);
        if (!std::isfinite(dyn_weight)) {
          dyn_weight = 0.0;
        }
        dyn_weight *= (td_opt_.weight_scale * td_calib_scale_ * td_bucket_scales_[bucket]);
        weight = td_opt_.default_net_weight + dyn_weight;
      }

      weight = std::clamp(weight, 1.0, td_opt_.weight_cap);
      raw_pin_weights[sink_pid] = weight;
      net_sum += weight;
    }

    if (net_has_violation) {
      Flt net_dyn_weight = evalWeightFormula(net_best_slack, slack_ref, wns);
      if (!std::isfinite(net_dyn_weight)) {
        net_dyn_weight = 0.0;
      }
      net_dyn_weight *= (td_opt_.weight_scale * td_calib_scale_ * 0.60);
      raw_timing_net_weights[net_id] = std::clamp(1.0 + net_dyn_weight, 1.0, td_opt_.weight_cap);
    }

    // Per-net cap keeps optimization stable when fanout is huge.
    if (net_sum > td_opt_.net_weight_cap && net_sum > 1e-12) {
      const Flt ratio = td_opt_.net_weight_cap / net_sum;
      for (ID sink_pid : net_pin_ids_[net_id]) {
        if (sink_pid == driver_pid) {
          continue;
        }
        const Pin& sink_pin = db_.pins_[sink_pid];
        if (!sink_pin.isInput) {
          continue;
        }
        raw_pin_weights[sink_pid] *= ratio;
      }
    }
  }

  // EMA smoothing avoids large gradient jitter between updates.
  const Flt beta = std::clamp(td_opt_.ema_beta, 0.0, 1.0);
  const Flt mix_sum = td_opt_.base_net_mix + td_opt_.timing_net_mix + td_opt_.clock_net_mix;
  const Flt base_mix = (mix_sum > 1e-12) ? (td_opt_.base_net_mix / mix_sum) : 1.0;
  const Flt timing_mix = (mix_sum > 1e-12) ? (td_opt_.timing_net_mix / mix_sum) : 0.0;
  const Flt clock_mix = (mix_sum > 1e-12) ? (td_opt_.clock_net_mix / mix_sum) : 0.0;
  for (ID pid = 0; pid < db_.pinCnt(); ++pid) {
    p2p_pin_weights_[pid] = (1.0 - beta) * p2p_pin_weights_[pid] + beta * raw_pin_weights[pid];
  }
  for (ID net_id = 0; net_id < db_.netCnt(); ++net_id) {
    base_net_weights_[net_id] = (1.0 - beta) * base_net_weights_[net_id] + beta * raw_base_net_weights[net_id];
    timing_net_weights_[net_id] = (1.0 - beta) * timing_net_weights_[net_id] + beta * raw_timing_net_weights[net_id];
    clock_net_weights_[net_id] = (1.0 - beta) * clock_net_weights_[net_id] + beta * raw_clock_net_weights[net_id];
    const Flt merged_weight = base_mix * base_net_weights_[net_id]
                              + timing_mix * timing_net_weights_[net_id]
                              + clock_mix * clock_net_weights_[net_id];
    const Flt layer_max = std::max({base_net_weights_[net_id], timing_net_weights_[net_id], clock_net_weights_[net_id]});
    net_weights_[net_id] = std::clamp(std::max(merged_weight, layer_max), 1.0, td_opt_.weight_cap);
  }

  if (td_opt_.p2p_ratio_limit > 0.0 && !p2p_pin_weights_.empty() && !net_weights_.empty()) {
    Flt p2p_sum = 0.0;
    Flt net_sum = 0.0;
    for (Flt w : p2p_pin_weights_) {
      p2p_sum += std::max<Flt>(w, 0.0);
    }
    for (Flt w : net_weights_) {
      net_sum += std::max<Flt>(w, 0.0);
    }
    if (net_sum > 1e-12) {
      const Flt ratio = p2p_sum / net_sum;
      if (ratio > td_opt_.p2p_ratio_limit) {
        const Flt derate = td_opt_.p2p_ratio_limit / ratio;
        for (Flt& w : p2p_pin_weights_) {
          w *= derate;
        }
        if (td_opt_.verbose) {
          std::cout << "[XGP-TD] iter=" << iter
                    << " p2p/net ratio=" << ratio
                    << " derate=" << derate
                    << " limit=" << td_opt_.p2p_ratio_limit << "\n";
        }
      }
    }
  }

  wl_.setNetWeights(net_weights_);
  wl_.setPinWeights(p2p_pin_weights_);

  if (td_opt_.verbose) {
    std::cout << "[XGP-TD] iter=" << iter
              << " updated net/pin weights, wns_proxy=" << wns
              << " calib_scale=" << td_calib_scale_
              << " bucket_scales=[" << td_bucket_scales_[0] << ","
              << td_bucket_scales_[1] << ","
              << td_bucket_scales_[2] << ","
              << td_bucket_scales_[3] << "]"
              << " mix=[" << td_opt_.base_net_mix << ","
              << td_opt_.timing_net_mix << ","
              << td_opt_.clock_net_mix << "]"
              << " p2p_limit=" << td_opt_.p2p_ratio_limit
              << " formula=" << td_opt_.weight_formula << "\n";
  }
}

void NesterovSolver::maybeCalibrateWithSta(int iter, Flt proxy_wns)
{
  if (!td_opt_.hybrid_enable) {
    return;
  }
  if (td_opt_.sta == nullptr) {
    return;
  }
  if (td_opt_.calibrate_period <= 0) {
    return;
  }
  if (iter % td_opt_.calibrate_period != 0) {
    return;
  }

  // Protect from pathological zero division.
  const Flt proxy_mag = std::max<Flt>(std::abs(proxy_wns), 1e-12);

  try {
    // Refresh STA state before querying WNS. This is intentionally low
    // frequency to avoid high runtime overhead.
    td_opt_.sta->searchPreamble();
    td_opt_.sta->ensureLevelized();
    const sta::Slack sta_wns = td_opt_.sta->worstSlack(sta::MinMax::max());

    const Flt sta_mag = std::max<Flt>(std::abs(sta_wns), 1e-12);
    Flt ratio = sta_mag / proxy_mag;

    // Keep calibration bounded so a single noisy sample will not destabilize
    // weight updates.
    ratio = std::clamp(ratio, 0.20, 5.00);
    const Flt alpha = std::clamp(td_opt_.calibrate_alpha, 0.0, 1.0);
    td_calib_scale_ = (1.0 - alpha) * td_calib_scale_ + alpha * ratio;

    // Optional higher-fidelity calibration: sample STA net slack and fit
    // fanout-bucket scaling factors so the proxy model tracks STA better
    // beyond a single global WNS ratio.
    if (td_opt_.use_sta_net_slack_calibration
        && db_.odb_nets_.size() == static_cast<size_t>(db_.netCnt())) {
      std::array<Flt, 4> ratio_sum{{0.0, 0.0, 0.0, 0.0}};
      std::array<int, 4> ratio_cnt{{0, 0, 0, 0}};

      const int sample_limit = std::max(1, td_opt_.sta_net_sample_limit);
      const int stride = std::max(1, db_.netCnt() / sample_limit);
      const PtVec& cur_coords = coords_[cur_buf_];

      for (ID net_id = 0; net_id < db_.netCnt(); net_id += stride) {
        if (net_id < 0 || net_id >= static_cast<ID>(db_.odb_nets_.size())) {
          continue;
        }
        const ID driver_pid = net_driver_pin_id_[net_id];
        if (driver_pid < 0) {
          continue;
        }
        const odb::dbNet* odb_net = db_.odb_nets_[net_id];
        if (odb_net == nullptr) {
          continue;
        }

        const int fanout = std::max<int>(0, static_cast<int>(net_pin_ids_[net_id].size()) - 1);
        if (fanout < 1) {
          continue;
        }

        const Pin& drv_pin = db_.pins_[driver_pid];
        const Pt drv_pt = cur_coords[drv_pin.inst_id] + drv_pin.offset;

        Flt proxy_net_wns = 0.0;
        bool has_sink = false;
        for (ID sink_pid : net_pin_ids_[net_id]) {
          if (sink_pid == driver_pid) {
            continue;
          }
          const Pin& sink_pin = db_.pins_[sink_pid];
          if (!sink_pin.isInput) {
            continue;
          }
          has_sink = true;
          const Pt sink_pt = cur_coords[sink_pin.inst_id] + sink_pin.offset;
          const Flt sink_slack = estimateProxySlack(drv_pt, sink_pt, fanout);
          proxy_net_wns = std::min(proxy_net_wns, sink_slack);
        }
        if (!has_sink) {
          continue;
        }

        const sta::Slack sta_net_slack = td_opt_.sta->slack(odb_net, sta::MinMax::max());
        if (!std::isfinite(sta_net_slack)) {
          continue;
        }

        const Flt proxy_mag_net = std::max<Flt>(std::abs(proxy_net_wns), 1e-12);
        const Flt sta_mag_net = std::max<Flt>(std::abs(sta_net_slack), 1e-12);
        const Flt net_ratio = std::clamp(sta_mag_net / proxy_mag_net, 0.20, 5.00);

        const int bucket = fanoutBucket(fanout);
        ratio_sum[bucket] += net_ratio;
        ratio_cnt[bucket] += 1;
      }

      for (int b = 0; b < 4; ++b) {
        if (ratio_cnt[b] > 0) {
          const Flt mean_ratio = ratio_sum[b] / static_cast<Flt>(ratio_cnt[b]);
          td_bucket_scales_[b] = (1.0 - alpha) * td_bucket_scales_[b] + alpha * mean_ratio;
          td_bucket_scales_[b] = std::clamp(td_bucket_scales_[b], 0.20, 5.00);
        }
      }
    } else {
      // Fallback when net-level STA slack sampling is not available.
      for (int b = 0; b < 4; ++b) {
        const Flt exponent = 1.0 + 0.15 * b;
        const Flt target = std::clamp(std::pow(ratio, exponent), 0.20, 5.00);
        td_bucket_scales_[b] = (1.0 - alpha) * td_bucket_scales_[b] + alpha * target;
        td_bucket_scales_[b] = std::clamp(td_bucket_scales_[b], 0.20, 5.00);
      }
    }

    if (td_opt_.verbose) {
      std::cout << "[XGP-TD] iter=" << iter << " STA calibrate"
                << " sta_wns=" << sta_wns
                << " proxy_wns=" << proxy_wns
                << " ratio=" << ratio
              << " scale=" << td_calib_scale_
              << " bucket_scales=[" << td_bucket_scales_[0] << ","
              << td_bucket_scales_[1] << ","
              << td_bucket_scales_[2] << ","
              << td_bucket_scales_[3] << "]\n";
    }
  } catch (...) {
    // Fail-open behavior: keep solver progressing with proxy model only.
    if (td_opt_.verbose) {
      std::cout << "[XGP-TD] iter=" << iter
                << " STA calibrate failed, fallback to proxy-only" << "\n";
    }
  }
}

// ── solve ─────────────────────────────────────────────────────────────────────
void NesterovSolver::solve(const ConvergenceCriteria& crit, ProgressCb progress_cb)
{
  const Flt base_den_weight = std::max<Flt>(den_.weight(), 1e-6);
  const Flt max_den_weight = base_den_weight * 1e4;

  initBuffers();

  Flt ov = 0.0;
  for (int it = 0; it < crit.max_iter; ++it) {
    state_.iter = it;
    updateNesterovMomentum();

    // Hello-like behavior: timing data is periodically refreshed from the
    // current placement snapshot, then reused for subsequent iterations.
    updateTimingDrivenWeights(it);

    computeGrads(cur_buf_);
    last_grad_norm_ = vecNorm(grads_[cur_buf_]);

    den_(coords_[cur_buf_]);
    const Flt raw_ov = den_.overflow();
    const Flt total_mov_area = db_.totalMovableArea();
    ov = (total_mov_area > 0.0) ? (raw_ov / total_mov_area) : 0.0;

    nesterovStep();

    prev_buf_ = cur_buf_;
    cur_buf_ = 1 - cur_buf_;

    const Flt hpwl = wl_.hpwl();
    if (progress_cb) {
      progress_cb(it, ov, hpwl);
    }

    if (it % 10 == 0) {
      std::cout << "[XGP] iter=" << it << " raw_ov=" << raw_ov
                << " norm_ov=" << ov << " hpwl=" << hpwl
                << " grad_norm=" << last_grad_norm_
                << " step_norm=" << last_step_norm_ << "\n";
    }

    if (it + 1 < crit.max_iter) {
      Flt den_weight = den_.weight();
      const Flt target = std::max<Flt>(crit.target_overflow, 1e-4);
      if (ov < 0.5 * target) {
        den_weight *= 0.95;
      } else if (ov > target) {
        const Flt severity = std::min<Flt>(1.0, (ov - target) / (2.0 * target));
        den_weight *= (1.02 + 0.08 * severity);
      }
      den_.setWeight(std::clamp(den_weight, base_den_weight, max_den_weight));
    }

    if (it >= crit.min_iter && ov < crit.target_overflow) {
      state_.converged = true;
      break;
    }
  }

  // Re-evaluate overflow on the final accepted coordinates (prev_buf_) so the
  // exported solver state matches the placement that is written back to DB.
  den_(coords_[prev_buf_]);
  final_raw_overflow_ = den_.overflow();
  const Flt total_mov_area = db_.totalMovableArea();
  final_norm_overflow_ = (total_mov_area > 0.0) ? (final_raw_overflow_ / total_mov_area) : 0.0;

  commitCoords();
}

}  // namespace xgp

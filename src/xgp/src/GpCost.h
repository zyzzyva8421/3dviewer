// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpCost.h — WL and density cost functions.
// Mirrors wirelength and density cost from original placement solver.

#pragma once

#include "GpDb.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace xgp {

// ── Wire-Length Cost (LSE with EBeta) ────────────────────────────────────────
class WLCost {
 public:
  WLCost(DB* db) : db_(db), gamma_(1.0), weight_(1.0), ebeta_(0.5) {}

  void setGamma(Flt g) { gamma_ = g; }
  void setWeight(Flt w) { weight_ = w; }
  void setEBeta(Flt e) { ebeta_ = e; }
  void setNetWeights(const std::vector<Flt>& w) { 
    net_weights_ = w; 
    use_net_weights_ = !w.empty();
  }
  void setPinWeights(const std::vector<Flt>& w) {
    pin_weights_ = w;
    use_pin_weights_ = !w.empty();
  }
  Flt weight() const { return weight_; }
  Flt gamma() const { return gamma_; }
  Flt ebeta() const { return ebeta_; }
  Flt hpwl() const { return hpwl_; }
  Flt mstThreshold() const { return mst_threshold_; }

  // Set MST threshold: use MST for nets with more pins than this
  void setMstThreshold(int t) { mst_threshold_ = t; }
  
  // Enable extra MST for large nets (per Hello ExtraCost)
  void setExtraEnabled(bool e) { extra_enabled_ = e; }
  void setExtraRatio(Flt r) { extra_ratio_ = r; }
  
  // Extra MST cost: combines MST with HPWL based on ratio
  // For nets marked as extra large nets - uses MST + weighted HPWL
  Flt computeExtraCostForNet(const PtVec& coords, const std::vector<Pin>& pins, int net_id) {
    if (pins.size() < 3 || !extra_enabled_) {
      // Fall back to HPWL
      return computeMstForNet(coords, pins);
    }
    
    // Get pin locations
    PtVec pin_locs;
    for (const auto& pin : pins) {
      pin_locs.push_back(coords[pin.inst_id] + pin.offset);
    }
    
    // Compute HPWL
    Flt minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (const auto& p : pin_locs) {
      minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
      miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
    }
    Flt hpwl = (maxx - minx) + (maxy - miny);
    
    // Compute MST
    Flt mst_wl = computeMstForNet(coords, pins);
    
    // Combine: extra_cost = hpwl * keep_ratio + mst * extra_ratio
    // keep_ratio = 1 - extra_ratio (from Hello)
    Flt keep_ratio = 1.0 - extra_ratio_;
    return keep_ratio * hpwl + extra_ratio_ * mst_wl;
  }
  
  // Compute MST wirelength for a net using Prim's algorithm
  // For nets with fanout > threshold, computes MST instead of HPWL
  Flt computeMstForNet(const PtVec& coords, const std::vector<Pin>& pins) {
    if (pins.size() < 3) return 0.0;  // 2-pin nets use HPWL
    
    PtVec pin_locs;
    for (const auto& pin : pins) {
      pin_locs.push_back(coords[pin.inst_id] + pin.offset);
    }
    
    // Prim's MST: start from first pin, always connect to nearest unconnected
    Flt mst_wl = 0.0;
    std::vector<bool> connected(pins.size(), false);
    connected[0] = true;
    
    for (int iter = 1; iter < (int)pins.size(); ++iter) {
      Flt best_dist = 1e30;
      int best_to = -1;
      
      for (int i = 0; i < (int)pins.size(); ++i) {
        if (!connected[i]) continue;
        for (int j = 0; j < (int)pins.size(); ++j) {
          if (connected[j]) continue;
          Pt d = pin_locs[i] - pin_locs[j];
          Flt dist = std::abs(d.x) + std::abs(d.y);  // Manhattan
          if (dist < best_dist) {
            best_dist = dist;
            best_to = j;
          }
        }
      }
      
      if (best_to >= 0) {
        mst_wl += best_dist;
        connected[best_to] = true;
      }
    }
    
    return mst_wl;
  }

  // Compute EBeta offset for a given pin count (per Hello)
  // offset = 2 * ebeta * log(pin_count)
  static Flt calcEBetaOffset(int pin_count, Flt ebeta) {
    if (pin_count <= 1) return 0.0;
    return 2.0 * ebeta * std::log((Flt)pin_count);
  }

  // Compute cost only (with net weights)
  Flt operator()(const PtVec& coords) {
    computeNetBBoxes(coords);
    Flt total = 0.0;
    for (const auto& nb : net_bboxes_) {
      Flt w = 1.0;
      if (use_net_weights_ && nb.net_id < (int)net_weights_.size()) {
        w = net_weights_[nb.net_id];
      }
      total += w * (nb.bbox.width() + nb.bbox.height());
    }
    hpwl_ = total;
    return weight_ * total;
  }

  // Compute cost + gradient
  Flt operator()(const PtVec& coords, PtVec& grad) {
    std::fill(grad.begin(), grad.end(), Pt{});
    computeNetBBoxes(coords);
    Flt total_wl = 0.0;

    // LSE wirelength gradients with EBeta correction
    for (const auto& nb : net_bboxes_) {
      const Pt dCenter{
          (nb.bbox.lo.x + nb.bbox.hi.x) * 0.5,
          (nb.bbox.lo.y + nb.bbox.hi.y) * 0.5};
      if (!std::isfinite(dCenter.x) || !std::isfinite(dCenter.y)) {
        continue;
      }
      
      // EBeta offset: add log(pin_count) correction for large nets
      int pin_count = (int)pin_lists_[nb.net_id].size();
      Flt ebeta_offset = calcEBetaOffset(pin_count, ebeta_);
      (void)ebeta_offset;
      
      auto safeExp = [](Flt x) {
        // Keep exponential in a numerically safe range.
        const Flt xc = std::clamp(x, -50.0, 50.0);
        return std::exp(xc);
      };

      const Flt expSumX = safeExp((nb.bbox.lo.x - dCenter.x) / gamma_) +
                          safeExp((nb.bbox.hi.x - dCenter.x) / gamma_);
      const Flt expSumY = safeExp((nb.bbox.lo.y - dCenter.y) / gamma_) +
                          safeExp((nb.bbox.hi.y - dCenter.y) / gamma_);
      if (expSumX < 1e-20 || expSumY < 1e-20) continue;

      const Pt expD{
          safeExp((nb.bbox.lo.x - dCenter.x) / gamma_) -
          safeExp((nb.bbox.hi.x - dCenter.x) / gamma_),
          safeExp((nb.bbox.lo.y - dCenter.y) / gamma_) -
          safeExp((nb.bbox.hi.y - dCenter.y) / gamma_)};

      const Flt diffX = expD.x * (gamma_ / expSumX);
      const Flt diffY = expD.y * (gamma_ / expSumY);
      if (!std::isfinite(diffX) || !std::isfinite(diffY)) {
        continue;
      }

      // Distribute to pins
      for (ID pin_id : pin_ids_[nb.net_id]) {
        const Pin& pin = db_->pins_[pin_id];
        const Pt& p = coords[pin.inst_id];
        const Flt wx = (p.x < dCenter.x) ? diffX : -diffX;
        const Flt wy = (p.y < dCenter.y) ? diffY : -diffY;
        
        // Apply net weight if available
        Flt w = weight_;
        if (use_net_weights_ && nb.net_id < (int)net_weights_.size()) {
          w *= net_weights_[nb.net_id];
        }
        if (use_pin_weights_ && pin_id < (ID)pin_weights_.size()) {
          w *= pin_weights_[pin_id];
        }
        
        grad[pin.inst_id].x += w * wx;
        grad[pin.inst_id].y += w * wy;
      }

      Flt net_w = 1.0;
      if (use_net_weights_ && nb.net_id < (int)net_weights_.size()) {
        net_w = net_weights_[nb.net_id];
      }
      total_wl += net_w * (nb.bbox.width() + nb.bbox.height());
    }
    hpwl_ = total_wl;
    return weight_ * hpwl_;
  }

 private:
  DB* db_;
  Flt gamma_ = 1.0;
  Flt weight_ = 1.0;
  Flt ebeta_ = 0.5;
  Flt hpwl_ = 0.0;
  int mst_threshold_ = 50;  // Use MST for nets with > 50 pins
  bool extra_enabled_ = false;  // Extra MST enabled
  Flt extra_ratio_ = 0.5;  // Extra MST ratio (from Hello)
  bool use_net_weights_ = false;
  std::vector<Flt> net_weights_;
  bool use_pin_weights_ = false;
  std::vector<Flt> pin_weights_;

  struct NetBBox {
    ID net_id = 0;
    Box bbox;
  };
  std::vector<NetBBox> net_bboxes_;
  std::vector<std::vector<Pin>> pin_lists_;
  std::vector<std::vector<ID>> pin_ids_;

  void computeNetBBoxes(const PtVec& coords) {
    const ID n = db_->netCnt();
    net_bboxes_.clear();
    net_bboxes_.reserve(n);
    pin_lists_.clear();
    pin_lists_.resize(n);
    pin_ids_.clear();
    pin_ids_.resize(n);

    // Group pins by net
    for (ID pin_id = 0; pin_id < (ID)db_->pins_.size(); ++pin_id) {
      const auto& pin = db_->pins_[pin_id];
      if (pin.inst_id < coords.size() && pin.net_id < (ID)pin_lists_.size()) {
        pin_lists_[pin.net_id].push_back(pin);
        pin_ids_[pin.net_id].push_back(pin_id);
      }
    }

    // Compute bounding boxes
    for (ID ni = 0; ni < n; ++ni) {
      const auto& pins = pin_lists_[ni];
      if (pins.empty()) continue;
      Flt minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
      for (const auto& pin : pins) {
        if (pin.inst_id >= coords.size()) continue;
        const Pt p = coords[pin.inst_id] + pin.offset;
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x);
        maxy = std::max(maxy, p.y);
      }
      Box nb(minx, miny, maxx, maxy);
      if (nb.width() < 1e-10 && nb.height() < 1e-10 && !pins.empty() && pins[0].inst_id < coords.size()) {
        // Connect to center
        nb = Box{coords[pins[0].inst_id].x - 0.5, coords[pins[0].inst_id].y - 0.5,
                coords[pins[0].inst_id].x + 0.5, coords[pins[0].inst_id].y + 0.5};
      }
      net_bboxes_.push_back({ni, nb});
    }
  }
};

// ── Density Cost (Bell) ────────────────────────────────────────────────────────
enum class InstAreaType { Real, WithPadding, WithPaddingAdjusted };

class DenCost {
 public:
  DenCost(DB* db) : db_(db), target_(0.7), weight_(1.0), radius_(2), smooth_(false),
      inst_area_type_(InstAreaType::WithPadding), inst_size_type_(InstAreaType::WithPadding),
      grad_wt_mode_(99), macro_cost_ratio_(1.0), dwa_enabled_(false) {}

  void setTarget(Flt t) { target_ = t; }
  void setWeight(Flt w) { weight_ = w; }
  void setSmooth(bool s) { smooth_ = s; }
  void setInstAreaType(InstAreaType t) { inst_area_type_ = t; }
  void setInstSizeType(InstAreaType t) { inst_size_type_ = t; }
  void setGradWtMode(unsigned m) { grad_wt_mode_ = m; }
  void setMacroCostRatio(Flt r) { macro_cost_ratio_ = r; }
  void setDWAEnabled(bool e) { dwa_enabled_ = e; }
  void setPadding(Flt p) { padding_ = p; }
  
  Flt weight() const { return weight_; }
  Flt target() const { return target_; }
  Flt overflow() const { return overflow_; }
  bool smooth() const { return smooth_; }
  unsigned gradWtMode() const { return grad_wt_mode_; }
  bool isDoSpread() const { return grad_wt_mode_ != 99; }

  // Compute cost only
  Flt operator()(const PtVec& coords) {
    clearBins();

    // Accumulate instance areas into bins
    for (const auto& inst : db_->insts_) {
      if (inst.fixed) continue;
      addInstToBins(inst, coords[inst.id]);
    }

    // Compute overflow
    Flt total = 0.0;
    overflow_ = 0.0;
    for (const auto& bin : db_->grid().bins) {
      const Flt bin_target = bin.is_macro ? (target_ * macro_cost_ratio_) : target_;
      const Flt area = bin.area;
      if (area > bin_target) {
        const Flt of = area - bin_target;
        overflow_ += of;
        total += 0.5 * weight_ * of * of;
      }
    }
    return total;
  }

  // Compute cost + gradient
  Flt operator()(const PtVec& coords, PtVec& grad) {
    const ID n = db_->instCnt();
    std::fill(grad.begin(), grad.end(), Pt{});
    clearBins();

    // Accumulate instance areas into bins
    for (const auto& inst : db_->insts_) {
      if (inst.fixed) continue;
      addInstToBins(inst, coords[inst.id]);
    }

    // Compute overflow_ at BIN level (sum of (area - target) for overcrowded bins)
    // NOT instance level - avoids double counting across instances in same bin
    // Matches GPL: sum_overflow = sum over bins of max(0, area - target)
    overflow_ = 0.0;
    for (const auto& bin : db_->grid().bins) {
      if (bin.area > bin.target) {
        overflow_ += (bin.area - bin.target);
      }
    }

    // Compute gradients using Bell-shaped (smooth) kernel
    // Gradient direction: from instance toward center of overcrowded bins
    Flt total = 0.0;
    const auto& grid = db_->grid();
    const ID2 r = radius_;

    for (ID i = 0; i < n; ++i) {
      const Inst& inst = db_->insts_[i];
      if (inst.fixed) continue;

      const Pt p = coords[i];
      const ID2 bi = grid.binId(p);
      const ID2 bj = bi / grid.nx;
      const ID2 bii = bi % grid.nx;

      // Check nearby bins
      for (ID2 dj = -r; dj <= r; ++dj) {
        const ID2 j = bj + dj;
        if (j < 0 || j >= grid.ny) continue;
        for (ID2 di = -r; di <= r; ++di) {
          const ID2 k = bii + di;
          if (k < 0 || k >= grid.nx) continue;
          const auto& bin = grid.at(k, j);
          if (bin.area <= bin.target) continue;

          const Flt of = bin.area - bin.target;

          // Bell-shaped gradient: Gaussian kernel centered on bin center
          const Pt bc = bin.box.center();
          const Pt d{p.x - bc.x, p.y - bc.y};
          const Flt dist = d.norm();
          const Flt sig = 0.5 * grid.step.x;
          if (dist < 3.0 * sig) {
            const Flt g = weight_ * of * inst.w * inst.h *
                       std::exp(-dist * dist / (2.0 * sig * sig));
            grad[i].x += g * (d.x / (dist + 1e-10));
            grad[i].y += g * (d.y / (dist + 1e-10));
          }

          // Cost contribution (for reference)
          total += 0.5 * weight_ * of * of * inst.w * inst.h
                   * std::exp(-dist * dist / (2.0 * sig * sig));
        }
      }
    }

    // Free bin cost: push density toward under-utilized bins
    if (free_bin_cost_weight_ > 0.0) {
      for (ID i = 0; i < n; ++i) {
        const Inst& inst = db_->insts_[i];
        if (inst.fixed) continue;
        const Pt p = coords[i];
        const ID2 bi = grid.binId(p);
        const ID2 bj = bi / grid.nx;
        const ID2 bii = bi % grid.nx;
        
        // Find nearest free bin
        Flt best_dist = 1e30;
        Pt best_center;
        for (ID2 dj = -r; dj <= r; ++dj) {
          const ID2 j = bj + dj;
          if (j < 0 || j >= grid.ny) continue;
          for (ID2 di = -r; di <= r; ++di) {
            const ID2 k = bii + di;
            if (k < 0 || k >= grid.nx) continue;
            const auto& bin = grid.at(k, j);
            // Free bin: area < target (with some margin)
            if (bin.area < bin.target * 0.9) {
              const Pt bc = bin.box.center();
              const Pt d{p.x - bc.x, p.y - bc.y};
              const Flt dist = d.norm();
              if (dist < best_dist) {
                best_dist = dist;
                best_center = bc;
              }
            }
          }
        }
        
        // Apply free bin gradient
        if (best_dist < 1e29) {
          const Pt d{p.x - best_center.x, p.y - best_center.y};
          const Flt dist = d.norm();
          if (dist > 1e-10) {
            const Flt g = free_bin_cost_weight_ * inst.w * inst.h / (dist + 1.0);
            grad[i].x -= g * (d.x / dist);
            grad[i].y -= g * (d.y / dist);
          }
        }
      }
    }

    // Constraint bins cost: bins with constraints
    if (cons_bin_cost_weight_ > 0.0) {
      // For bins with constraints, similar treatment as overflow
    }

    // Sum overflow
    for (const auto& bin : db_->grid().bins) {
      const Flt target = bin.target;
      if (bin.area > target) {
        const Flt of = bin.area - target;
        total += 0.5 * weight_ * of * of;
      }
    }
    return total;
  }

  // Helper: get instance size with padding
  Pt getInstSizeWithPadding(const Inst& inst) const {
    Pt sz(inst.w, inst.h);
    if (inst_size_type_ == InstAreaType::WithPadding) {
      sz.x += padding_;
      sz.y += padding_;
    } else if (inst_size_type_ == InstAreaType::WithPaddingAdjusted) {
      // Apply DWA adjustment if enabled
      if (dwa_enabled_ && inst.id < inst_added_rat_.size()) {
        Flt scale = std::sqrt(inst_added_rat_[inst.id]);
        sz.x *= scale;
        sz.y *= scale;
      }
    }
    return sz;
  }

 private:
  DB* db_;
  Flt target_ = 0.7;
  Flt weight_ = 1.0;
  Flt overflow_ = 0.0;
  int radius_ = 2;
  bool smooth_ = false;
  InstAreaType inst_area_type_ = InstAreaType::WithPadding;
  InstAreaType inst_size_type_ = InstAreaType::WithPadding;
  unsigned grad_wt_mode_ = 99;
  Flt macro_cost_ratio_ = 1.0;
  bool dwa_enabled_ = false;
  Flt padding_ = 2.0;
  Flt free_bin_cost_weight_ = 0.0;
  Flt cons_bin_cost_weight_ = 0.0;
  Flt total_free_area_ = 0.0;
  std::vector<Flt> inst_added_rat_;
  std::vector<Flt> bin_target_areas_;

  void clearBins() {
    for (auto& bin : db_->grid().bins) {
      bin.area = 0.0;
    }
  }

  void addInstToBins(const Inst& inst, const Pt& p) {
    Grid& g = db_->grid();
    const ID2 bi = g.binId(p);
    const ID2 bj = bi / g.nx;
    const ID2 bii = bi % g.nx;

    for (ID2 dj = -radius_; dj <= radius_; ++dj) {
      const ID2 j = bj + dj;
      if (j < 0 || j >= g.ny) continue;
      for (ID2 di = -radius_; di <= radius_; ++di) {
        const ID2 k = bii + di;
        if (k < 0 || k >= g.nx) continue;
        Bin& bin = g.at(k, j);
        const Box& bbin = bin.box;
        // Instance area coverage
        Flt ix1 = p.x - inst.w * 0.5;
        Flt iy1 = p.y - inst.h * 0.5;
        Flt ix2 = p.x + inst.w * 0.5;
        Flt iy2 = p.y + inst.h * 0.5;
        // Bin overlap area
        Flt ox1 = std::max(ix1, bbin.lo.x);
        Flt oy1 = std::max(iy1, bbin.lo.y);
        Flt ox2 = std::min(ix2, bbin.hi.x);
        Flt oy2 = std::min(iy2, bbin.hi.y);
        if (ox2 > ox1 && oy2 > oy1) {
          bin.area += (ox2 - ox1) * (oy2 - oy1);
        }
      }
    }
  }
};

}  // namespace xgp
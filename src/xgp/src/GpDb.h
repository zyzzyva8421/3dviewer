// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpDb.h — Database adapter for xgp.
// Mirrors GP database access from original placement.

#pragma once

#include "GpTypes.h"

namespace odb {
class dbNet;
}

namespace xgp {

// ── Instance ───────────────────────────────────────────────────────────────────────
struct Inst {
  ID   id    = 0;
  Flt  w = 0, h = 0;  // width, height in DBU
  Pt   p;                // current position (center) in DBU
  bool fixed = false;     // fixed instance (macros, IO, etc.)
  bool isMacro = false;   // macro instance

  Inst() = default;
  Inst(ID i, Flt w_, Flt h_, const Pt& p_, bool fixed_ = false)
      : id(i), w(w_), h(h_), p(p_), fixed(fixed_) {}
};

// ── Pin ────────────────────────────────────────────────────────────────────────
struct Pin {
  ID  net_id = 0;
  ID  inst_id = 0;
  Pt  offset{};               // pin offset within instance (local coords)
  bool isInput = false;        // input pin

  Pin() = default;
  Pin(ID ni, ID ii, const Pt& off, bool in = false)
      : net_id(ni), inst_id(ii), offset(off), isInput(in) {}
};

// ── Net ────────────────────────────────────────────────────────────────────────
struct Net {
  ID   id = 0;
  ID2  pins{};    // number of pins (2 for bezier, else 2)
  Flt  weight = 1.0;
  bool isSignal = true;

  Net() = default;
  Net(ID i, ID2 p, bool sig = true) : id(i), pins(p), isSignal(sig) {}
};

// ── Bin Grid ──────────────────────────────────────────────────────────────────────────
struct Bin {
  ID    ix = 0, iy = 0;
  Box   box{};           // bin bounding box in DBU
  Flt   area = 0.0;      // total instance area in bin
  Flt   target = 0.0;    // target area (density * bin area)
  bool  isExtended = false;  // extended halo bin
  bool  is_macro = false; // bin contains macro instances

  Bin() = default;
  Bin(ID id, ID x, ID y, const Box& b) : ix(x), iy(y), box(b) {}
  Flt overflow() const { return area > target ? area - target : 0.0; }
};

using BinVec = std::vector<Bin>;

// ── Grid ───────────────────────────────────────────────────────────────────────────
struct Grid {
  ID2  nx = 0, ny = 0;     // bin count per axis
  Pt   step{};              // bin pitch (DBU)
  Pt   origin{};            // grid origin (lower-left corner)
  BinVec bins;             // all bins

  Grid() = default;
  Grid(ID2 x, ID2 y, const Pt& step_, const Pt& origin_)
      : nx(x), ny(y), step(step_), origin(origin_) {
    bins.reserve(nx * ny);
    for (ID2 j = 0; j < ny; ++j) {
      for (ID2 i = 0; i < nx; ++i) {
        Box b(origin.x + i * step.x, origin.y + j * step.y,
            origin.x + (i + 1) * step.x, origin.y + (j + 1) * step.y);
        ID id = i + j * nx;
        bins.emplace_back(id, i, j, b);  // id, x, y, box
      }
    }
  }

  Bin& at(ID2 i, ID2 j) { return bins[i + j * nx]; }
  const Bin& at(ID2 i, ID2 j) const { return bins[i + j * nx]; }
  ID2 binId(const Pt& p) const {
    ID2 i = (ID2)((p.x - origin.x) / step.x);
    ID2 j = (ID2)((p.y - origin.y) / step.y);
    return std::max(ID2(0), std::min(i + j * nx, (ID2)bins.size() - 1));
  }
};

// ── DB ────────────────────────────────────────────────────────────────────────
// Mirrors GPDatabase from original placement.
// Holds all placement data: instances, nets, pins, bin grid.
class DB {
 public:
  // Metadata
  Flt dbu_per_micron = 1.0;  // DBU per micron
  Box  die{};               // die bounding box in DBU

  std::vector<Inst> insts_;     // instances (movables + fixed)
  std::vector<Net>  nets_;      // nets
  std::vector<Pin>  pins_;     // pins
  std::vector<odb::dbNet*> odb_nets_;  // ODB nets aligned with nets_
  Grid              grid_;     // density bin grid

  std::vector<ID> movables_;   // indices of movable instances
  mutable Flt totalMovableArea_ = 0.0;  // cached total movable area

  // Accessors
  ID instCnt() const { return (ID)insts_.size(); }
  ID netCnt() const { return (ID)nets_.size(); }
  ID pinCnt() const { return (ID)pins_.size(); }
  ID movCnt() const { return (ID)movables_.size(); }

  // Instance access
  Inst& insts() { return insts_[0]; }
  const Inst& insts() const { return insts_[0]; }
  Inst& inst(ID i) { return insts_[i]; }
  const Inst& inst(ID i) const { return insts_[i]; }

  Inst* instPtr(ID i) { return i < insts_.size() ? &insts_[i] : nullptr; }

  // Current coordinates (for reading back after solve)
  PtVec coords() const {
    PtVec c;
    c.reserve(insts_.size());
    for (const auto& inst : insts_) c.push_back(inst.p);
    return c;
  }

  // Set coordinates
  void setCoords(const PtVec& c) {
    for (size_t i = 0; i < insts_.size() && i < c.size(); ++i) {
      insts_[i].p = c[i];
    }
  }

  Grid& grid() { return grid_; }
  const Grid& grid() const { return grid_; }

  // Total area of movable instances (for overflow normalization)
  Flt totalMovableArea() const {
    if (totalMovableArea_ == 0.0) {
      Flt sum = 0.0;
      for (const auto& inst : insts_) {
        if (!inst.fixed) sum += inst.w * inst.h;
      }
      totalMovableArea_ = sum;
    }
    return std::max(totalMovableArea_, 1.0);
  }

  // Density
  Flt overflow() const {
    if (grid_.bins.empty()) return 0.0;
    Flt total = 0.0;
    Flt max_of = 0.0;
    for (const auto& bin : grid_.bins) {
      Flt of = bin.overflow();
      total += of;
      max_of = std::max(max_of, of);
    }
    return max_of;  // use max overflow for convergence
  }
};

}  // namespace xgp
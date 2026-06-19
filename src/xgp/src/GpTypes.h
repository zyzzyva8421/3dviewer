// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// GpTypes.h — Core scalar/vector types for xgp.
// Mirrors basic types from original placement solver.

#pragma once

#include <cmath>
#include <vector>

namespace xgp {

// ── Core Scalar/Vector Types ───────────────────────────────────────────────────────────
// Flt: scalar type (mirrors DataType from original solver)
using Flt = double;

// Pt: 2D point (mirrors Point2 from original solver)
struct Pt {
  Flt x = 0.0, y = 0.0;

  Pt() = default;
  Pt(Flt x, Flt y) : x(x), y(y) {}

  Pt operator+(const Pt& o) const { return Pt{x + o.x, y + o.y}; }
  Pt operator-(const Pt& o) const { return Pt{x - o.x, y - o.y}; }
  Pt operator*(Flt s) const { return Pt{x * s, y * s}; }
  Pt operator/(Flt s) const { return Pt{x / s, y / s}; }

  // Scalar * Pt (for 0.001 * grads[i])
  friend Pt operator*(Flt s, const Pt& p) { return Pt{s * p.x, s * p.y}; }

  Pt& operator+=(const Pt& o) { x += o.x; y += o.y; return *this; }
  Pt& operator-=(const Pt& o) { x -= o.x; y -= o.y; return *this; }
  Pt& operator*=(Flt s) { x *= s; y *= s; return *this; }

  // Euclidean norm: ||P|| = sqrt(x^2 + y^2)
  // Mirrors original solver's Point2::norm()
  Flt norm() const { return std::sqrt(x * x + y * y); }

  // Euclidean distance between two points
  Flt dist(const Pt& o) const { return (*this - o).norm(); }

  // Element-wise sqrt
  Pt sqrt() const { return Pt{std::sqrt(x), std::sqrt(y)}; }
};

// Box: axis-aligned bounding box
struct Box {
  Pt lo, hi;
  Box() = default;
  Box(Flt x1, Flt y1, Flt x2, Flt y2) : lo(x1, y1), hi(x2, y2) {}
  Box(const Pt& a, const Pt& b) : lo(a), hi(b) {}

  Flt width() const { return hi.x - lo.x; }
  Flt height() const { return hi.y - lo.y; }
  Flt area() const { return width() * height(); }
  Pt center() const { return Pt{(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5}; }

  bool contains(const Pt& p) const {
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
  }
  bool overlaps(const Box& o) const {
    return !(o.hi.x < lo.x || o.lo.x > hi.x || o.hi.y < lo.y || o.lo.y > hi.y);
  }
};

// ID2: 2D index (mirrors ID2 from original solver)
using ID = int;
using ID2 = ID;

// Vector of points (mirrors Vec<Point2> from original solver)
using PtVec = std::vector<Pt>;

}  // namespace xgp
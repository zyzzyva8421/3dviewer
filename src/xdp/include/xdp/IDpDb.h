// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors
//
// IDpDb.h — Abstract interface between DP algorithm core and platform DB.
//
// Design rules:
//   - All coordinates are in DBU (integer).
//   - Views are read-only snapshots populated via import().
//   - Writeback goes through setCellPos() + commit().
//   - No platform headers (ODB / DBt*) appear here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xdp {

// ── Basic scalar types ────────────────────────────────────────────────────────
using ID  = int32_t;
using Flt = double;
static constexpr ID INVALID_ID = -1;

// ── Cell orientation ──────────────────────────────────────────────────────────
// Matches ODB dbOrientType values used in DP.
enum class Orient : int8_t {
  N  = 0,  // normal
  S  = 1,  // 180°
  FN = 2,  // flip X  (mirror about Y-axis)
  FS = 3,  // flip Y  (mirror about X-axis, i.e. 180° + flip)
};

// ── Geometry ──────────────────────────────────────────────────────────────────
struct DpRect {
  int lx, ly, ux, uy;  // DBU
};

// ── Row ───────────────────────────────────────────────────────────────────────
// One placement row (standard cell or macro row).
struct RowView {
  ID     id;
  int    lx, ly;      // origin in DBU
  int    site_w;      // site width in DBU
  int    site_h;      // site height in DBU
  int    num_sites;   // total number of sites
  Orient orient;      // row orientation
};

// ── Cell ──────────────────────────────────────────────────────────────────────
struct CellView {
  ID     id;
  int    lx, ly;      // current lower-left in DBU
  int    w,  h;       // width / height in DBU
  bool   is_fixed;
  bool   is_macro;
  Orient orient;
  ID     pin_offset;  // first pin in pins[] for this cell
  ID     pin_cnt;
};

// ── Net ───────────────────────────────────────────────────────────────────────
struct NetView {
  ID  id;
  ID  pin_offset;
  ID  pin_cnt;
};

// ── Pin ───────────────────────────────────────────────────────────────────────
// offset_x/y relative to cell lower-left in DBU.
// For IO (BTerms): cell_id == INVALID_ID, cx/cy are fixed absolute coords.
struct PinView {
  ID   id;
  ID   cell_id;       // INVALID_ID if IO pad
  ID   net_id;
  int  offset_x;      // relative to cell lower-left in DBU
  int  offset_y;
  int  cx, cy;        // absolute center in DBU (recomputed after each move)
  bool is_io;
};

// ── Site ──────────────────────────────────────────────────────────────────────
// Site type description used for legalization.
struct SiteView {
  int  w, h;          // DBU
  bool is_hybrid;     // hybrid-height row
};

// ── IDpDb ─────────────────────────────────────────────────────────────────────
// Platform adapters (e.g. OdbDpDb) implement this interface.
// The DP algorithm core talks only to IDpDb.
class IDpDb {
 public:
  virtual ~IDpDb() = default;

  // ── Read phase ──────────────────────────────────────────────────────────────
  // Called once before DP starts.
  virtual void import() = 0;

  // Core area bounding box
  virtual DpRect coreRect() const = 0;

  // Flat arrays — DP indexes directly by ID
  virtual const std::vector<CellView>& cells() const = 0;
  virtual const std::vector<NetView>&  nets()  const = 0;
  virtual const std::vector<PinView>&  pins()  const = 0;
  virtual const std::vector<RowView>&  rows()  const = 0;

  // Total movable (non-fixed, non-macro) cell count
  virtual int movableCellCount() const = 0;

  // ── Write phase ─────────────────────────────────────────────────────────────
  // DP calls setCellPos() for each moved cell, then commit() once.
  virtual void setCellPos(ID cell_id, int lx, int ly, Orient orient) = 0;

  // Flush pending positions to the platform DB.
  virtual void commit() = 0;

  // ── Optional filler / well-tap hints ────────────────────────────────────────
  // Returns the master name for filler cells (may be empty if not configured).
  // DP uses this to optionally insert fillers during legalization.
  virtual std::vector<std::string> fillerMasterNames() const { return {}; }
  virtual std::string wellTapMasterName() const { return {}; }

  // ── Diagnostic hook ─────────────────────────────────────────────────────────
  virtual void onLegalizeEnd(int violations) { (void)violations; }
};

}  // namespace xdp

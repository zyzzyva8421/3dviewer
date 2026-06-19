// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors

#include "OdbDpDb.h"

#include "odb/db.h"
#include "utl/Logger.h"

namespace xdp {

// ── Orient helpers ────────────────────────────────────────────────────────────

Orient OdbDpDb::odbOrientToXdp(odb::dbOrientType o)
{
  switch (o.getValue()) {
    case odb::dbOrientType::R0:   return Orient::N;
    case odb::dbOrientType::R180: return Orient::S;
    case odb::dbOrientType::MY:   return Orient::FN;
    case odb::dbOrientType::MX:   return Orient::FS;
    default:                       return Orient::N;
  }
}

odb::dbOrientType OdbDpDb::xdpOrientToOdb(Orient o)
{
  switch (o) {
    case Orient::N:  return odb::dbOrientType::R0;
    case Orient::S:  return odb::dbOrientType::R180;
    case Orient::FN: return odb::dbOrientType::MY;
    case Orient::FS: return odb::dbOrientType::MX;
    default:         return odb::dbOrientType::R0;
  }
}

// ── Constructor ───────────────────────────────────────────────────────────────

OdbDpDb::OdbDpDb(odb::dbBlock* block, utl::Logger* logger)
    : block_(block), logger_(logger)
{
}

// ── import ────────────────────────────────────────────────────────────────────

void OdbDpDb::import()
{
  // ── Core rect ───────────────────────────────────────────────────────────────
  {
    odb::Rect r = block_->getCoreArea();
    core_ = {r.xMin(), r.yMin(), r.xMax(), r.yMax()};
  }

  // ── Rows ────────────────────────────────────────────────────────────────────
  rows_.clear();
  {
    ID rid = 0;
    for (odb::dbRow* row : block_->getRows()) {
      odb::dbSite* site = row->getSite();
      int sw = static_cast<int>(site->getWidth());
      int sh = static_cast<int>(site->getHeight());
      odb::Point origin = row->getOrigin();
      int num_sites = static_cast<int>(row->getSiteCount());
      RowView rv;
      rv.id        = rid++;
      rv.lx        = origin.getX();
      rv.ly        = origin.getY();
      rv.site_w    = sw;
      rv.site_h    = sh;
      rv.num_sites = num_sites;
      rv.orient    = odbOrientToXdp(row->getOrient());
      rows_.push_back(rv);
    }
  }

  // ── Cells ───────────────────────────────────────────────────────────────────
  cells_.clear();
  cell_inst_.clear();

  // We need a stable inst-ID → CellView mapping.
  // Use a map from dbInst* → cell ID for pin construction.
  std::unordered_map<odb::dbInst*, ID> inst_to_id;

  {
    ID cid = 0;
    for (odb::dbInst* inst : block_->getInsts()) {
      odb::dbBox*  bbox = inst->getBBox();
      odb::Rect    r    = bbox->getBox();
      bool is_fixed  = inst->isFixed();
      bool is_macro  = inst->getMaster()->isBlock();

      CellView cv;
      cv.id         = cid;
      cv.lx         = r.xMin();
      cv.ly         = r.yMin();
      cv.w          = r.dx();
      cv.h          = r.dy();
      cv.is_fixed   = is_fixed;
      cv.is_macro   = is_macro;
      cv.orient     = odbOrientToXdp(inst->getOrient());
      cv.pin_offset = 0;  // filled after pins built
      cv.pin_cnt    = 0;

      cells_.push_back(cv);
      cell_inst_.push_back(inst);
      inst_to_id[inst] = cid;

      if (!is_fixed && !is_macro) {
        ++movable_cell_count_;
      }
      ++cid;
    }
  }

  // ── Nets + Pins ─────────────────────────────────────────────────────────────
  // Build flat pins_ array; for each cell track first-pin offset + count.
  // Approach: collect per-net pins, then flatten.
  nets_.clear();
  pins_.clear();

  // Temporary per-cell pin lists to set pin_offset/cnt on cells
  std::vector<std::vector<ID>> cell_pin_ids(cells_.size());

  {
    ID nid = 0;
    for (odb::dbNet* net : block_->getNets()) {
      ID pin_offset = static_cast<ID>(pins_.size());
      ID pin_cnt    = 0;

      // ITerms (cell pins)
      for (odb::dbITerm* iterm : net->getITerms()) {
        odb::dbInst* inst = iterm->getInst();
        auto it = inst_to_id.find(inst);
        if (it == inst_to_id.end()) continue;

        ID cell_id = it->second;

        // Pin center (absolute) from ODB average
        int cx = 0, cy = 0;
        iterm->getAvgXY(&cx, &cy);

        // Offset relative to cell lower-left
        const CellView& cv = cells_[cell_id];
        int offset_x = cx - cv.lx;
        int offset_y = cy - cv.ly;

        PinView pv;
        pv.id       = static_cast<ID>(pins_.size());
        pv.cell_id  = cell_id;
        pv.net_id   = nid;
        pv.offset_x = offset_x;
        pv.offset_y = offset_y;
        pv.cx       = cx;
        pv.cy       = cy;
        pv.is_io    = false;

        pins_.push_back(pv);
        cell_pin_ids[cell_id].push_back(pv.id);
        ++pin_cnt;
      }

      // BTerms (IO pads)
      for (odb::dbBTerm* bterm : net->getBTerms()) {
        int cx = 0, cy = 0;
        if (!bterm->getFirstPinLocation(cx, cy)) continue;

        PinView pv;
        pv.id       = static_cast<ID>(pins_.size());
        pv.cell_id  = INVALID_ID;
        pv.net_id   = nid;
        pv.offset_x = 0;
        pv.offset_y = 0;
        pv.cx       = cx;
        pv.cy       = cy;
        pv.is_io    = true;

        pins_.push_back(pv);
        ++pin_cnt;
      }

      NetView nv;
      nv.id         = nid;
      nv.pin_offset = pin_offset;
      nv.pin_cnt    = pin_cnt;
      nets_.push_back(nv);
      ++nid;
    }
  }

  // Fill pin_offset / pin_cnt on cells
  for (ID cid = 0; cid < static_cast<ID>(cells_.size()); ++cid) {
    const auto& cpins = cell_pin_ids[cid];
    if (!cpins.empty()) {
      cells_[cid].pin_offset = cpins.front();
      cells_[cid].pin_cnt    = static_cast<ID>(cpins.size());
    }
  }
}

// ── setCellPos ────────────────────────────────────────────────────────────────

void OdbDpDb::setCellPos(ID cell_id, int lx, int ly, Orient orient)
{
  pending_[cell_id] = {lx, ly, orient};
}

// ── commit ────────────────────────────────────────────────────────────────────

void OdbDpDb::commit()
{
  for (auto& [cid, pos] : pending_) {
    if (cid < 0 || cid >= static_cast<ID>(cell_inst_.size())) continue;
    odb::dbInst* inst = cell_inst_[cid];
    if (inst->isFixed()) continue;
    inst->setLocation(pos.lx, pos.ly);
    inst->setOrient(xdpOrientToOdb(pos.orient));
    inst->setPlacementStatus(odb::dbPlacementStatus::PLACED);
  }
  pending_.clear();
}

}  // namespace xdp

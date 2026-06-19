// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors
//
// Xgp.cc — xgp command entry point.

#include "Xgp.h"
#include "GpPlacer.h"
#include "GpDb.h"
#include "GpCost.h"
#include "GpSolver.h"

#include "ord/OpenRoad.hh"
#include "odb/db.h"
#include "utl/Logger.h"

#include <unordered_map>

namespace xgp {
void Xgp::globalPlace(int max_iter,
                      bool verbose,
                      bool td_enable,
                      int td_update_period,
                      int td_formula,
                      double td_weight_scale,
                      bool td_hybrid_enable,
                      int td_calibrate_period,
                      double td_calibrate_alpha,
                      double td_weight_cap,
                      double td_net_cap,
                      double td_ema_beta,
                      double td_rc_per_dbu,
                      double td_wire_res_per_dbu,
                      double td_wire_cap_per_dbu,
                      double td_sink_cap_base,
                      double td_fanout_cap_scale,
                      double td_fanout_delay_scale,
                      double td_base_net_mix,
                      double td_timing_net_mix,
                      double td_clock_net_mix,
                      bool td_use_clock_net_weight,
                      double td_clock_net_weight,
                      int td_clock_net_fanout_threshold,
                      double td_p2p_ratio_limit,
                      bool td_use_sta_net_slack_calibration,
                      int td_sta_net_sample_limit) {
  // Get logger from OpenRoad app
  utl::Logger* logger = ord::OpenRoad::openRoad()->getLogger();

  // Import ODB database
  odb::dbBlock* block = ord::OpenRoad::openRoad()->getDb()->getChip()->getBlock();
  if (!block) {
    logger->error(utl::XGP, 1, "No block loaded");
  }

  // Convert ODB to GP DB
  DB db;
  db.dbu_per_micron = block->getDbUnitsPerMicron();

  // Get die box
  odb::Rect diebox = block->getDieArea();
  db.die = Box(diebox.xMin(), diebox.yMin(), diebox.xMax(), diebox.yMax());

  // Build local contiguous instance ids for all movable instances.
  // This is required because dbInst::getId() is not guaranteed to be dense.
  std::unordered_map<odb::dbInst*, ID> inst_map;
  ID inst_cnt = 0;
  for (auto inst : block->getInsts()) {
    if (inst->isFixed()) continue;
    const odb::Rect rect = inst->getBBox()->getBox();
    Flt w = rect.xMax() - rect.xMin();
    Flt h = rect.yMax() - rect.yMin();
    Pt p = Pt((rect.xMin() + rect.xMax()) * 0.5, (rect.yMin() + rect.yMax()) * 0.5);
    db.insts_.emplace_back(inst_cnt, w, h, p, false);
    if (inst->isBlock()) {
      db.insts_.back().isMacro = true;
    }
    inst_map[inst] = inst_cnt;
    inst_cnt++;
  }

  // Build local contiguous net ids and pins.
  std::unordered_map<odb::dbNet*, ID> net_map;
  for (auto net : block->getNets()) {
    if (net->isSpecial()) continue;
    int pins = 0;
    for (auto* iterm : net->getITerms()) {
      (void)iterm;
      pins++;
    }
    for (auto* bterm : net->getBTerms()) {
      (void)bterm;
      pins++;
    }
    if (pins >= 2) {
      bool is_signal = (net->getSigType() == odb::dbSigType::SIGNAL);
      ID net_id = static_cast<ID>(db.nets_.size());
      db.nets_.emplace_back(net_id, pins, is_signal);
      db.odb_nets_.push_back(net);
      net_map[net] = net_id;

      // Build instance terminal pins. BTerms are skipped in this lightweight
      // model because they do not map to movable instances directly.
      for (auto ipin : net->getITerms()) {
        odb::dbInst* inst = ipin->getInst();
        auto it = inst_map.find(inst);
        if (it == inst_map.end()) {
          continue;
        }

        int x, y;
        ipin->getAvgXY(&x, &y);
        Pt off = Pt(x - inst->getOrigin().getX(),
                   y - inst->getOrigin().getY());
        db.pins_.emplace_back(net_id, it->second, off, ipin->isInputSignal());
      }
    }
  }

  // Movables index
  for (ID i = 0; i < db.insts_.size(); ++i) {
    if (!db.insts_[i].fixed) {
      db.movables_.push_back(i);
    }
  }

  // Run placement
  GpPlacer placer(db);
  PlacerOptions opts;
  opts.max_iter = max_iter;
  opts.verbose = verbose;
  opts.wl_gamma = 1.0;
  opts.td_enable = td_enable;
  opts.td_update_period = td_update_period;
  opts.td_formula = td_formula;
  opts.td_weight_scale = td_weight_scale;
  opts.td_hybrid_enable = td_hybrid_enable;
  opts.td_calibrate_period = td_calibrate_period;
  opts.td_calibrate_alpha = td_calibrate_alpha;
  opts.td_weight_cap = td_weight_cap;
  opts.td_net_cap = td_net_cap;
  opts.td_ema_beta = td_ema_beta;
  opts.td_rc_per_dbu = td_rc_per_dbu;
  opts.td_wire_res_per_dbu = td_wire_res_per_dbu;
  opts.td_wire_cap_per_dbu = td_wire_cap_per_dbu;
  opts.td_sink_cap_base = td_sink_cap_base;
  opts.td_fanout_cap_scale = td_fanout_cap_scale;
  opts.td_fanout_delay_scale = td_fanout_delay_scale;
  opts.td_base_net_mix = td_base_net_mix;
  opts.td_timing_net_mix = td_timing_net_mix;
  opts.td_clock_net_mix = td_clock_net_mix;
  opts.td_use_clock_net_weight = td_use_clock_net_weight;
  opts.td_clock_net_weight = td_clock_net_weight;
  opts.td_clock_net_fanout_threshold = td_clock_net_fanout_threshold;
  opts.td_p2p_ratio_limit = td_p2p_ratio_limit;
  opts.td_use_sta_net_slack_calibration = td_use_sta_net_slack_calibration;
  opts.td_sta_net_sample_limit = td_sta_net_sample_limit;
  opts.td_sta = ord::OpenRoad::openRoad()->getSta();
  placer.run(opts);

  // Write back to ODB
  ID idx = 0;
  for (auto inst : block->getInsts()) {
    if (!inst->isFixed() && idx < db.instCnt()) {
      Pt p = db.insts_[idx].p;
      inst->setLocation((int)p.x, (int)p.y);
      idx++;
    }
  }

  logger->info(utl::XGP, 2,
                "xgp::globalPlace done: overflow={}", placer.overflow());
}

}  // namespace xgp
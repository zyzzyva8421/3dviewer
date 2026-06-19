// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors
//
// OdbDpDb.h — ODB-backed implementation of IDpDb.
#pragma once

#include <unordered_map>
#include <vector>

#include "odb/db.h"
#include "utl/Logger.h"
#include "xdp/IDpDb.h"

namespace xdp {

class OdbDpDb : public IDpDb
{
 public:
  OdbDpDb(odb::dbBlock* block, utl::Logger* logger);

  // IDpDb interface
  void import() override;

  DpRect coreRect() const override { return core_; }

  const std::vector<CellView>& cells() const override { return cells_; }
  const std::vector<NetView>&  nets()  const override { return nets_;  }
  const std::vector<PinView>&  pins()  const override { return pins_;  }
  const std::vector<RowView>&  rows()  const override { return rows_;  }

  int movableCellCount() const override { return movable_cell_count_; }

  void setCellPos(ID cell_id, int lx, int ly, Orient orient) override;
  void commit() override;

 private:
  odb::dbBlock* block_;
  utl::Logger*  logger_;

  DpRect core_{};
  std::vector<CellView> cells_;
  std::vector<NetView>  nets_;
  std::vector<PinView>  pins_;
  std::vector<RowView>  rows_;

  int movable_cell_count_ = 0;

  // Map from cell ID → dbInst* for commit writeback
  std::vector<odb::dbInst*> cell_inst_;

  // Pending positions: cell_id → (lx, ly, orient)
  struct PendingPos {
    int lx, ly;
    Orient orient;
  };
  std::unordered_map<ID, PendingPos> pending_;

  // Helpers
  static Orient odbOrientToXdp(odb::dbOrientType o);
  static odb::dbOrientType xdpOrientToOdb(Orient o);
};

}  // namespace xdp

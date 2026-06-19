// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors

#include "xdp/Xdp.h"

#include "OdbDpDb.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace xdp {

struct Xdp::Impl
{
  odb::dbDatabase* db     = nullptr;
  utl::Logger*     logger = nullptr;
};

Xdp::Xdp(odb::dbDatabase* db, utl::Logger* logger)
    : impl_(std::make_unique<Impl>())
{
  impl_->db     = db;
  impl_->logger = logger;
}

Xdp::~Xdp() = default;

void Xdp::detailedPlace(bool incremental, bool verbose)
{
  auto* block = impl_->db->getChip()->getBlock();
  OdbDpDb db(block, impl_->logger);
  db.import();

  impl_->logger->info(utl::XDP,
                      1,
                      "xdp::detailedPlace cells={} nets={} rows={} movable={}",
                      db.cells().size(),
                      db.nets().size(),
                      db.rows().size(),
                      db.movableCellCount());

  // Algorithm stub: positions unchanged, write back as-is.
  db.commit();
}

}  // namespace xdp

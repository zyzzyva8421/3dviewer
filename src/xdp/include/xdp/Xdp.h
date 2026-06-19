// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors
#pragma once

#include <memory>

namespace odb {
class dbDatabase;
}
namespace utl {
class Logger;
}

namespace xdp {

class Xdp
{
 public:
  Xdp(odb::dbDatabase* db, utl::Logger* logger);
  ~Xdp();

  // Main entry: run detailed placement / legalization.
  void detailedPlace(bool incremental, bool verbose);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xdp

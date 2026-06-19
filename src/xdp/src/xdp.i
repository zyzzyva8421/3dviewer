// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors
//
// xdp.i — SWIG interface for the xdp module.

%module xdp
%{
#include "ord/OpenRoad.hh"
#include "gpl/Replace.h"
%}

%include "Exception.i"

%inline %{

void
xdp_detailed_place_cmd(bool incremental, bool verbose)
{
  // detailedPlace uses Replace for detailed placement
  ord::OpenRoad::openRoad()->getReplace()->doIncrementalPlace(1);
}

void
xdp_global_place_cmd(int max_iter, bool verbose)
{
  ord::OpenRoad::openRoad()->getReplace()->doNesterovPlace(max_iter, {}, 0);
}

%}

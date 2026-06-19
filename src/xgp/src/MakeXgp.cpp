// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xgp Authors

#include "xgp/MakeXgp.h"

#include "tcl.h"
#include "utl/decode.h"

extern "C" {
extern int Xgp_Init(Tcl_Interp* interp);
}

namespace xgp {

extern const char* xgp_tcl_inits[];

void initXgp(Tcl_Interp* tcl_interp)
{
  Xgp_Init(tcl_interp);
  utl::evalTclInit(tcl_interp, xgp::xgp_tcl_inits);
}

}  // namespace xgp
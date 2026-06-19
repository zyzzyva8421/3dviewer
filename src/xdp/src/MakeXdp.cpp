// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, xdp Authors

#include "xdp/MakeXdp.h"

#include "tcl.h"
#include "utl/decode.h"

extern "C" {
extern int Xdp_Init(Tcl_Interp* interp);
}

namespace xdp {

extern const char* xdp_tcl_inits[];

void initXdp(Tcl_Interp* tcl_interp)
{
  Xdp_Init(tcl_interp);
  utl::evalTclInit(tcl_interp, xdp::xdp_tcl_inits);
}

}  // namespace xdp

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, xdp Authors

include("openroad")

swig_lib(NAME      xdp
         NAMESPACE xdp
         I_FILE    src/xdp.i
         SCRIPTS   src/xdp.tcl
         SWIG_INCLUDES ${PROJECT_SOURCE_DIR}/src
                       ${ODB_HOME}/src/swig/common
                       ${ODB_HOME}/src/swig/tcl
                       ${ODB_HOME}/include
)

target_sources(xdp
  PRIVATE
    src/MakeXdp.cpp
    src/Xdp.cpp
    src/OdbDpDb.cpp
)

target_include_directories(xdp
  PUBLIC
    include
)

target_link_libraries(xdp
  PRIVATE
    utl_lib
    odb
    gpl_lib
)

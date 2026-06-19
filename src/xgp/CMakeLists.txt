# xgp CMakeLists.txt

include("openroad")

swig_lib(NAME      xgp
         NAMESPACE xgp
         I_FILE    src/xgp.i
         SCRIPTS   src/Xgp.tcl
         SWIG_INCLUDES ${PROJECT_SOURCE_DIR}/src
                       ${PROJECT_SOURCE_DIR}/src/gpl/include
                       ${PROJECT_SOURCE_DIR}/src/xgp/src
                       ${PROJECT_SOURCE_DIR}/src/utl/include
                       ${PROJECT_SOURCE_DIR}/src/odb/include
                       ${ODB_HOME}/src/swig/common
                       ${ODB_HOME}/src/swig/tcl
                       ${ODB_HOME}/include
)

# Single library with all sources
add_library(xgp_lib STATIC
  src/MakeXgp.cpp
  src/GpTypes.h
  src/GpDb.h
  src/GpCost.h
  src/GpSolver.h
  src/GpSolver.cpp
  src/GpPlacer.h
  src/GpPlacer.cpp
)

# Add Xgp.cc to xgp target for wrapper symbols
target_sources(xgp
  PRIVATE
  src/Xgp.cc
)

target_include_directories(xgp_lib
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/utl/include
  ${PROJECT_SOURCE_DIR}/src/dbSta/include
  ${PROJECT_SOURCE_DIR}/src/sta/include
)

# Add include paths for SWIG wrapper
target_include_directories(xgp
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src/gpl/include
  ${CMAKE_SOURCE_DIR}/src/odb/include
  ${CMAKE_SOURCE_DIR}/src/xgp/src
  ${PROJECT_SOURCE_DIR}/src/utl/include
)

target_link_libraries(xgp_lib
  PUBLIC
  ord
  odb
  utl
  dbSta
  OpenSTA
)

# Also need utl for xgp target (swig)
target_link_libraries(xgp
  PRIVATE
  utl
  xgp_lib
)

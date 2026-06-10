# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025, The OpenROAD Authors
#
# Regression tests for OpenROAD 3D Viewer

find_package(GTest REQUIRED)

# Root source directory for easy reference
set(OR3D_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ============================================================================
# OdbSceneBuilder unit tests (data layer, no GUI)
# ============================================================================

add_executable(test_odb_scene_builder test_OdbSceneBuilder.cpp)

target_include_directories(test_odb_scene_builder PRIVATE
    ${OR3D_DIR}
    ${OR3D_DIR}/../..
    ${OPENROAD_HOME}/include
    ${OPENROAD_HOME}/src/odb/include
)

target_sources(test_odb_scene_builder PRIVATE
    ${OR3D_DIR}/OdbSceneBuilder.cpp
)

target_link_libraries(test_odb_scene_builder PRIVATE
    GTest::GTest
    GTest::Main
    odb
    utl_lib
    Boost::boost
)

add_test(NAME openroad3d/test_odb_scene_builder
    COMMAND test_odb_scene_builder
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# LayerControl widget tests (pure Qt, no OSG)
# ============================================================================

find_package(Qt5 QUIET COMPONENTS Test)

add_executable(test_layer_control
    test_main.cpp
    test_layer_control.cpp
    ${OR3D_DIR}/LayerControl.cpp
)

target_include_directories(test_layer_control PRIVATE
    ${OR3D_DIR}
)

target_link_libraries(test_layer_control PRIVATE
    GTest::GTest
    Qt5::Widgets
)

if(Qt5Test_FOUND)
    target_compile_definitions(test_layer_control PRIVATE HAS_QT_TEST=1)
    target_link_libraries(test_layer_control PRIVATE Qt5::Test)
endif()

# Qt AUTOMOC is inherited from parent scope
set_target_properties(test_layer_control PROPERTIES AUTOMOC ON)

add_test(NAME openroad3d/test_layer_control
    COMMAND test_layer_control
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# OsgScene scene graph tests (OSG data structures, no viewer)
# ============================================================================

set(OSG_TEST_SRCS
    test_main.cpp
    test_osg_scene.cpp
    ${OR3D_DIR}/render/backend_osg/OsgScene.cpp
    ${OR3D_DIR}/render/backend_osg/OsgGeometry.cpp
    ${OR3D_DIR}/render/backend_osg/OsgMaterial.cpp
    ${OR3D_DIR}/render/backend_osg/OsgLight.cpp
    ${OR3D_DIR}/render/backend_osg/OsgTexture.cpp
    ${OR3D_DIR}/render/backend_osg/OsgFontAtlas.cpp
    ${OR3D_DIR}/render/backend_osg/OsgTextGeode.cpp
)

add_executable(test_osg_scene ${OSG_TEST_SRCS})

target_include_directories(test_osg_scene PRIVATE
    ${OR3D_DIR}
    ${OR3D_DIR}/../..
    ${OR3D_DIR}/render/backend_osg
    ${OPENSCENEGRAPH_INCLUDE_DIR}
)


target_link_libraries(test_osg_scene PRIVATE
    GTest::GTest
    Qt5::Core
    Qt5::Widgets
    ${OPENSCENEGRAPH_LIBRARIES}
)

add_test(NAME openroad3d/test_osg_scene
    COMMAND test_osg_scene
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# Full-stack window smoke test (Qt + OSG + ODB)
# ============================================================================

set(WINDOW_TEST_SRCS
    test_main.cpp
    test_window_smoke.cpp
    ${OR3D_DIR}/OpenRoad3DWindow.cpp
    ${OR3D_DIR}/OdbSceneBuilder.cpp
    ${OR3D_DIR}/LayerControl.cpp
    ${OR3D_DIR}/render/api/BackendFactory.cpp
    ${OR3D_DIR}/render/backend_osg/OsgBackend.cpp
    ${OR3D_DIR}/render/backend_osg/OsgScene.cpp
    ${OR3D_DIR}/render/backend_osg/OsgGeometry.cpp
    ${OR3D_DIR}/render/backend_osg/OsgMaterial.cpp
    ${OR3D_DIR}/render/backend_osg/OsgLight.cpp
    ${OR3D_DIR}/render/backend_osg/OsgTexture.cpp
    ${OR3D_DIR}/render/backend_osg/OsgFontAtlas.cpp
    ${OR3D_DIR}/render/backend_osg/OsgTextGeode.cpp
)

add_executable(test_gui_smoke ${WINDOW_TEST_SRCS})

target_include_directories(test_gui_smoke PRIVATE
    ${OR3D_DIR}
    ${OR3D_DIR}/../..
    ${OPENSCENEGRAPH_INCLUDE_DIR}
    ${OPENROAD_HOME}/include
    ${OPENROAD_HOME}/src/odb/include
)

target_link_libraries(test_gui_smoke PRIVATE
    GTest::GTest
    Qt5::Core
    Qt5::Widgets
    Qt5::OpenGL
    OpenGL::GL
    odb
    utl_lib
    Boost::boost
    ${OPENSCENEGRAPH_LIBRARIES}
)

set_target_properties(test_gui_smoke PROPERTIES AUTOMOC ON)

add_test(NAME openroad3d/test_gui_smoke
    COMMAND test_gui_smoke
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================================
# Common: set ODB path for real-data tests
# ============================================================================

if(DEFINED OPENROAD_3D_TEST_ODB)
    foreach(TEST_TARGET
            openroad3d/test_odb_scene_builder
            openroad3d/test_gui_smoke)
        set_tests_properties(${TEST_TARGET} PROPERTIES
            ENVIRONMENT "OPENROAD_3D_TEST_ODB=${OPENROAD_3D_TEST_ODB}")
    endforeach()
endif()

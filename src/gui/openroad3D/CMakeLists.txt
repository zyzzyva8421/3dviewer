# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025, The OpenROAD Authors

# OpenROAD 3D Viewer - standalone 3D layout viewer
find_package(OpenSceneGraph REQUIRED COMPONENTS osgViewer osgDB osgUtil osgGA osgShadow)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets OpenGL)

set(OPENROAD3D_SOURCES
    main.cpp
    OpenRoad3DWindow.cpp
    OdbSceneBuilder.cpp
    LayerControl.cpp

    # Core domain (reused from 3dviewer)
    core/domain/SceneTypes.h

    # Render API interfaces (reused from 3dviewer)
    render/api/BackendFactory.cpp
    render/api/RenderCapabilities.cpp
    render/api/BackendType.h
    render/api/IGeometry.h
    render/api/ILight.h
    render/api/IMaterial.h
    render/api/IRenderBackend.h
    render/api/IScene.h
    render/api/ITexture.h
    render/api/RenderCapabilities.h

    # OSG backend implementation (reused from 3dviewer)
    render/backend_osg/OsgBackend.cpp
    render/backend_osg/OsgBackend.h
    render/backend_osg/OsgGeometry.cpp
    render/backend_osg/OsgGeometry.h
    render/backend_osg/OsgLight.cpp
    render/backend_osg/OsgLight.h
    render/backend_osg/OsgMaterial.cpp
    render/backend_osg/OsgMaterial.h
    render/backend_osg/OsgPickHandler.cpp
    render/backend_osg/OsgPickHandler.h
    render/backend_osg/OsgScene.cpp
    render/backend_osg/OsgScene.h
    render/backend_osg/OsgTexture.cpp
    render/backend_osg/OsgTexture.h
)

add_executable(openroad_3d ${OPENROAD3D_SOURCES})

target_include_directories(openroad_3d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENSCENEGRAPH_INCLUDE_DIR}
    ${OPENROAD_HOME}/include
    ${OPENROAD_HOME}/src/odb/include
)

target_link_libraries(openroad_3d PRIVATE
    odb
    Qt5::Core
    Qt5::Widgets
    Qt5::OpenGL
    OpenGL::GL
    ${OPENSCENEGRAPH_LIBRARIES}
    utl_lib
    Boost::boost
)

target_compile_definitions(openroad_3d PRIVATE
    OSG_HAS_SHASH
    HAS_OSG
)

# Disable LTO for this target
set_target_properties(openroad_3d PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(openroad_3d PRIVATE "-fno-lto")
target_link_options(openroad_3d PRIVATE "-fno-lto")

install(TARGETS openroad_3d RUNTIME DESTINATION bin)
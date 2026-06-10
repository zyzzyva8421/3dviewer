// SPDX-License-Identifier: BSD-3-Clause
//
// Standalone debug tool: creates a single wire object with a label,
// then dumps the scene graph (transform matrices, positions) for
// visual verification of label positioning.
//
// Build & run:
//   g++ -std=c++17 -I.. -I../../.. $(pkg-config --cflags openscenegraph) \
//       debug_label.cpp ../render/backend_osg/OsgScene.cpp \
//       ../render/backend_osg/OsgGeometry.cpp \
//       ../render/backend_osg/OsgMaterial.cpp \
//       ../render/backend_osg/OsgLight.cpp \
//       ../render/backend_osg/OsgTexture.cpp \
//       ../render/backend_osg/OsgFontAtlas.cpp \
//       ../render/backend_osg/OsgTextGeode.cpp \
//       $(pkg-config --libs openscenegraph) -o debug_label
//   ./debug_label

#include <cstdio>
#include <string>
#include <array>

#include <osg/Node>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Geometry>

#include "core/domain/SceneTypes.h"
#include "render/backend_osg/OsgScene.h"

using namespace viewer3d::domain;
using namespace viewer3d::render::backend_osg;

static void dumpNode(osg::Node* node, int depth = 0);

static void dumpLabelNode(osg::Node* node, int depth = 0) {
    if (!node) return;
    const std::string indent(depth * 2, ' ');

    osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(node);
    if (mt) {
        osg::Matrixd mat = mt->getMatrix();
        osg::Vec3d t = mat.getTrans();
        osg::Vec3d s = mat.getScale();
        osg::Quat r = mat.getRotate();
        double ra, rx, ry, rz;
        r.getRotate(ra, rx, ry, rz);

        fprintf(stderr, "%s[MatrixTransform] name='%s'\n", indent.c_str(), node->getName().c_str());
        fprintf(stderr, "%s  translate=(%.4f, %.4f, %.4f)\n", indent.c_str(), t.x(), t.y(), t.z());
        fprintf(stderr, "%s  scale=(%.4f, %.4f, %.4f)\n", indent.c_str(), s.x(), s.y(), s.z());
        fprintf(stderr, "%s  rotate=(%.2fdeg around [%.2f, %.2f, %.2f])\n",
                indent.c_str(), ra * 180.0 / M_PI, rx, ry, rz);

        // Dump matrix row by row
        fprintf(stderr, "%s  matrix:\n", indent.c_str());
        for (int row = 0; row < 4; ++row) {
            fprintf(stderr, "%s    [%.4f  %.4f  %.4f  %.4f]\n",
                    indent.c_str(),
                    mat(row, 0), mat(row, 1), mat(row, 2), mat(row, 3));
        }
    }

    osg::Geode* geode = dynamic_cast<osg::Geode*>(node);
    if (geode) {
        fprintf(stderr, "%s[Geode] name='%s' drawables=%u\n",
                indent.c_str(), node->getName().c_str(), geode->getNumDrawables());
    }

    osg::Group* group = node->asGroup();
    if (group) {
        for (unsigned int i = 0; i < group->getNumChildren(); ++i) {
            dumpLabelNode(group->getChild(i), depth + 1);
        }
    }
}

static void verifyLabelPlacement(osg::Node* node,
                                   float expectedSurfaceZ,
                                   float expectedOffset,
                                   const char* labelName) {
    osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(node);
    if (!mt) return;

    std::string name = node->getName();
    if (name.find("labelXform:") == std::string::npos) return;

    osg::Matrixd mat = mt->getMatrix();
    osg::Vec3d t = mat.getTrans();
    osg::Vec3d s = mat.getScale();

    float worldZ = t.z();
    float scale = s.z();

    fprintf(stderr, "\n=== Label Verification: %s ===\n", labelName);
    fprintf(stderr, "  Surface z: %.4f\n", expectedSurfaceZ);
    fprintf(stderr, "  Label local z: %.4f\n", t.z());
    fprintf(stderr, "  Label scale: %.4f\n", scale);
    fprintf(stderr, "  Text height at scale: %.4f\n", scale * 0.8f);  // charSize=0.8
    fprintf(stderr, "  Expected gap: %.4f\n", expectedOffset);
    fprintf(stderr, "  Actual distance from surface: %.4f\n", worldZ - expectedSurfaceZ);

    // Check if label is close to surface (within tolerance)
    float tolerance = 0.05f;
    float distanceFromSurface = worldZ - expectedSurfaceZ;
    if (distanceFromSurface < 0 || distanceFromSurface > expectedOffset + tolerance) {
        fprintf(stderr, "  WARNING: Label may not be properly attached to surface!\n");
    } else {
        fprintf(stderr, "  OK: Label appears to be properly positioned\n");
    }
}

int main() {
    // Create a scene with font
    OsgScene scene;

    // Simulate a typical M1 wire: 0.2 wide, 5 long, 0.13 thick
    // centered at (10, 20) in the XY plane
    ObjectRecord wire;
    wire.objectId = "test_wire";
    wire.type = ObjectType::Wire;
    wire.displayName = "wire_NET1";
    wire.hasBbox = true;
    wire.layerId = "metal1";

    // Wire from (9.9, 10) to (10.1, 15), thickness 0.13
    wire.bboxMin = {9.9f, 10.0f, 0.0f};
    wire.bboxMax = {10.1f, 15.0f, 0.13f};

    // Transform at center of bbox
    float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;  // 10.0
    float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;  // 12.5
    float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;  // 0.065
    wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

    fprintf(stderr, "=== Wire bbox: [%.2f, %.2f, %.2f] -> [%.2f, %.2f, %.2f]\n",
            wire.bboxMin[0], wire.bboxMin[1], wire.bboxMin[2],
            wire.bboxMax[0], wire.bboxMax[1], wire.bboxMax[2]);
    fprintf(stderr, "=== Transform center: (%.2f, %.2f, %.2f)\n", cx, cy, cz);
    fprintf(stderr, "=== dims: x=%.2f y=%.2f z=%.2f\n",
            wire.bboxMax[0]-wire.bboxMin[0],
            wire.bboxMax[1]-wire.bboxMin[1],
            wire.bboxMax[2]-wire.bboxMin[2]);

    // Add metal1 layer
    LayerRecord layer;
    layer.layerId = "metal1";
    layer.name = "metal1";
    layer.zBase = 0.065f;
    layer.thickness = 0.13f;
    layer.visible = true;
    layer.selectable = true;
    layer.color = {0.9f, 0.2f, 0.2f, 1.0f};
    scene.updateLayer(layer);

    // Create the scene object with label
    OsgSceneObject* sceneObj = scene.createSceneObject(wire, scene.getLayer("metal1"));
    scene.addObject(sceneObj);

    // Enable display names
    scene.setDisplayNamesVisible(true);

    // Dump the scene graph
    fprintf(stderr, "\n=== Scene Graph Dump ===\n");
    osg::Group* root = scene.getObjectRoot();
    if (root) {
        fprintf(stderr, "[Root] children=%u\n", root->getNumChildren());
        for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
            dumpLabelNode(root->getChild(i));
        }
    }

    // Verify label placement
    fprintf(stderr, "\n=== Label Placement Verification ===\n");

    // Wire is at z=0.065, with thickness 0.13, so front surface is at z=0.13
    float wireFrontSurfaceZ = 0.13f;
    float expectedOffset = 0.016f;  // charSize * 0.02 = 0.8 * 0.02

    // Find the wire node and its labels
    for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
        osg::Node* child = root->getChild(i);
        osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(child);
        if (mt && child->getName() == "test_wire") {
            osg::Group* wireGroup = mt->asGroup();
            if (wireGroup) {
                for (unsigned int j = 0; j < wireGroup->getNumChildren(); ++j) {
                    osg::Node* labelNode = wireGroup->getChild(j);
                    if (labelNode->getName().find("labelXform:") != std::string::npos) {
                        verifyLabelPlacement(labelNode, wireFrontSurfaceZ, expectedOffset, "wire_NET1");
                    }
                }
            }
        }
    }

    // Now delete the scene object and clean up
    scene.removeObject(sceneObj);

    fprintf(stderr, "\n=== DONE ===\n");
    return 0;
}
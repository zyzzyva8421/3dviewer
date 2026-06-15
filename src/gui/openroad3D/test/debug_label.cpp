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

static const char* pass_fail(bool ok) {
    return ok ? "PASS" : "FAIL";
}

static void dumpNode(osg::Node* node, int depth = 0) {
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
    }

    osg::Geode* geode = dynamic_cast<osg::Geode*>(node);
    if (geode) {
        fprintf(stderr, "%s[Geode] name='%s' drawables=%u\n",
                indent.c_str(), node->getName().c_str(), geode->getNumDrawables());
    }

    osg::Group* group = node->asGroup();
    if (group) {
        for (unsigned int i = 0; i < group->getNumChildren(); ++i) {
            dumpNode(group->getChild(i), depth + 1);
        }
    }
}

int main() {
    // Create a scene with font
    OsgScene scene;

    int totalTests = 0;
    int passedTests = 0;

    // =========================================================================
    // Test 1: Vertical wire (Y longest) - text on Front/Back (Z) and Right/Left (X)
    // =========================================================================
    {
        fprintf(stderr, "\n============================================================\n");
        fprintf(stderr, "TEST 1: Vertical wire (ylen > xlen, ylen > zlen)\n");
        fprintf(stderr, "============================================================\n");

        ObjectRecord wire;
        wire.objectId = "vertical_wire";
        wire.type = ObjectType::Wire;
        wire.displayName = "wire_VNET";
        wire.hasBbox = true;
        wire.layerId = "metal1";

        // Wire: x from 9.9 to 10.1 (xlen=0.2), y from 10 to 15 (ylen=5), z from 0 to 0.13 (zlen=0.13)
        wire.bboxMin = {9.9f, 10.0f, 0.0f};
        wire.bboxMax = {10.1f, 15.0f, 0.13f};

        float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;  // 10.0
        float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;  // 12.5
        float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;  // 0.065
        wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

        LayerRecord layer;
        layer.layerId = "metal1";
        layer.name = "metal1";
        layer.zBase = 0.065f;
        layer.thickness = 0.13f;
        layer.visible = true;
        layer.selectable = true;
        layer.color = {0.9f, 0.2f, 0.2f, 1.0f};
        scene.updateLayer(layer);

        OsgSceneObject* sceneObj = scene.createSceneObject(wire, scene.getLayer("metal1"));
        scene.addObject(sceneObj);
        scene.setDisplayNamesVisible(true);

        // Dump scene graph
        osg::Group* root = scene.getObjectRoot();
        if (root) {
            for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
                dumpNode(root->getChild(i));
            }
        }

        // Find label transforms
        osg::MatrixTransform* wireMT = nullptr;
        for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
            if (root->getChild(i)->getName() == "vertical_wire") {
                wireMT = dynamic_cast<osg::MatrixTransform*>(root->getChild(i));
                break;
            }
        }

        if (!wireMT) {
            fprintf(stderr, "FAIL: vertical_wire not found in scene\n");
        } else {
            // Search for label transforms through LOD
            osg::Group* wireGroup = wireMT->asGroup();
            osg::Group* labelsGroup = nullptr;
            for (unsigned int i = 0; i < wireGroup->getNumChildren(); ++i) {
                osg::Node* child = wireGroup->getChild(i);
                if (child->getName().find(":labels") != std::string::npos) {
                    labelsGroup = child->asGroup();
                    break;
                }
            }

            if (!labelsGroup) {
                fprintf(stderr, "FAIL: labels not found for vertical_wire\n");
            } else {
                // LOD has one child: the label group
                osg::Group* innerGroup = nullptr;
                for (unsigned int i = 0; i < labelsGroup->getNumChildren(); ++i) {
                    if (labelsGroup->getChild(i)->asGroup()) {
                        innerGroup = labelsGroup->getChild(i)->asGroup();
                        break;
                    }
                }

                if (innerGroup) {
                    float zlen = 0.13f;
                    float expectedFrontZ = 0.0f;          // at front surface
                    float expectedBackZ = zlen;            // at back surface
                    float expectedZCenter = zlen * 0.5f;  // zlen/2 (for R/L faces)

                    for (unsigned int j = 0; j < innerGroup->getNumChildren(); ++j) {
                        osg::Node* labelNode = innerGroup->getChild(j);
                        if (labelNode->getName().find("labelXform:") == std::string::npos) continue;

                        osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(labelNode);
                        if (!mt) continue;

                        osg::Vec3d t = mt->getMatrix().getTrans();
                        // Local Z should be one of: -offset, zlen+offset, or zlen/2
                        bool isFrontLike = (std::abs(t.z() - expectedFrontZ) < 0.001f);
                        bool isBackLike = (std::abs(t.z() - expectedBackZ) < 0.001f);
                        bool isCenterLike = (std::abs(t.z() - expectedZCenter) < 0.001f);

                        fprintf(stderr, "  Label '%s' local Z=%.4f: front=%d back=%d center=%d\n",
                                labelNode->getName().c_str(), t.z(), isFrontLike, isBackLike, isCenterLike);

                        totalTests++;
                        if (isFrontLike || isBackLike || isCenterLike) {
                            passedTests++;
                        } else {
                            fprintf(stderr, "  FAIL: unexpected Z position\n");
                        }
                    }
                }
            }
        }

        scene.removeObject(sceneObj);
    }

    // =========================================================================
    // Test 2: Horizontal wire (X longest) - text on Front/Back (Z) and Top/Bottom (Y)
    // =========================================================================
    {
        fprintf(stderr, "\n============================================================\n");
        fprintf(stderr, "TEST 2: Horizontal wire (xlen > ylen, xlen > zlen)\n");
        fprintf(stderr, "============================================================\n");

        ObjectRecord wire;
        wire.objectId = "horizontal_wire";
        wire.type = ObjectType::Wire;
        wire.displayName = "wire_HNET";
        wire.hasBbox = true;
        wire.layerId = "metal1";

        // Wire: x from 0 to 20 (xlen=20), y from 5 to 5.2 (ylen=0.2), z from 0 to 0.13 (zlen=0.13)
        wire.bboxMin = {0.0f, 5.0f, 0.0f};
        wire.bboxMax = {20.0f, 5.2f, 0.13f};

        float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;  // 10.0
        float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;  // 5.1
        float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;  // 0.065
        wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

        OsgSceneObject* sceneObj = scene.createSceneObject(wire, scene.getLayer("metal1"));
        scene.addObject(sceneObj);
        scene.setDisplayNamesVisible(true);

        osg::Group* root = scene.getObjectRoot();

        osg::MatrixTransform* wireMT = nullptr;
        for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
            if (root->getChild(i)->getName() == "horizontal_wire") {
                wireMT = dynamic_cast<osg::MatrixTransform*>(root->getChild(i));
                break;
            }
        }

        if (!wireMT) {
            fprintf(stderr, "FAIL: horizontal_wire not found\n");
        } else {
            osg::Group* wireGroup = wireMT->asGroup();
            osg::Group* labelsGroup = nullptr;
            for (unsigned int i = 0; i < wireGroup->getNumChildren(); ++i) {
                if (wireGroup->getChild(i)->getName().find(":labels") != std::string::npos) {
                    labelsGroup = wireGroup->getChild(i)->asGroup();
                    break;
                }
            }

            if (!labelsGroup) {
                fprintf(stderr, "FAIL: labels not found for horizontal_wire\n");
            } else {
                osg::Group* innerGroup = nullptr;
                for (unsigned int i = 0; i < labelsGroup->getNumChildren(); ++i) {
                    if (labelsGroup->getChild(i)->asGroup()) {
                        innerGroup = labelsGroup->getChild(i)->asGroup();
                        break;
                    }
                }

                if (innerGroup) {
                    float zlen = 0.13f;
                    float expectedFrontZ = 0.0f;
                    float expectedBackZ = zlen;
                    float expectedZCenter = zlen * 0.5f;

                    for (unsigned int j = 0; j < innerGroup->getNumChildren(); ++j) {
                        osg::Node* labelNode = innerGroup->getChild(j);
                        if (labelNode->getName().find("labelXform:") == std::string::npos) continue;

                        osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(labelNode);
                        if (!mt) continue;

                        osg::Vec3d t = mt->getMatrix().getTrans();
                        bool isFrontLike = (std::abs(t.z() - expectedFrontZ) < 0.001f);
                        bool isBackLike = (std::abs(t.z() - expectedBackZ) < 0.001f);
                        bool isCenterLike = (std::abs(t.z() - expectedZCenter) < 0.001f);

                        fprintf(stderr, "  Label '%s' local Z=%.4f: front=%d back=%d center=%d\n",
                                labelNode->getName().c_str(), t.z(), isFrontLike, isBackLike, isCenterLike);

                        totalTests++;
                        if (isFrontLike || isBackLike || isCenterLike) {
                            passedTests++;
                        } else {
                            fprintf(stderr, "  FAIL: unexpected Z position\n");
                        }
                    }
                }
            }
        }

        scene.removeObject(sceneObj);
    }

    // =========================================================================
    // Summary
    // =========================================================================
    fprintf(stderr, "\n============================================================\n");
    fprintf(stderr, "RESULTS: %d/%d tests passed\n", passedTests, totalTests);
    fprintf(stderr, "============================================================\n");

    if (totalTests == 0 || passedTests != totalTests) {
        fprintf(stderr, "SOME TESTS FAILED\n");
        return 1;
    }

    fprintf(stderr, "ALL TESTS PASSED\n");
    return 0;
}

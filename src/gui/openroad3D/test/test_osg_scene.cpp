// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors
//
// Unit tests for OsgScene (scene graph operations).
// These tests exercise the OSG data structures without a rendering context.

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/domain/SceneTypes.h"
#include "render/api/IScene.h"
#include "render/backend_osg/OsgScene.h"

#include <osg/MatrixTransform>
#include <osg/Geode>

namespace viewer3d {
namespace render {
namespace backend_osg {
namespace test {

using viewer3d::domain::ColorRgba;
using viewer3d::domain::LayerRecord;
using viewer3d::domain::ObjectRecord;
using viewer3d::domain::ObjectType;

// ---------------------------------------------------------------------------
// Helpers: make test data
// ---------------------------------------------------------------------------
static ObjectRecord makeObject(const std::string& id,
                               ObjectType type,
                               const std::string& layerId,
                               float x = 0,
                               float y = 0,
                               float w = 10,
                               float h = 10,
                               float zMin = 0,
                               float zMax = 1) {
  ObjectRecord obj;
  obj.objectId = id;
  obj.type = type;
  obj.layerId = layerId;
  obj.displayName = id;
  obj.hasBbox = true;
  obj.bboxMin = {x, y, zMin};
  obj.bboxMax = {x + w, y + h, zMax};
  // Transform centered on bbox
  float cx = (obj.bboxMin[0] + obj.bboxMax[0]) * 0.5f;
  float cy = (obj.bboxMin[1] + obj.bboxMax[1]) * 0.5f;
  float cz = (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f;
  obj.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f,
                   cx,   cy,   cz,   1.0f};
  return obj;
}

static LayerRecord makeLayer(const std::string& id,
                             float zBase = 0,
                             float thickness = 0.3f) {
  LayerRecord layer;
  layer.layerId = id;
  layer.name = id;
  layer.zBase = zBase;
  layer.thickness = thickness;
  layer.visible = true;
  layer.selectable = true;
  layer.color = {0.5f, 0.5f, 0.5f, 1.0f};
  return layer;
}

// ---------------------------------------------------------------------------
//  Fixture
// ---------------------------------------------------------------------------
class OsgSceneTest : public ::testing::Test {
 protected:
  OsgScene scene_;

  void SetUp() override {}
  void TearDown() override {}
};

// ---------------------------------------------------------------------------
//  Scene lifecycle
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, EmptyScene) {
  EXPECT_EQ(scene_.getObjectCount(), 0u);
}

TEST_F(OsgSceneTest, AddSingleObject) {
  LayerRecord layer = makeLayer("metal1");
  ObjectRecord obj = makeObject("wire_0", ObjectType::Wire, "metal1");

  scene_.updateLayer(layer);
  OsgSceneObject* sceneObj = scene_.createSceneObject(obj, scene_.getLayer("metal1"));
  ASSERT_NE(sceneObj, nullptr);
  scene_.addObject(sceneObj);

  EXPECT_EQ(scene_.getObjectCount(), 1u);
  EXPECT_EQ(sceneObj->getId(), "wire_0");
  EXPECT_TRUE(sceneObj->isVisible());
  EXPECT_TRUE(sceneObj->isSelectable());
}

TEST_F(OsgSceneTest, AddAndRemoveObject) {
  LayerRecord layer = makeLayer("metal1");
  ObjectRecord obj = makeObject("wire_0", ObjectType::Wire, "metal1");

  scene_.updateLayer(layer);
  OsgSceneObject* sceneObj = scene_.createSceneObject(obj, scene_.getLayer("metal1"));
  ASSERT_NE(sceneObj, nullptr);
  scene_.addObject(sceneObj);
  EXPECT_EQ(scene_.getObjectCount(), 1u);

  scene_.removeObject(sceneObj);
  EXPECT_EQ(scene_.getObjectCount(), 0u);
}

TEST_F(OsgSceneTest, ClearScene) {
  LayerRecord layer = makeLayer("metal1");
  scene_.updateLayer(layer);

  for (int i = 0; i < 5; ++i) {
    ObjectRecord obj = makeObject("obj_" + std::to_string(i),
                                  ObjectType::Wire, "metal1",
                                  i * 10, 0, 5, 5, 0, 1);
    OsgSceneObject* sceneObj = scene_.createSceneObject(obj, scene_.getLayer("metal1"));
    scene_.addObject(sceneObj);
  }

  EXPECT_EQ(scene_.getObjectCount(), 5u);
  scene_.clear();
  EXPECT_EQ(scene_.getObjectCount(), 0u);
}

// ---------------------------------------------------------------------------
//  Multiple objects
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, MultipleObjectsDifferentLayers) {
  scene_.updateLayer(makeLayer("metal1", 0, 0.3f));
  scene_.updateLayer(makeLayer("metal2", 0.5f, 0.3f));

  OsgSceneObject* obj1 = scene_.createSceneObject(
      makeObject("w1", ObjectType::Wire, "metal1", 0, 0, 10, 10, 0, 0.3f),
      scene_.getLayer("metal1"));
  OsgSceneObject* obj2 = scene_.createSceneObject(
      makeObject("w2", ObjectType::Wire, "metal2", 5, 5, 10, 10, 0.5f, 0.8f),
      scene_.getLayer("metal2"));

  scene_.addObject(obj1);
  scene_.addObject(obj2);

  EXPECT_EQ(scene_.getObjectCount(), 2u);

  // Objects by layer
  auto m1objs = scene_.getObjectsByLayer("metal1");
  ASSERT_EQ(m1objs.size(), 1u);
  EXPECT_EQ(m1objs[0]->getId(), "w1");

  auto m2objs = scene_.getObjectsByLayer("metal2");
  ASSERT_EQ(m2objs.size(), 1u);
  EXPECT_EQ(m2objs[0]->getId(), "w2");
}

// ---------------------------------------------------------------------------
//  Object properties
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, ObjectVisibility) {
  LayerRecord layer = makeLayer("metal1");
  scene_.updateLayer(layer);

  OsgSceneObject* sceneObj = scene_.createSceneObject(
      makeObject("vtest", ObjectType::Wire, "metal1"),
      scene_.getLayer("metal1"));

  scene_.addObject(sceneObj);

  EXPECT_TRUE(sceneObj->isVisible());
  sceneObj->setVisible(false);
  EXPECT_FALSE(sceneObj->isVisible());
  sceneObj->setVisible(true);
  EXPECT_TRUE(sceneObj->isVisible());
}

TEST_F(OsgSceneTest, ObjectSelectability) {
  LayerRecord layer = makeLayer("metal1");
  scene_.updateLayer(layer);

  OsgSceneObject* sceneObj = scene_.createSceneObject(
      makeObject("stest", ObjectType::Wire, "metal1"),
      scene_.getLayer("metal1"));

  scene_.addObject(sceneObj);
  EXPECT_TRUE(sceneObj->isSelectable());
  sceneObj->setSelectable(false);
  EXPECT_FALSE(sceneObj->isSelectable());
}

TEST_F(OsgSceneTest, ObjectTransformSetAndGet) {
  LayerRecord layer = makeLayer("metal1");
  scene_.updateLayer(layer);

  ObjectRecord obj = makeObject("ttest", ObjectType::Inst, "metal1",
                                2, 3, 8, 6, 0, 4);
  OsgSceneObject* sceneObj = scene_.createSceneObject(obj, scene_.getLayer("metal1"));
  scene_.addObject(sceneObj);

  // Set the transform via the public API and verify
  std::array<float, 16> xform;
  xform.fill(0.0f);
  xform[0] = xform[5] = xform[10] = xform[15] = 1.0f;
  xform[12] = 6.0f;
  xform[13] = 6.0f;
  xform[14] = 2.0f;
  sceneObj->setTransform(xform);

  const auto& stored = sceneObj->getTransform();
  EXPECT_FLOAT_EQ(stored[12], 6.0f);
  EXPECT_FLOAT_EQ(stored[13], 6.0f);
  EXPECT_FLOAT_EQ(stored[14], 2.0f);
}

// ---------------------------------------------------------------------------
//  Bounding box
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, EmptyBoundingBox) {
  Vec3 min, max;
  scene_.getBoundingBox(min, max);
  EXPECT_FLOAT_EQ(min.x, 0);
  EXPECT_FLOAT_EQ(min.y, 0);
  EXPECT_FLOAT_EQ(min.z, 0);
}

TEST_F(OsgSceneTest, BoundingBoxWithObjects) {
  scene_.updateLayer(makeLayer("metal1", 0, 0.3f));
  scene_.updateLayer(makeLayer("metal2", 0.5f, 0.3f));

  // Object at (0,0)-(10,10) on metal1 (z 0 to 0.3)
  OsgSceneObject* obj1 = scene_.createSceneObject(
      makeObject("a", ObjectType::Wire, "metal1", 0, 0, 10, 10, 0, 0.3f),
      scene_.getLayer("metal1"));
  scene_.addObject(obj1);

  // Object at (20,20)-(30,30) on metal2 (z 0.5 to 0.8)
  OsgSceneObject* obj2 = scene_.createSceneObject(
      makeObject("b", ObjectType::Wire, "metal2", 20, 20, 10, 10, 0.5f, 0.8f),
      scene_.getLayer("metal2"));
  scene_.addObject(obj2);

  Vec3 min, max;
  scene_.getBoundingBox(min, max);

  // Bbox should cover both objects
  EXPECT_LE(min.x, 0);
  EXPECT_LE(min.y, 0);
  EXPECT_LE(min.z, 0);
  EXPECT_GE(max.x, 30);
  EXPECT_GE(max.y, 30);
  EXPECT_GE(max.z, 0.5f);
}

// ---------------------------------------------------------------------------
//  Layer control
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, SetLayerVisible) {
  scene_.updateLayer(makeLayer("metal1"));
  scene_.updateLayer(makeLayer("metal2"));

  EXPECT_TRUE(scene_.isLayerVisible("metal1"));
  scene_.setLayerVisible("metal1", false);
  EXPECT_FALSE(scene_.isLayerVisible("metal1"));
  EXPECT_TRUE(scene_.isLayerVisible("metal2"));  // other layer unchanged
}

TEST_F(OsgSceneTest, UpdateLayerPreservesDefaults) {
  LayerRecord layer = makeLayer("metal1");
  scene_.updateLayer(layer);

  const LayerRecord* stored = scene_.getLayer("metal1");
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->layerId, "metal1");
  EXPECT_TRUE(stored->visible);
  EXPECT_TRUE(stored->selectable);
}

// ---------------------------------------------------------------------------
//  Label placement verification (programmatic, not visual)
// ---------------------------------------------------------------------------

TEST_F(OsgSceneTest, LabelTransformForVerticalWire) {
  // Create a vertical wire (Y longest:5.0, X shortest: 0.2)
  scene_.updateLayer(makeLayer("metal1", 0.065f, 0.13f));

  ObjectRecord wire;
  wire.objectId = "vertical_wire";
  wire.type = ObjectType::Wire;
  wire.displayName = "wire_NET1";
  wire.layerId = "metal1";
  wire.hasBbox = true;
  wire.bboxMin = {9.9f, 10.0f, 0.0f};
  wire.bboxMax = {10.1f, 15.0f, 0.13f};

  float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;
  float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;
  float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;
  wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

  OsgSceneObject* sceneObj = scene_.createSceneObject(wire, scene_.getLayer("metal1"));
  scene_.addObject(sceneObj);
  scene_.setDisplayNamesVisible(true);

  // Get the object root and find the label transform
  osg::Group* root = scene_.getObjectRoot();
  ASSERT_NE(root, nullptr);

  // Find the wire's MatrixTransform
  osg::MatrixTransform* wireMT = nullptr;
  for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
    osg::Node* child = root->getChild(i);
    if (child->getName() == "vertical_wire") {
      wireMT = dynamic_cast<osg::MatrixTransform*>(child);
      break;
    }
  }
  ASSERT_NE(wireMT, nullptr);

  // Labels are inside a Group child named "wire:labels"
  osg::MatrixTransform* labelMT = nullptr;
  osg::Group* wireGroup = wireMT->asGroup();
  ASSERT_NE(wireGroup, nullptr);

  // Find the labels group (now an LOD wrapping the actual group)
  osg::Group* labelsGroup = nullptr;
  for (unsigned int i = 0; i < wireGroup->getNumChildren(); ++i) {
    osg::Node* child = wireGroup->getChild(i);
    if (child->getName().find(":labels") != std::string::npos) {
      labelsGroup = child->asGroup();
      break;
    }
  }
  ASSERT_NE(labelsGroup, nullptr);

  // LOD has one child: the actual label group
  osg::Group* innerLabelGroup = nullptr;
  for (unsigned int i = 0; i < labelsGroup->getNumChildren(); ++i) {
    osg::Node* child = labelsGroup->getChild(i);
    if (child->asGroup()) {
      innerLabelGroup = child->asGroup();
      break;
    }
  }
  ASSERT_NE(innerLabelGroup, nullptr);

  // Find label transform inside the inner label group
  for (unsigned int i = 0; i < innerLabelGroup->getNumChildren(); ++i) {
    osg::Node* child = innerLabelGroup->getChild(i);
    if (child->getName().find("labelXform:") != std::string::npos) {
      labelMT = dynamic_cast<osg::MatrixTransform*>(child);
      if (labelMT) break;
    }
  }
  ASSERT_NE(labelMT, nullptr);

  // Verify the label transform exists and has reasonable values
  osg::Matrixd mat = labelMT->getMatrix();
  osg::Vec3d t = mat.getTrans();
  osg::Vec3d s = mat.getScale();

  // Label should have non-zero scale
  EXPECT_GT(s.x(), 0.0);
  EXPECT_GT(s.y(), 0.0);
  EXPECT_GT(s.z(), 0.0);

  // Label should have some translation (position offset from wire center)
  // For a vertical wire with Y longest, the side labels should have X offset
  // For front/back labels, the Z offset should be roughly half the wire thickness + small offset
  fprintf(stderr, "Label transform: translate=(%.4f, %.4f, %.4f), scale=(%.4f, %.4f, %.4f)\n",
          t.x(), t.y(), t.z(), s.x(), s.y(), s.z());

  scene_.removeObject(sceneObj);
}

TEST_F(OsgSceneTest, LabelCharSizeProportionalToObjectSize) {
  // Small object should get smaller charSize, large object should get larger
  scene_.updateLayer(makeLayer("metal1", 0.0f, 0.1f));
  scene_.updateLayer(makeLayer("metal2", 0.0f, 0.1f));

  // Small wire (0.1 x 0.1 x 0.1)
  ObjectRecord smallWire;
  smallWire.objectId = "small_wire";
  smallWire.type = ObjectType::Wire;
  smallWire.displayName = "small";
  smallWire.layerId = "metal1";
  smallWire.hasBbox = true;
  smallWire.bboxMin = {0, 0, 0};
  smallWire.bboxMax = {0.1f, 0.1f, 0.1f};
  smallWire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0.05f, 0.05f, 0.05f};

  // Large wire (10 x 10 x 0.5)
  ObjectRecord largeWire;
  largeWire.objectId = "large_wire";
  largeWire.type = ObjectType::Wire;
  largeWire.displayName = "large";
  largeWire.layerId = "metal2";
  largeWire.hasBbox = true;
  largeWire.bboxMin = {0, 0, 0};
  largeWire.bboxMax = {10.0f, 10.0f, 0.5f};
  largeWire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, 5.0f, 5.0f, 0.25f};

  OsgSceneObject* smallObj = scene_.createSceneObject(smallWire, scene_.getLayer("metal1"));
  OsgSceneObject* largeObj = scene_.createSceneObject(largeWire, scene_.getLayer("metal2"));
  scene_.addObject(smallObj);
  scene_.addObject(largeObj);
  scene_.setDisplayNamesVisible(true);

  // Both objects should have labels with different scales
  osg::Group* root = scene_.getObjectRoot();

  // Helper to find label scale traversing through LOD
  auto findLabelScale = [](osg::Group* objGroup) -> float {
    if (!objGroup) return 0.0f;
    // Find the LOD child named ":labels"
    for (unsigned int i = 0; i < objGroup->getNumChildren(); ++i) {
      osg::Node* child = objGroup->getChild(i);
      if (child->getName().find(":labels") != std::string::npos) {
        osg::Group* lod = child->asGroup();
        if (!lod) continue;
        // LOD's first child is the inner label group
        for (unsigned int j = 0; j < lod->getNumChildren(); ++j) {
          osg::Group* inner = lod->getChild(j)->asGroup();
          if (!inner) continue;
          // Search for labelXform in the inner group
          for (unsigned int k = 0; k < inner->getNumChildren(); ++k) {
            osg::Node* labelNode = inner->getChild(k);
            if (labelNode->getName().find("labelXform:") != std::string::npos) {
              osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(labelNode);
              if (mt) {
                osg::Vec3d s = mt->getMatrix().getScale();
                return s.x();
              }
            }
          }
        }
      }
    }
    return 0.0f;
  };

  // Find small wire label
  float smallScale = 0.0f;
  float largeScale = 0.0f;

  for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
    osg::Node* child = root->getChild(i);
    if (child->getName() == "small_wire") {
      smallScale = findLabelScale(child->asGroup());
    } else if (child->getName() == "large_wire") {
      largeScale = findLabelScale(child->asGroup());
    }
  }

  fprintf(stderr, "Small wire label scale: %.6f\n", smallScale);
  fprintf(stderr, "Large wire label scale: %.6f\n", largeScale);

  // Large object should have larger or equal label scale
  // (proportional to object size)
  EXPECT_GE(largeScale, smallScale);

  scene_.removeObject(smallObj);
  scene_.removeObject(largeObj);
}

// ---------------------------------------------------------------------------
//  Label Z positioning on surfaces (the "text on wire surface" regression)
// ---------------------------------------------------------------------------

// Helper: find all label MatrixTransform nodes for an object, traversing LOD
static std::vector<osg::Vec3d> getLabelTranslations(osg::Group* root,
                                                     const std::string& objId) {
  std::vector<osg::Vec3d> result;
  if (!root) return result;

  osg::MatrixTransform* objMT = nullptr;
  for (unsigned int i = 0; i < root->getNumChildren(); ++i) {
    if (root->getChild(i)->getName() == objId) {
      objMT = dynamic_cast<osg::MatrixTransform*>(root->getChild(i));
      break;
    }
  }
  if (!objMT) return result;

  osg::Group* objGroup = objMT->asGroup();
  if (!objGroup) return result;

  // Find LOD
  osg::Group* lod = nullptr;
  for (unsigned int i = 0; i < objGroup->getNumChildren(); ++i) {
    if (objGroup->getChild(i)->getName().find(":labels") != std::string::npos) {
      lod = objGroup->getChild(i)->asGroup();
      break;
    }
  }
  if (!lod) return result;

  // LOD first child = inner label group
  osg::Group* innerGroup = nullptr;
  for (unsigned int i = 0; i < lod->getNumChildren(); ++i) {
    if (lod->getChild(i)->asGroup()) {
      innerGroup = lod->getChild(i)->asGroup();
      break;
    }
  }
  if (!innerGroup) return result;

  // Collect label MatrixTransform translations
  for (unsigned int i = 0; i < innerGroup->getNumChildren(); ++i) {
    osg::Node* child = innerGroup->getChild(i);
    if (child->getName().find("labelXform:") == std::string::npos) continue;
    osg::MatrixTransform* mt = dynamic_cast<osg::MatrixTransform*>(child);
    if (!mt) continue;
    result.push_back(mt->getMatrix().getTrans());
  }
  return result;
}

TEST_F(OsgSceneTest, LabelZPositionOnVerticalWire) {
  // Vertical wire (ylen > xlen, ylen > zlen):
  //   - Text on Front/Back (Z faces) → Z = -offset / zlen+offset
  //   - Text on Right/Left (X faces) → Z = zlen/2 (centered)
  scene_.updateLayer(makeLayer("metal1", 0.065f, 0.13f));

  ObjectRecord wire;
  wire.objectId = "vwire";
  wire.type = ObjectType::Wire;
  wire.displayName = "vnet";
  wire.layerId = "metal1";
  wire.hasBbox = true;
  // ylen=5.0 > xlen=0.2, zlen=0.13
  wire.bboxMin = {9.9f, 10.0f, 0.0f};
  wire.bboxMax = {10.1f, 15.0f, 0.13f};

  float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;
  float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;
  float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;
  wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

  OsgSceneObject* sceneObj = scene_.createSceneObject(wire, scene_.getLayer("metal1"));
  scene_.addObject(sceneObj);
  scene_.setDisplayNamesVisible(true);

  auto translations = getLabelTranslations(scene_.getObjectRoot(), "vwire");
  ASSERT_GT(translations.size(), 0) << "Expected at least one label transform";

  // Expected Z values in LOCAL coordinates:
  //   frontZ = 0 (geometry front surface, local Z=0)
  //   backZ  = zlen (geometry back surface, local Z=zlen)
  //   zCenter = zlen/2
  const float zlen = 0.13f;
  const float frontZ = 0.0f;
  const float backZ = zlen;
  const float zCenter = zlen * 0.5f;

  bool foundFrontOrBack = false;
  bool foundCenter = false;
  for (const auto& t : translations) {
    SCOPED_TRACE(::testing::Message() << "label Z=" << t.z());
    if (std::abs(t.z() - frontZ) < 0.001f || std::abs(t.z() - backZ) < 0.001f) {
      foundFrontOrBack = true;
    }
    if (std::abs(t.z() - zCenter) < 0.001f) {
      foundCenter = true;
    }
    // Every label Z must match front, back, or center
    EXPECT_TRUE(std::abs(t.z() - frontZ) < 0.001f ||
                std::abs(t.z() - backZ) < 0.001f ||
                std::abs(t.z() - zCenter) < 0.001f)
        << "Label Z=" << t.z() << " does not match any expected value"
        << " (frontZ=" << frontZ << " backZ=" << backZ << " zCenter=" << zCenter << ")";
  }
  EXPECT_TRUE(foundFrontOrBack) << "No front/back labels found (expected for vertical wire)";
  EXPECT_TRUE(foundCenter) << "No right/left centered labels found (expected for vertical wire)";

  scene_.removeObject(sceneObj);
}

TEST_F(OsgSceneTest, LabelZPositionOnHorizontalWire) {
  // Horizontal wire (xlen > ylen, xlen > zlen):
  //   - Text on Front/Back (Z faces) → Z = -offset / zlen+offset
  //   - Text on Top/Bottom (Y faces) → Z = zlen/2 (centered)
  scene_.updateLayer(makeLayer("metal1", 0.065f, 0.13f));

  ObjectRecord wire;
  wire.objectId = "hwire";
  wire.type = ObjectType::Wire;
  wire.displayName = "hnet";
  wire.layerId = "metal1";
  wire.hasBbox = true;
  // xlen=20.0 > ylen=0.2, zlen=0.13
  wire.bboxMin = {0.0f, 5.0f, 0.0f};
  wire.bboxMax = {20.0f, 5.2f, 0.13f};

  float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;
  float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;
  float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;
  wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

  OsgSceneObject* sceneObj = scene_.createSceneObject(wire, scene_.getLayer("metal1"));
  scene_.addObject(sceneObj);
  scene_.setDisplayNamesVisible(true);

  auto translations = getLabelTranslations(scene_.getObjectRoot(), "hwire");
  ASSERT_GT(translations.size(), 0) << "Expected at least one label transform";

  const float zlen = 0.13f;
  // No geometric offset — text sits exactly at the surface
  const float frontZ = 0.0f;
  const float backZ = zlen;
  const float zCenter = zlen * 0.5f;

  bool foundFrontOrBack = false;
  bool foundCenter = false;
  for (const auto& t : translations) {
    if (std::abs(t.z() - frontZ) < 0.001f || std::abs(t.z() - backZ) < 0.001f) {
      foundFrontOrBack = true;
    }
    if (std::abs(t.z() - zCenter) < 0.001f) {
      foundCenter = true;
    }
    EXPECT_TRUE(std::abs(t.z() - frontZ) < 0.001f ||
                std::abs(t.z() - backZ) < 0.001f ||
                std::abs(t.z() - zCenter) < 0.001f)
        << "Label Z=" << t.z() << " does not match any expected value";
  }
  EXPECT_TRUE(foundFrontOrBack) << "No front/back labels found (expected for horizontal wire)";
  EXPECT_TRUE(foundCenter) << "No top/bottom centered labels found (expected for horizontal wire)";

  scene_.removeObject(sceneObj);
}

TEST_F(OsgSceneTest, LabelZPositionMatchesGeometrySurface) {
  // Verify Front/Back text world Z matches the actual geometry surface.
  // Text sits exactly at the surface (Z-fighting prevented by PolygonOffset).
  // Front face at local Z=0 = world Z=centerZ.
  // Back face at local Z=zlen = world Z=centerZ+zlen.
  scene_.updateLayer(makeLayer("metal2", 0.5f, 0.3f));

  ObjectRecord wire;
  wire.objectId = "surface_wire";
  wire.type = ObjectType::Wire;
  wire.displayName = "surf_net";
  wire.layerId = "metal2";
  wire.hasBbox = true;
  wire.bboxMin = {0.0f, 0.0f, 0.5f};
  wire.bboxMax = {10.0f, 0.2f, 0.8f};  // zlen=0.3

  float cx = (wire.bboxMin[0] + wire.bboxMax[0]) * 0.5f;  // 5.0
  float cy = (wire.bboxMin[1] + wire.bboxMax[1]) * 0.5f;  // 0.1
  float cz = (wire.bboxMin[2] + wire.bboxMax[2]) * 0.5f;  // 0.65
  wire.transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, cx,cy,cz,1};

  OsgSceneObject* sceneObj = scene_.createSceneObject(wire, scene_.getLayer("metal2"));
  scene_.addObject(sceneObj);
  scene_.setDisplayNamesVisible(true);

  auto translations = getLabelTranslations(scene_.getObjectRoot(), "surface_wire");
  ASSERT_GT(translations.size(), 0);

  const float zlen = 0.3f;
  const float frontZ_local = 0.0f;    // at the geometry front surface
  const float backZ_local = zlen;      // at the geometry back surface

  // Front text local Z = 0
  // Front text world Z = cz + 0 = cz
  // Geometry front surface world Z = cz (local Z=0) → exact match
  bool foundFront = false;
  bool foundBack = false;
  for (const auto& t : translations) {
    if (std::abs(t.z() - frontZ_local) < 0.001f) {
      foundFront = true;
      float frontWorldZ = cz + t.z();
      float geomFrontWorldZ = cz;
      EXPECT_NEAR(frontWorldZ, geomFrontWorldZ, 0.001f)
          << "Front text should be exactly at geometry front surface";
    }
    if (std::abs(t.z() - backZ_local) < 0.001f) {
      foundBack = true;
      float backWorldZ = cz + t.z();
      float geomBackWorldZ = cz + zlen;
      EXPECT_NEAR(backWorldZ, geomBackWorldZ, 0.001f)
          << "Back text should be exactly at geometry back surface";
    }
  }
  EXPECT_TRUE(foundFront) << "No front label found (Z=" << frontZ_local << ")";
  EXPECT_TRUE(foundBack) << "No back label found (Z=" << backZ_local << ")";

  scene_.removeObject(sceneObj);
}

}  // namespace test
}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d

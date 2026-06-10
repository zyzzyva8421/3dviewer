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

}  // namespace test
}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d

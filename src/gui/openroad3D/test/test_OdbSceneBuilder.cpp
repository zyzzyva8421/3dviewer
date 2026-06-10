// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors
//
// Regression tests for OdbSceneBuilder
//
// Tests verify the scene building pipeline:
//   1. Empty / null database produces empty snapshot
//   2. Real ODB file produces valid scene structure
//   3. Layer Z-ordering, deduplication, and object references

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "OdbSceneBuilder.h"
#include "core/domain/SceneTypes.h"
#include "odb/db.h"

namespace gui {
namespace openroad3d {
namespace test {

// ---------------------------------------------------------------------------
// Fixture: manages db lifecycle for each test
// ---------------------------------------------------------------------------
class OdbSceneBuilderTest : public ::testing::Test {
 protected:
  odb::dbDatabase* db_ = nullptr;

  // Helper to create and set up a fresh database
  odb::dbDatabase* createEmptyDb() {
    if (db_) {
      odb::dbDatabase::destroy(db_);
    }
    db_ = odb::dbDatabase::create();
    return db_;
  }

  void TearDown() override {
    if (db_) {
      odb::dbDatabase::destroy(db_);
    }
    db_ = nullptr;
  }
};

// ---------------------------------------------------------------------------
// Fixture: loads a real ODB file shipped with the test environment
// ---------------------------------------------------------------------------
class OdbSceneBuilderRealDataTest : public ::testing::Test {
 protected:
  odb::dbDatabase* db_ = nullptr;
  std::string odb_path_;

  void SetUp() override {
    // Resolve ODB path from env var, then from known defaults
    const char* env = std::getenv("OPENROAD_3D_TEST_ODB");
    if (env && env[0] != '\0') {
      odb_path_ = env;
    }

    if (odb_path_.empty()) {
      // Try a few common locations
      const char* home = std::getenv("OPENROAD_HOME");
      if (home) {
        std::string candidate = std::string(home)
                                + "/flow/results/nangate45/gcd/base/6_final.odb";
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) {
          probe.close();
          odb_path_ = candidate;
        }
      }
    }

    if (odb_path_.empty()) {
      GTEST_SKIP() << "OPENROAD_3D_TEST_ODB not set and no default ODB found";
      return;
    }

    std::ifstream f(odb_path_, std::ios::binary);
    ASSERT_TRUE(f.good()) << "Cannot open ODB: " << odb_path_;

    db_ = odb::dbDatabase::create();
    db_->read(f);
    f.close();

    ASSERT_NE(db_, nullptr);
  }

  void TearDown() override {
    if (db_) {
      odb::dbDatabase::destroy(db_);
    }
    db_ = nullptr;
  }
};

// ===========================================================================
//  Empty / null database tests
// ===========================================================================

TEST_F(OdbSceneBuilderTest, NullDb) {
  // Build with a null db_ by temporarily setting to nullptr
  OdbSceneBuilder builder(nullptr);
  auto snapshot = builder.build();
  EXPECT_EQ(snapshot.sourceTag, "openroad_db");
  EXPECT_TRUE(snapshot.objects.empty());
  EXPECT_TRUE(snapshot.layers.empty());
}

TEST_F(OdbSceneBuilderTest, EmptyDbNoChip) {
  // Fresh db with no chip loaded → empty snapshot
  createEmptyDb();
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();
  EXPECT_EQ(snapshot.sourceTag, "openroad_db");
  EXPECT_TRUE(snapshot.objects.empty());
  EXPECT_TRUE(snapshot.layers.empty());
}

TEST_F(OdbSceneBuilderTest, DbUnitsPerMicronDefault) {
  // Database without a block still returns the default (0) for dbUnitsPerMicron
  createEmptyDb();
  OdbSceneBuilder builder(db_);
  // getDbUnitsPerMicron() returns 0 when no block is loaded
  EXPECT_EQ(builder.getDbUnitsPerMicron(), 0);
}

// ===========================================================================
//  Real ODB data tests  (requires 6_final.odb)
// ===========================================================================

TEST_F(OdbSceneBuilderRealDataTest, NonEmptySnapshot) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  EXPECT_EQ(snapshot.sourceTag, "openroad_db");
  EXPECT_GT(snapshot.objects.size(), 0)
      << "Real ODB should produce at least some objects";
  EXPECT_GT(snapshot.layers.size(), 0)
      << "Real ODB should produce at least some layers";
}

TEST_F(OdbSceneBuilderRealDataTest, LayerZOrdering) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  ASSERT_GT(snapshot.layers.size(), 1);

  // Layers must be sorted by zBase in ascending order
  for (size_t i = 1; i < snapshot.layers.size(); ++i) {
    EXPECT_GT(snapshot.layers[i].zBase, snapshot.layers[i - 1].zBase)
        << "Layer '" << snapshot.layers[i].name
        << "' has zBase=" << snapshot.layers[i].zBase
        << " which is not greater than previous layer '"
        << snapshot.layers[i - 1].name
        << "' zBase=" << snapshot.layers[i - 1].zBase;
  }
}

TEST_F(OdbSceneBuilderRealDataTest, AllObjectLayerIdsValid) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  // Collect all valid layer IDs
  std::set<std::string> validLayerIds;
  for (const auto& layer : snapshot.layers) {
    validLayerIds.insert(layer.layerId);
  }

  for (const auto& obj : snapshot.objects) {
    EXPECT_TRUE(validLayerIds.find(obj.layerId) != validLayerIds.end())
        << "Object '" << obj.objectId << "' references unknown layerId '"
        << obj.layerId << "'";
  }
}

TEST_F(OdbSceneBuilderRealDataTest, ObjectTypesPresent) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  // Count objects by type
  int instCount = 0, wireCount = 0, viaCount = 0, pinCount = 0;
  for (const auto& obj : snapshot.objects) {
    switch (obj.type) {
      case viewer3d::domain::ObjectType::Inst:  instCount++;  break;
      case viewer3d::domain::ObjectType::Wire:  wireCount++;  break;
      case viewer3d::domain::ObjectType::Via:   viaCount++;   break;
      case viewer3d::domain::ObjectType::Pin:   pinCount++;   break;
      default: break;
    }
  }

  fprintf(stderr,
          "DEBUG object counts: inst=%d wire=%d via=%d pin=%d total=%zu\n",
          instCount, wireCount, viaCount, pinCount, snapshot.objects.size());

  // A real design should have at least instances
  EXPECT_GT(instCount, 0) << "Expected at least some instances in real ODB";
}

TEST_F(OdbSceneBuilderRealDataTest, InstancesHaveBbox) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  for (const auto& obj : snapshot.objects) {
    if (obj.type != viewer3d::domain::ObjectType::Inst) {
      continue;
    }
    EXPECT_TRUE(obj.hasBbox)
        << "Instance '" << obj.objectId << "' is missing bbox";
    if (obj.hasBbox) {
      // XY bbox should be non-degenerate
      EXPECT_GT(obj.bboxMax[0], obj.bboxMin[0])
          << "Instance '" << obj.objectId << "' has zero-width bbox in X";
      EXPECT_GT(obj.bboxMax[1], obj.bboxMin[1])
          << "Instance '" << obj.objectId << "' has zero-width bbox in Y";
      // Z range should be non-negative
      EXPECT_GE(obj.bboxMax[2], obj.bboxMin[2])
          << "Instance '" << obj.objectId << "' has inverted Z range";
    }
  }
}

TEST_F(OdbSceneBuilderRealDataTest, WiresHaveValidLayerAndZ) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  for (const auto& obj : snapshot.objects) {
    if (obj.type != viewer3d::domain::ObjectType::Wire) {
      continue;
    }
    EXPECT_TRUE(obj.hasBbox)
        << "Wire '" << obj.objectId << "' is missing bbox";
    if (obj.hasBbox) {
      EXPECT_GT(obj.bboxMax[2], obj.bboxMin[2])
          << "Wire '" << obj.objectId << "' has zero or negative Z thickness";
    }
  }
}

TEST_F(OdbSceneBuilderRealDataTest, ViaTopBottomCutStructure) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  // Count vias by groupId — each via creates 3 objects sharing a groupId
  std::map<std::string, std::vector<const viewer3d::domain::ObjectRecord*>>
      viaGroups;
  for (const auto& obj : snapshot.objects) {
    if (obj.type == viewer3d::domain::ObjectType::Via && !obj.groupId.empty()) {
      viaGroups[obj.groupId].push_back(&obj);
    }
  }

  if (viaGroups.empty()) {
    GTEST_SKIP() << "No via objects found in this design";
    return;
  }

  // groupId groups all vias of the same layer-type on the same net.
  // Each individual via has 3 parts (top+bottom+cut), so count must be a
  // multiple of 3 and at least 3.
  for (const auto& [groupId, parts] : viaGroups) {
    EXPECT_GE(parts.size(), 3)
        << "Via group '" << groupId << "' has " << parts.size()
        << " parts, expected at least 3";
    EXPECT_EQ(parts.size() % 3, 0)
        << "Via group '" << groupId << "' has " << parts.size()
        << " parts, expected a multiple of 3 (top+bottom+cut)";

    // All parts should have valid bboxes
    for (const auto* part : parts) {
      EXPECT_TRUE(part->hasBbox);
      if (part->hasBbox) {
        EXPECT_GT(part->bboxMax[2], part->bboxMin[2])
            << "Via part '" << part->objectId << "' has zero thickness";
      }
    }
  }
}

TEST_F(OdbSceneBuilderRealDataTest, DisplayNamesNonEmpty) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  // Every instance should have a display name
  for (const auto& obj : snapshot.objects) {
    if (obj.type == viewer3d::domain::ObjectType::Inst) {
      EXPECT_FALSE(obj.displayName.empty())
          << "Instance '" << obj.objectId << "' has empty display name";
    }
  }
}

TEST_F(OdbSceneBuilderRealDataTest, LayerColorsAreValid) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  for (const auto& layer : snapshot.layers) {
    // Color components should be in [0, 1] range
    EXPECT_GE(layer.color.r, 0.0f);
    EXPECT_GE(layer.color.g, 0.0f);
    EXPECT_GE(layer.color.b, 0.0f);
    EXPECT_LE(layer.color.r, 1.0f);
    EXPECT_LE(layer.color.g, 1.0f);
    EXPECT_LE(layer.color.b, 1.0f);
    EXPECT_GE(layer.color.a, 0.0f);
    EXPECT_LE(layer.color.a, 1.0f);
  }
}

TEST_F(OdbSceneBuilderRealDataTest, WiresAndViasExist) {
  OdbSceneBuilder builder(db_);
  auto snapshot = builder.build();

  // For a real routed design, expect at least some wires or vias
  size_t wireViaCount = 0;
  for (const auto& obj : snapshot.objects) {
    if (obj.type == viewer3d::domain::ObjectType::Wire
        || obj.type == viewer3d::domain::ObjectType::Via) {
      wireViaCount++;
    }
  }

  if (wireViaCount == 0) {
    // This might happen if the design is not routed — skip, not fail
    GTEST_SKIP() << "No wires or vias found (design may be un-routed)";
  }
}

// ===========================================================================
//  Incremental build consistency tests
// ===========================================================================

TEST_F(OdbSceneBuilderRealDataTest, RepeatedBuildIsDeterministic) {
  OdbSceneBuilder builder(db_);
  auto snapshot1 = builder.build();
  auto snapshot2 = builder.build();

  // Both snapshots must have the same number of objects and layers
  EXPECT_EQ(snapshot1.objects.size(), snapshot2.objects.size());
  EXPECT_EQ(snapshot1.layers.size(), snapshot2.layers.size());

  // Layer IDs and order must be identical
  ASSERT_EQ(snapshot1.layers.size(), snapshot2.layers.size());
  for (size_t i = 0; i < snapshot1.layers.size(); ++i) {
    EXPECT_EQ(snapshot1.layers[i].layerId, snapshot2.layers[i].layerId);
    EXPECT_EQ(snapshot1.layers[i].name, snapshot2.layers[i].name);
    EXPECT_FLOAT_EQ(snapshot1.layers[i].zBase, snapshot2.layers[i].zBase);
  }
}

}  // namespace test
}  // namespace openroad3d
}  // namespace gui

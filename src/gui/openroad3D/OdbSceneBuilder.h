// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/domain/SceneTypes.h"
#include "odb/db.h"

namespace gui {
namespace openroad3d {

// Forward declarations
class OpenRoad3DWindow;

class OdbSceneBuilder {
 public:
  explicit OdbSceneBuilder(odb::dbDatabase* db);
  ~OdbSceneBuilder();

  // Build SceneSnapshot from the current database design
  viewer3d::domain::SceneSnapshot build();

  // Get database units per micron for coordinate conversion
  int getDbUnitsPerMicron() const;

 private:
  // Process all instances
  void processInstances(viewer3d::domain::SceneSnapshot& snapshot);

  // Process all nets and their routing
  void processNets(viewer3d::domain::SceneSnapshot& snapshot);

  // Process all block terminals (pins)
  void processPins(viewer3d::domain::SceneSnapshot& snapshot);

  // Process all vias
  void processVias(viewer3d::domain::SceneSnapshot& snapshot);

  // Create or get a layer record for the given layer name
  viewer3d::domain::LayerRecord* getOrCreateLayer(
      viewer3d::domain::SceneSnapshot& snapshot,
      const std::string& layerName,
      float zBase);

  // Convert db units to microns
  float dbuToMicrons(int dbu) const;

  // Generate unique object ID
  std::string generateObjectId(const std::string& prefix);

  odb::dbDatabase* db_;
  int db_units_per_micron_ = 0;
  int object_id_counter_ = 0;
};

}  // namespace openroad3d
}  // namespace gui
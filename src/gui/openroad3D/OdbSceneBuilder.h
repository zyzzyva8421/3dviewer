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

  // Build a complete SceneSnapshot from the current ODB design state.
  // Output units are microns for geometry coordinates.
  viewer3d::domain::SceneSnapshot build();

  // Return DB units per micron used for dbu-to-micron conversion.
  int getDbUnitsPerMicron() const;

 private:
  // Append placed instances as ObjectRecord entries.
  void processInstances(viewer3d::domain::SceneSnapshot& snapshot);

  // Append routed net geometry (wire segments and vias).
  void processNets(viewer3d::domain::SceneSnapshot& snapshot);

  // Append top-level block terminals and pin geometry.
  void processPins(viewer3d::domain::SceneSnapshot& snapshot);

  // Append placement/routing blockage visualization objects.
  void processBlockages(viewer3d::domain::SceneSnapshot& snapshot);

  // Append DRC markers from the design database.
  void processDrcMarkers(viewer3d::domain::SceneSnapshot& snapshot);

  // Append routing track guides as helper visualization objects.
  void processTrackGrids(viewer3d::domain::SceneSnapshot& snapshot);

  // Fetch an existing layer record or create a new one in snapshot.layers.
  // zBase is stored in microns and should be the layer center Z.
  viewer3d::domain::LayerRecord* getOrCreateLayer(
      viewer3d::domain::SceneSnapshot& snapshot,
      const std::string& layerName,
      float zBase);

  // Convert ODB DBU coordinates to microns.
  float dbuToMicrons(int dbu) const;

  // Generate a monotonically increasing object id for temporary records.
  std::string generateObjectId(const std::string& prefix);

  // Non-owning pointer managed by OpenROAD/ODB lifetime.
  odb::dbDatabase* db_;
  // Cached DBU scale factor for coordinate conversion.
  int db_units_per_micron_ = 0;
  // Internal counter for generated object ids.
  int object_id_counter_ = 0;
};

}  // namespace openroad3d
}  // namespace gui
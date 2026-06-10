// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "OdbSceneBuilder.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>

#include "odb/db.h"
#include "odb/dbShape.h"

namespace gui {
namespace openroad3d {

namespace {

// Cache for layer z positions: layer number -> zBase
// Computed once using stacking formula: z = prev_z + prev_thickness/2 + current_thickness/2
std::map<int, float> layerZCache;

// Cache for layer thickness: layer number -> thickness
std::map<int, float> layerThicknessCache;

// Compute and cache all layer z positions based on stacking formula
void computeLayerZPositions(odb::dbTech* tech, int db_units_per_micron) {
  layerZCache.clear();
  layerThicknessCache.clear();
  
  if (!tech) {
    return;
  }

  // Get layers in physical stacking order (already ordered in dbSet)
  std::vector<odb::dbTechLayer*> layers;
  odb::dbSet<odb::dbTechLayer> techLayers = tech->getLayers();
  for (odb::dbTechLayer* layer : techLayers) {
    layers.push_back(layer);
  }

  fprintf(stderr, "DEBUG computeLayerZPositions: layer order:\n");
  for (size_t i = 0; i < layers.size(); ++i) {
    odb::dbTechLayer* layer = layers[i];
    fprintf(stderr, "  [%zu] layer#%d name=%s type=%d\n", 
            i, layer->getNumber(), layer->getConstName(), (int)layer->getType());
  }

  // Calculate z for each layer using stacking formula
  // z(layer_n) = z(layer_{n-1}) + thickness(layer_{n-1})/2 + thickness(layer_n)/2
  for (size_t i = 0; i < layers.size(); ++i) {
    odb::dbTechLayer* layer = layers[i];
    int layer_num = layer->getNumber();
    
    uint32_t thickness_dbu;
    float thickness = layer->getThickness(thickness_dbu) 
        ? (static_cast<float>(thickness_dbu) / static_cast<float>(db_units_per_micron))
        : (static_cast<float>(layer->getWidth()) / static_cast<float>(db_units_per_micron)) / 2.0f;

    float zCenter;
    float zBottom;
    if (i == 0) {
      // First layer center is at thickness / 2
      zBottom = 0.0f;
      zCenter = thickness / 2.0f;
      layerZCache[layer_num] = zCenter;
    } else {
      // center = prev_center + prev_thickness/2 + current_thickness/2
      odb::dbTechLayer* prev_layer = layers[i-1];
      int prev_layer_num = prev_layer->getNumber();
      float prev_center = layerZCache[prev_layer_num];
      uint32_t prev_thickness_dbu;
      float prev_thickness = prev_layer->getThickness(prev_thickness_dbu)
          ? (static_cast<float>(prev_thickness_dbu) / static_cast<float>(db_units_per_micron))
          : (static_cast<float>(prev_layer->getWidth()) / static_cast<float>(db_units_per_micron)) / 2.0f;
      zCenter = prev_center + prev_thickness / 2.0f + thickness / 2.0f;
      layerZCache[layer_num] = zCenter;
      zBottom = zCenter - thickness / 2.0f;
    }
    
    // Store thickness in a separate cache for use in layer display
    layerThicknessCache[layer_num] = thickness;
    
        float top = zBottom + thickness;
        fprintf(stderr, "  [%zu] layer#%d %s: zBottom=%.4f zCenter=%.4f top=%.4f thickness=%.4f\n",
          i, layer_num, layer->getConstName(), zBottom, zCenter, top, thickness);
  }
}

// Layer z-positions in microns using stacking formula
float getLayerZBase(odb::dbTechLayer* layer) {
  if (!layer) {
    return 0.0f;
  }
  int layer_num = layer->getNumber();
  
  // Check cache first
  auto it = layerZCache.find(layer_num);
  if (it != layerZCache.end()) {
    return it->second;
  }
  
  // Fallback: use layer number-based formula if cache not populated
  odb::dbTechLayerType type = layer->getType();
  if (type == odb::dbTechLayerType::ROUTING) {
    return layer_num * 0.1f;
  } else if (type == odb::dbTechLayerType::CUT) {
    // Cut layers are between the two metals they connect
    // With 0.1f scale: metal2 (z=0.4) and metal4 (z=0.8), via12_cut at z=0.6
    return layer_num * 0.1f - 0.05f;
  }
  return layer_num * 0.06f;
}

// Get via Z position (between the two metal layers)
float getViaZBase(odb::dbTechLayer* topLayer, odb::dbTechLayer* bottomLayer) {
  float topZ = topLayer ? getLayerZBase(topLayer) : 0.1f;
  float bottomZ = bottomLayer ? getLayerZBase(bottomLayer) : 0.05f;
  return (topZ + bottomZ) * 0.5f;
}

// Get layer thickness in microns
// Uses layer's actual thickness from getThickness() if available
// Falls back to getWidth()/2 for layers without thickness defined
float getLayerThickness(odb::dbTechLayer* layer, int db_units_per_micron) {
  if (!layer) {
    return 0.1f;
  }
  uint32_t thickness_dbu;
  if (layer->getThickness(thickness_dbu)) {
    return static_cast<float>(thickness_dbu) / static_cast<float>(db_units_per_micron);
  }
  // Fallback: use default width / 2
  int width = layer->getWidth();
  return (static_cast<float>(width) / static_cast<float>(db_units_per_micron)) / 2.0f;
}

// Generate via layer name like "via12" from two layer names
std::string getViaLayerName(odb::dbTechLayer* topLayer, odb::dbTechLayer* bottomLayer) {
  std::string viaName = "via";
  if (bottomLayer) {
    viaName += std::to_string(bottomLayer->getNumber());
  }
  if (topLayer) {
    viaName += std::to_string(topLayer->getNumber());
  }
  return viaName;
}

// Fixed color scheme for common metal layers
viewer3d::domain::ColorRgba fixedLayerColor(const std::string& layerName) {
  viewer3d::domain::ColorRgba color;
  color.a = 0.85f;

  // Metal 1-4: Red, Green, Yellow, Cyan
  if (layerName == "metal1" || layerName == "Metal1" || layerName == "METAL1") {
    color.r = 0.9f; color.g = 0.2f; color.b = 0.2f;  // Red
  } else if (layerName == "metal2" || layerName == "Metal2" || layerName == "METAL2") {
    color.r = 0.2f; color.g = 0.9f; color.b = 0.2f;  // Green
  } else if (layerName == "metal3" || layerName == "Metal3" || layerName == "METAL3") {
    color.r = 0.9f; color.g = 0.9f; color.b = 0.2f;  // Yellow
  } else if (layerName == "metal4" || layerName == "Metal4" || layerName == "METAL4") {
    color.r = 0.2f; color.g = 0.9f; color.b = 0.9f;  // Cyan
  } else if (layerName == "metal5" || layerName == "Metal5" || layerName == "METAL5") {
    color.r = 0.9f; color.g = 0.5f; color.b = 0.2f;  // Orange
  } else if (layerName == "metal6" || layerName == "Metal6" || layerName == "METAL6") {
    color.r = 0.5f; color.g = 0.2f; color.b = 0.9f;  // Purple
  } else if (layerName == "metal7" || layerName == "Metal7" || layerName == "METAL7") {
    color.r = 0.2f; color.g = 0.5f; color.b = 0.9f;  // Sky Blue
  } else if (layerName == "metal8" || layerName == "Metal8" || layerName == "METAL8") {
    color.r = 0.9f; color.g = 0.2f; color.b = 0.9f;  // Magenta
  } else if (layerName == "metal9" || layerName == "Metal9" || layerName == "METAL9") {
    color.r = 0.4f; color.g = 0.9f; color.b = 0.4f;  // Lime
  } else if (layerName == "metal10" || layerName == "Metal10" || layerName == "METAL10") {
    color.r = 0.9f; color.g = 0.4f; color.b = 0.6f;  // Pink
  } else {
    // For other layers, use hash-based random but distinct colors
    int hash = 0;
    for (char c : layerName) {
      hash = hash * 31 + static_cast<int>(c);
    }
    float h = std::fmod(hash * 0.618033988749, 1.0f);
    // Use higher saturation and distinct hue to avoid similar colors
    float s = 0.75f;
    float v = 0.9f;

    // HSV to RGB conversion
    int hi = static_cast<int>(h * 6);
    float f = h * 6 - hi;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (hi % 6) {
      case 0: color.r = v; color.g = t; color.b = p; break;
      case 1: color.r = q; color.g = v; color.b = p; break;
      case 2: color.r = p; color.g = v; color.b = t; break;
      case 3: color.r = p; color.g = q; color.b = v; break;
      case 4: color.r = t; color.g = p; color.b = v; break;
      default: color.r = v; color.g = p; color.b = q; break;
    }
  }
  return color;
}

// Generate pseudo-random but stable color from layer name
viewer3d::domain::ColorRgba colorFromLayerName(const std::string& name) {
  // First try fixed colors
  viewer3d::domain::ColorRgba color = fixedLayerColor(name);
  if (color.r > 0 || color.g > 0 || color.b > 0) {
    return color;
  }

  // Simple hash to get consistent colors per layer
  int hash = 0;
  for (char c : name) {
    hash = hash * 31 + static_cast<int>(c);
  }
  float h = std::fmod(hash * 0.618033988749, 1.0f);
  float s = 0.7f;
  float v = 0.9f;

  // HSV to RGB conversion
  int hi = static_cast<int>(h * 6);
  float f = h * 6 - hi;
  float p = v * (1 - s);
  float q = v * (1 - f * s);
  float t = v * (1 - (1 - f) * s);

  switch (hi % 6) {
    case 0: color.r = v; color.g = t; color.b = p; break;
    case 1: color.r = q; color.g = v; color.b = p; break;
    case 2: color.r = p; color.g = v; color.b = t; break;
    case 3: color.r = p; color.g = q; color.b = v; break;
    case 4: color.r = t; color.g = p; color.b = v; break;
    default: color.r = v; color.g = p; color.b = q; break;
  }

  return color;
}

std::string makePointerObjectId(const void* ptr,
                                const std::string& category,
                                int index = -1,
                                const std::string& role = "") {
  std::ostringstream oss;
  oss << "ptr_" << ptr << "_" << category;
  if (index >= 0) {
    oss << "_" << index;
  }
  if (!role.empty()) {
    oss << "_" << role;
  }
  return oss.str();
}

}  // anonymous namespace

OdbSceneBuilder::OdbSceneBuilder(odb::dbDatabase* db) : db_(db), object_id_counter_(0) {
  if (db_) {
    odb::dbChip* chip = db_->getChip();
    if (chip) {
      odb::dbBlock* block = chip->getBlock();
      if (block) {
        db_units_per_micron_ = block->getDbUnitsPerMicron();
      }
    }
  }
}

OdbSceneBuilder::~OdbSceneBuilder() {}

int OdbSceneBuilder::getDbUnitsPerMicron() const {
  return db_units_per_micron_;
}

float OdbSceneBuilder::dbuToMicrons(int dbu) const {
  return static_cast<float>(dbu) / static_cast<float>(db_units_per_micron_);
}

std::string OdbSceneBuilder::generateObjectId(const std::string& prefix) {
  std::ostringstream oss;
  oss << prefix << "_" << object_id_counter_++;
  return oss.str();
}

viewer3d::domain::LayerRecord* OdbSceneBuilder::getOrCreateLayer(
    viewer3d::domain::SceneSnapshot& snapshot,
    const std::string& layerName,
    float zBase) {
  // Check if layer already exists
  for (auto& layer : snapshot.layers) {
    if (layer.name == layerName) {
      return &layer;
    }
  }

  // Create new layer
  viewer3d::domain::LayerRecord layer;
  layer.layerId = layerName;
  layer.name = layerName;
  layer.zBase = zBase;
  layer.thickness = 0.3f;
  layer.visible = true;
  layer.selectable = true;
  layer.color = colorFromLayerName(layerName);
  layer.transparency = 0.0f;
  layer.lineWidth = 1.0f;
  layer.lineStyle = viewer3d::domain::LineStyle::Solid;

  snapshot.layers.push_back(layer);
  return &snapshot.layers.back();
}

void OdbSceneBuilder::processInstances(viewer3d::domain::SceneSnapshot& snapshot) {
  odb::dbChip* chip = db_->getChip();
  if (!chip) {
    return;
  }
  odb::dbBlock* block = chip->getBlock();
  if (!block) {
    return;
  }

  // Ensure layer center/thickness cache is available for ITerm layer-based Z placement.
  computeLayerZPositions(db_->getTech(), db_units_per_micron_);

  for (auto inst : block->getInsts()) {
    viewer3d::domain::ObjectRecord obj;
    obj.objectId = makePointerObjectId(inst, "inst");
    obj.type = viewer3d::domain::ObjectType::Inst;
    obj.displayName = inst->getName();

    // Get bounding box
    odb::dbBox* bbox = inst->getBBox();
    if (bbox) {
      odb::Rect rect = bbox->getBox();

      // Derive instance Z from its contained pin-shape layers:
      // centerZ = average(layerCenterZ of all ITerm shapes)
      // thickness = minimal value that covers all pin-shape Z intervals with that center.
      bool hasPinShape = false;
      float layerCenterSum = 0.0f;
      int layerCenterCount = 0;
      float pinThicknessSum = 0.0f;
      float pinZMin = 0.0f;
      float pinZMax = 0.0f;
      std::set<int> seenLayerNums;

      for (auto iterm : inst->getITerms()) {
        if (!iterm) {
          continue;
        }

        odb::dbITermShapeItr itr(true);
        itr.begin(iterm);
        odb::dbShape shape;
        while (itr.next(shape)) {
          odb::dbTechLayer* pinLayer = shape.getTechLayer();
          if (!pinLayer) {
            continue;
          }

          const float layerCenter = getLayerZBase(pinLayer);
          const float layerThickness = getLayerThickness(pinLayer, db_units_per_micron_);
          const float shapeZMin = layerCenter - layerThickness * 0.5f;
          const float shapeZMax = layerCenter + layerThickness * 0.5f;

          const int layerNum = pinLayer->getNumber();
          if (seenLayerNums.insert(layerNum).second) {
            layerCenterSum += layerCenter;
            layerCenterCount++;
            pinThicknessSum += layerThickness;

            if (!hasPinShape) {
              pinZMin = shapeZMin;
              pinZMax = shapeZMax;
              hasPinShape = true;
            } else {
              pinZMin = std::min(pinZMin, shapeZMin);
              pinZMax = std::max(pinZMax, shapeZMax);
            }
          }
        }
      }

      float centerZ = 0.25f;
      float thicknessZ = 0.5f;
      if (hasPinShape && layerCenterCount > 0) {
        centerZ = layerCenterSum / static_cast<float>(layerCenterCount);
        const float halfSpan = std::max(centerZ - pinZMin, pinZMax - centerZ);
        const float coverThickness = halfSpan * 2.0f;
        // Thickness must satisfy both:
        // 1) cover all contained pin shapes in Z;
        // 2) be at least the sum of all pin-shape thicknesses.
        thicknessZ = std::max(0.01f, std::max(coverThickness, pinThicknessSum));
      }

      obj.hasBbox = true;
      obj.bboxMin[0] = dbuToMicrons(rect.xMin());
      obj.bboxMin[1] = dbuToMicrons(rect.yMin());
      obj.bboxMin[2] = centerZ - thicknessZ * 0.5f;
      obj.bboxMax[0] = dbuToMicrons(rect.xMax());
      obj.bboxMax[1] = dbuToMicrons(rect.yMax());
      obj.bboxMax[2] = centerZ + thicknessZ * 0.5f;

      // Calculate center position for transform
      float centerX = (obj.bboxMin[0] + obj.bboxMax[0]) * 0.5f;
      float centerY = (obj.bboxMin[1] + obj.bboxMax[1]) * 0.5f;
      float centerZComputed = (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f;

      // Set transform (4x4 matrix with translation)
      obj.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f,
                       centerX, centerY, centerZComputed, 1.0f};

      // Use layer "instance" for visualization
      viewer3d::domain::LayerRecord* layer
          = getOrCreateLayer(snapshot, "instance", 0.0f);
      if (layer) {
        layer->color = {0.6f, 0.6f, 0.6f, 1.0f};
        layer->transparency = 0.55f;
        obj.layerId = layer->layerId;
      }

      // Style reference for cell name
      obj.styleRef = inst->getMaster()->getName();

      snapshot.objects.push_back(obj);
    }
  }
}

void OdbSceneBuilder::processNets(viewer3d::domain::SceneSnapshot& snapshot) {
  odb::dbChip* chip = db_->getChip();
  if (!chip) {
    return;
  }
  odb::dbBlock* block = chip->getBlock();
  if (!block) {
    return;
  }

  // Compute layer z positions using stacking formula
  odb::dbTech* tech = db_->getTech();
  computeLayerZPositions(tech, db_units_per_micron_);

  int total_shapes = 0;
  int via_shapes = 0;
  int segment_shapes = 0;
  int via_objects_created = 0;

  // Process all nets and their wires
  for (auto net : block->getNets()) {
    std::string netName = net->getName();

    // Get wire from net (single wire per net)
    odb::dbWire* wire = net->getWire();
    if (!wire) {
      continue;
    }

    // Iterate wire shapes using dbWireShapeItr
    odb::dbWireShapeItr itr;
    itr.begin(wire);

    odb::dbShape shape;
    int shapeIndex = 0;
    while (itr.next(shape)) {
      total_shapes++;
      viewer3d::domain::ObjectRecord obj;
      obj.objectId = makePointerObjectId(wire, "wire", shapeIndex);
      obj.type = viewer3d::domain::ObjectType::Wire;
      obj.displayName = netName;

      int x1 = shape.xMin();
      int y1 = shape.yMin();
      int x2 = shape.xMax();
      int y2 = shape.yMax();

      // Handle both isVia() for VIA type and isViaBox() for VIA_BOX type
      if (shape.isVia() || shape.isViaBox()) {
        via_shapes++;
        
        // Try dbVia first, then dbTechVia
        odb::dbTechLayer* topLayer = nullptr;
        odb::dbTechLayer* bottomLayer = nullptr;

        odb::dbVia* via = shape.getVia();
        if (via) {
          topLayer = via->getTopLayer();
          bottomLayer = via->getBottomLayer();
        } else {
          // Try tech via
          odb::dbTechVia* techVia = shape.getTechVia();
          if (techVia) {
            topLayer = techVia->getTopLayer();
            bottomLayer = techVia->getBottomLayer();
          }
        }
        
        if (!topLayer || !bottomLayer) {
          continue;
        }

        // Get z values to determine physical top/bottom (z larger = physically on top)
        float topZ_raw = getLayerZBase(topLayer);
        float bottomZ_raw = getLayerZBase(bottomLayer);
        
        // Ensure topLayer is physically above bottomLayer
        odb::dbTechLayer* physTopLayer = topLayer;
        odb::dbTechLayer* physBottomLayer = bottomLayer;
        float physTopZ = topZ_raw;
        float physBottomZ = bottomZ_raw;
        if (bottomZ_raw > topZ_raw) {
          // Swap - bottom layer is physically higher
          physTopLayer = bottomLayer;
          physBottomLayer = topLayer;
          physTopZ = bottomZ_raw;
          physBottomZ = topZ_raw;
        }

        // Generate via layer name like "via12"
        std::string viaLayerName = getViaLayerName(physTopLayer, physBottomLayer);
        // Group ID to identify all three parts of a via as one entity
        std::string viaGroupId = netName + "_" + viaLayerName;
        float topThickness = getLayerThickness(physTopLayer, db_units_per_micron_);
        float bottomThickness = getLayerThickness(physBottomLayer, db_units_per_micron_);
        
        // Get placed via boxes (already translated by via placement) and
        // derive per-layer XY unions. This matches 2D GUI behavior.
        std::vector<odb::dbShape> viaBoxes;
        odb::dbShape::getViaBoxes(shape, viaBoxes);

        bool hasTopXY = false;
        bool hasBottomXY = false;
        bool hasCutXY = false;
        int topXMin = 0, topYMin = 0, topXMax = 0, topYMax = 0;
        int bottomXMin = 0, bottomYMin = 0, bottomXMax = 0, bottomYMax = 0;
        int cutXMin = 0, cutYMin = 0, cutXMax = 0, cutYMax = 0;

        // Get actual cut layer thickness from via cut boxes when available.
        float cutThickness = bottomThickness;  // fallback
        for (const auto& box : viaBoxes) {
          odb::dbTechLayer* boxLayer = box.getTechLayer();
          if (!boxLayer) {
            continue;
          }

          const int xMin = box.xMin();
          const int yMin = box.yMin();
          const int xMax = box.xMax();
          const int yMax = box.yMax();

          if (boxLayer->getNumber() == physTopLayer->getNumber()) {
            if (!hasTopXY) {
              topXMin = xMin;
              topYMin = yMin;
              topXMax = xMax;
              topYMax = yMax;
              hasTopXY = true;
            } else {
              topXMin = std::min(topXMin, xMin);
              topYMin = std::min(topYMin, yMin);
              topXMax = std::max(topXMax, xMax);
              topYMax = std::max(topYMax, yMax);
            }
          }

          if (boxLayer->getNumber() == physBottomLayer->getNumber()) {
            if (!hasBottomXY) {
              bottomXMin = xMin;
              bottomYMin = yMin;
              bottomXMax = xMax;
              bottomYMax = yMax;
              hasBottomXY = true;
            } else {
              bottomXMin = std::min(bottomXMin, xMin);
              bottomYMin = std::min(bottomYMin, yMin);
              bottomXMax = std::max(bottomXMax, xMax);
              bottomYMax = std::max(bottomYMax, yMax);
            }
          }

          if (boxLayer->getType() == odb::dbTechLayerType::CUT) {
            cutThickness = getLayerThickness(boxLayer, db_units_per_micron_);
            if (!hasCutXY) {
              cutXMin = xMin;
              cutYMin = yMin;
              cutXMax = xMax;
              cutYMax = yMax;
              hasCutXY = true;
            } else {
              cutXMin = std::min(cutXMin, xMin);
              cutYMin = std::min(cutYMin, yMin);
              cutXMax = std::max(cutXMax, xMax);
              cutYMax = std::max(cutYMax, yMax);
            }
          }
        }

        // Fallback if top/bottom boxes are not explicitly found in viaBoxes.
        if (!hasTopXY || !hasBottomXY || !hasCutXY) {
          int viaX = 0;
          int viaY = 0;
          shape.getViaXY(viaX, viaY);
          if (!hasTopXY) {
            topXMin = viaX;
            topYMin = viaY;
            topXMax = viaX;
            topYMax = viaY;
            hasTopXY = true;
          }
          if (!hasBottomXY) {
            bottomXMin = viaX;
            bottomYMin = viaY;
            bottomXMax = viaX;
            bottomYMax = viaY;
            hasBottomXY = true;
          }
          if (!hasCutXY) {
            cutXMin = viaX;
            cutYMin = viaY;
            cutXMax = viaX;
            cutYMax = viaY;
            hasCutXY = true;
          }
        }

        // Keep all Z definitions consistent as centers.
        // cutCenter = bottomCenter + bottomThickness/2 + cutThickness/2
        float viaZ = physBottomZ + bottomThickness / 2.0f + cutThickness / 2.0f;
        fprintf(stderr, "DEBUG via calc: name=%s bot=%s botCenterZ=%.4f botT=%.4f cutT=%.4f cutCenterZ=%.4f\n",
          viaLayerName.c_str(), physBottomLayer->getConstName(), physBottomZ,
          bottomThickness, cutThickness, viaZ);

        // Create via_top object - use the TOP metal layer's name (same physical layer)
        {
          viewer3d::domain::ObjectRecord viaTop;
          viaTop.objectId = makePointerObjectId(wire, "via", shapeIndex, "top");
          viaTop.type = viewer3d::domain::ObjectType::Via;
          viaTop.displayName = netName;
          viaTop.styleRef = "via_top";
          viaTop.groupId = viaGroupId;  // Group all via parts together

          viaTop.hasBbox = true;
          viaTop.bboxMin[0] = dbuToMicrons(topXMin);
          viaTop.bboxMin[1] = dbuToMicrons(topYMin);
          viaTop.bboxMin[2] = physTopZ - topThickness / 2.0f;
          viaTop.bboxMax[0] = dbuToMicrons(topXMax);
          viaTop.bboxMax[1] = dbuToMicrons(topYMax);
          viaTop.bboxMax[2] = physTopZ + topThickness / 2.0f;

          float centerX = (viaTop.bboxMin[0] + viaTop.bboxMax[0]) * 0.5f;
          float centerY = (viaTop.bboxMin[1] + viaTop.bboxMax[1]) * 0.5f;
          float centerZ = (viaTop.bboxMin[2] + viaTop.bboxMax[2]) * 0.5f;
                   viaTop.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f,
                              centerX, centerY, centerZ, 1.0f};

          // Use the TOP metal layer's name - via_top is physically on that metal layer
          std::string topLayerName = physTopLayer->getName();
          fprintf(stderr, "DEBUG via_top: topLayerName=%s physTopZ=%.4f viaZ=%.4f\n", 
                  topLayerName.c_str(), physTopZ, viaZ);
          viewer3d::domain::LayerRecord* layerRec
              = getOrCreateLayer(snapshot, topLayerName, physTopZ);
          if (layerRec) {
            viaTop.layerId = layerRec->layerId;
          }
          snapshot.objects.push_back(viaTop);
        }

        // Create via_bottom object - use the BOTTOM metal layer's name
        {
          viewer3d::domain::ObjectRecord viaBottom;
          viaBottom.objectId = makePointerObjectId(wire, "via", shapeIndex, "bottom");
          viaBottom.type = viewer3d::domain::ObjectType::Via;
          viaBottom.displayName = netName;
          viaBottom.styleRef = "via_bottom";
          viaBottom.groupId = viaGroupId;

          viaBottom.hasBbox = true;
          viaBottom.bboxMin[0] = dbuToMicrons(bottomXMin);
          viaBottom.bboxMin[1] = dbuToMicrons(bottomYMin);
          viaBottom.bboxMin[2] = physBottomZ - bottomThickness / 2.0f;
          viaBottom.bboxMax[0] = dbuToMicrons(bottomXMax);
          viaBottom.bboxMax[1] = dbuToMicrons(bottomYMax);
          viaBottom.bboxMax[2] = physBottomZ + bottomThickness / 2.0f;

          float centerX = (viaBottom.bboxMin[0] + viaBottom.bboxMax[0]) * 0.5f;
          float centerY = (viaBottom.bboxMin[1] + viaBottom.bboxMax[1]) * 0.5f;
          float centerZ = (viaBottom.bboxMin[2] + viaBottom.bboxMax[2]) * 0.5f;
          viaBottom.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 centerX, centerY, centerZ, 1.0f};

          // Use the BOTTOM metal layer's name - via_bottom is physically on that metal layer
          std::string bottomLayerName = physBottomLayer->getName();
          viewer3d::domain::LayerRecord* layerRec
              = getOrCreateLayer(snapshot, bottomLayerName, physBottomZ);
          if (layerRec) {
            viaBottom.layerId = layerRec->layerId;
          }
          snapshot.objects.push_back(viaBottom);
        }

        // Create via_cut object (the cut layer between metals)
        {
          viewer3d::domain::ObjectRecord viaCut;
          viaCut.objectId = makePointerObjectId(wire, "via", shapeIndex, "cut");
          viaCut.type = viewer3d::domain::ObjectType::Via;
          viaCut.displayName = netName;
          viaCut.styleRef = "via_cut";
          viaCut.groupId = viaGroupId;  // Group all via parts together

          // Cut layer centered at viaZ
          viaCut.hasBbox = true;
          viaCut.bboxMin[0] = dbuToMicrons(cutXMin);
          viaCut.bboxMin[1] = dbuToMicrons(cutYMin);
          viaCut.bboxMin[2] = viaZ - cutThickness / 2.0f;
          viaCut.bboxMax[0] = dbuToMicrons(cutXMax);
          viaCut.bboxMax[1] = dbuToMicrons(cutYMax);
          viaCut.bboxMax[2] = viaZ + cutThickness / 2.0f;

          float centerX = (viaCut.bboxMin[0] + viaCut.bboxMax[0]) * 0.5f;
          float centerY = (viaCut.bboxMin[1] + viaCut.bboxMax[1]) * 0.5f;
          float centerZ = (viaCut.bboxMin[2] + viaCut.bboxMax[2]) * 0.5f;
          viaCut.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f,
                              centerX, centerY, centerZ, 1.0f};

          // Use via layer name for cut (e.g., "via12_cut")
          viewer3d::domain::LayerRecord* layerRec
              = getOrCreateLayer(snapshot, viaLayerName + "_cut", viaZ);
          if (layerRec) {
            viaCut.layerId = layerRec->layerId;
          }
                   snapshot.objects.push_back(viaCut);
                   via_objects_created++;
        }
      } else {
        // Segment shape
        segment_shapes++;
        odb::dbTechLayer* layer = shape.getTechLayer();
        float zBase = getLayerZBase(layer);
        float thickness = getLayerThickness(layer, db_units_per_micron_);

        obj.hasBbox = true;
        obj.bboxMin[0] = dbuToMicrons(x1);
        obj.bboxMin[1] = dbuToMicrons(y1);
        obj.bboxMin[2] = zBase - thickness / 2.0f;
        obj.bboxMax[0] = dbuToMicrons(x2);
        obj.bboxMax[1] = dbuToMicrons(y2);
        obj.bboxMax[2] = zBase + thickness / 2.0f;

        // Transform = center (consistent with instance and via)
        float centerX = (obj.bboxMin[0] + obj.bboxMax[0]) * 0.5f;
        float centerY = (obj.bboxMin[1] + obj.bboxMax[1]) * 0.5f;
        float centerZ = (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f;
        obj.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        centerX, centerY, centerZ, 1.0f};

        obj.styleRef = "wire";

        if (layer) {
          std::string layerName = layer->getName();
          viewer3d::domain::LayerRecord* layerRec
              = getOrCreateLayer(snapshot, layerName, zBase);
          if (layerRec) {
            obj.layerId = layerRec->layerId;
          }
        }

        snapshot.objects.push_back(obj);
      }

      shapeIndex++;
    }
  }
}

void OdbSceneBuilder::processPins(viewer3d::domain::SceneSnapshot& snapshot) {
  odb::dbChip* chip = db_->getChip();
  if (!chip) {
    return;
  }
  odb::dbBlock* block = chip->getBlock();
  if (!block) {
    return;
  }

  // Top-level IO pins (BTerms)
  for (auto bterm : block->getBTerms()) {
    viewer3d::domain::ObjectRecord obj;
    obj.objectId = makePointerObjectId(bterm, "bterm_pin");
    obj.type = viewer3d::domain::ObjectType::Pin;
    obj.displayName = bterm->getName();

    // Get pin geometry from BPins
    for (auto bpin : bterm->getBPins()) {
      odb::Rect rect = bpin->getBBox();
      if (rect.area() > 0) {
        odb::dbTechLayer* pinLayer = nullptr;
        for (auto box : bpin->getBoxes()) {
          if (box) {
            pinLayer = box->getTechLayer();
            if (pinLayer) {
              break;
            }
          }
        }
        float zCenter = pinLayer ? getLayerZBase(pinLayer) : 0.1f;
        float zThickness = pinLayer ? getLayerThickness(pinLayer, db_units_per_micron_) : 0.2f;

        obj.hasBbox = true;
        obj.bboxMin[0] = dbuToMicrons(rect.xMin());
        obj.bboxMin[1] = dbuToMicrons(rect.yMin());
        obj.bboxMin[2] = zCenter - zThickness * 0.5f;
        obj.bboxMax[0] = dbuToMicrons(rect.xMax());
        obj.bboxMax[1] = dbuToMicrons(rect.yMax());
        obj.bboxMax[2] = zCenter + zThickness * 0.5f;

        float centerX = (obj.bboxMin[0] + obj.bboxMax[0]) * 0.5f;
        float centerY = (obj.bboxMin[1] + obj.bboxMax[1]) * 0.5f;
        float centerZ = (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f;
        obj.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        centerX, centerY, centerZ, 1.0f};

        if (pinLayer) {
          std::string layerName = pinLayer->getName();
          viewer3d::domain::LayerRecord* layerRec
              = getOrCreateLayer(snapshot, layerName, zCenter);
          obj.layerId = layerRec ? layerRec->layerId : "pin";
        } else {
          obj.layerId = "pin";
        }
        break;  // Use first pin geometry
      }
    }

    if (!obj.hasBbox) {
      // Fallback: use 0-sized pin
      obj.hasBbox = false;
      obj.layerId = "pin";
    }

    snapshot.objects.push_back(obj);
  }

  // Instance internal pins (ITerms)
  for (auto inst : block->getInsts()) {
    for (auto iterm : inst->getITerms()) {
      if (!iterm) {
        continue;
      }

      odb::dbITermShapeItr itr(true);
      itr.begin(iterm);
      odb::dbShape shape;
      int shapeIndex = 0;
      while (itr.next(shape)) {
        odb::dbTechLayer* pinLayer = shape.getTechLayer();
        if (!pinLayer) {
          continue;
        }

        viewer3d::domain::ObjectRecord obj;
        obj.objectId = makePointerObjectId(iterm, "iterm_pin", shapeIndex);
        obj.type = viewer3d::domain::ObjectType::Pin;

        std::string instName = inst->getName();
        std::string pinName = iterm->getMTerm() ? iterm->getMTerm()->getName() : "iterm";
        obj.displayName = instName + "/" + pinName;

        float zCenter = getLayerZBase(pinLayer);
        float zThickness = getLayerThickness(pinLayer, db_units_per_micron_);

        obj.hasBbox = true;
        obj.bboxMin[0] = dbuToMicrons(shape.xMin());
        obj.bboxMin[1] = dbuToMicrons(shape.yMin());
        obj.bboxMin[2] = zCenter - zThickness * 0.5f;
        obj.bboxMax[0] = dbuToMicrons(shape.xMax());
        obj.bboxMax[1] = dbuToMicrons(shape.yMax());
        obj.bboxMax[2] = zCenter + zThickness * 0.5f;

        float centerX = (obj.bboxMin[0] + obj.bboxMax[0]) * 0.5f;
        float centerY = (obj.bboxMin[1] + obj.bboxMax[1]) * 0.5f;
        float centerZ = (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f;
        obj.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         centerX, centerY, centerZ, 1.0f};

        std::string layerName = pinLayer->getName();
        viewer3d::domain::LayerRecord* layerRec
            = getOrCreateLayer(snapshot, layerName, zCenter);
        obj.layerId = layerRec ? layerRec->layerId : "pin";

        snapshot.objects.push_back(obj);
        shapeIndex++;
      }
    }
  }
}

void OdbSceneBuilder::processVias(viewer3d::domain::SceneSnapshot& snapshot) {
  odb::dbChip* chip = db_->getChip();
  if (!chip) {
    return;
  }
  odb::dbBlock* block = chip->getBlock();
  if (!block) {
    return;
  }

  // Note: Wire vias are already processed in processNets
  // This processes block vias defined in the library

  for (auto via : block->getVias()) {
    odb::dbTechLayer* topLayer = via->getTopLayer();
    odb::dbTechLayer* bottomLayer = via->getBottomLayer();
    if (!topLayer || !bottomLayer) {
      continue;
    }

    // Generate via layer name like "via12"
    std::string viaLayerName = getViaLayerName(topLayer, bottomLayer);
    float topThickness = getLayerThickness(topLayer, db_units_per_micron_);
    float bottomThickness = getLayerThickness(bottomLayer, db_units_per_micron_);
    float viaZ = getViaZBase(topLayer, bottomLayer);

    // Get via bbox
    odb::dbBox* bbox = via->getBBox();
    if (!bbox) {
      continue;
    }
    odb::Rect rect = bbox->getBox();

    // Create via_top object (on top metal layer)
    {
      viewer3d::domain::ObjectRecord viaTop;
      viaTop.objectId = generateObjectId("via_top");
      viaTop.type = viewer3d::domain::ObjectType::Via;
      viaTop.displayName = via->getName() + "_" + viaLayerName + "_top";
      viaTop.styleRef = "via_top";

      float topZ = getLayerZBase(topLayer);
      viaTop.hasBbox = true;
      viaTop.bboxMin[0] = dbuToMicrons(rect.xMin());
      viaTop.bboxMin[1] = dbuToMicrons(rect.yMin());
      viaTop.bboxMin[2] = topZ;
      viaTop.bboxMax[0] = dbuToMicrons(rect.xMax());
      viaTop.bboxMax[1] = dbuToMicrons(rect.yMax());
      viaTop.bboxMax[2] = topZ + topThickness;

      float centerX = (viaTop.bboxMin[0] + viaTop.bboxMax[0]) * 0.5f;
      float centerY = (viaTop.bboxMin[1] + viaTop.bboxMax[1]) * 0.5f;
      float centerZ = (viaTop.bboxMin[2] + viaTop.bboxMax[2]) * 0.5f;
      viaTop.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f,
                          centerX, centerY, centerZ, 1.0f};

      viewer3d::domain::LayerRecord* layerRec
          = getOrCreateLayer(snapshot, viaLayerName + "_top", topZ);
      if (layerRec) {
        viaTop.layerId = layerRec->layerId;
      }
      snapshot.objects.push_back(viaTop);
    }

    // Create via_bottom object (on bottom metal layer)
    {
      viewer3d::domain::ObjectRecord viaBottom;
      viaBottom.objectId = generateObjectId("via_bottom");
      viaBottom.type = viewer3d::domain::ObjectType::Via;
      viaBottom.displayName = via->getName() + "_" + viaLayerName + "_bottom";
      viaBottom.styleRef = "via_bottom";

      float bottomZ = getLayerZBase(bottomLayer);
      viaBottom.hasBbox = true;
      viaBottom.bboxMin[0] = dbuToMicrons(rect.xMin());
      viaBottom.bboxMin[1] = dbuToMicrons(rect.yMin());
      viaBottom.bboxMin[2] = bottomZ;
      viaBottom.bboxMax[0] = dbuToMicrons(rect.xMax());
      viaBottom.bboxMax[1] = dbuToMicrons(rect.yMax());
      viaBottom.bboxMax[2] = bottomZ + bottomThickness;

      float centerX = (viaBottom.bboxMin[0] + viaBottom.bboxMax[0]) * 0.5f;
      float centerY = (viaBottom.bboxMin[1] + viaBottom.bboxMax[1]) * 0.5f;
      float centerZ = (viaBottom.bboxMin[2] + viaBottom.bboxMax[2]) * 0.5f;
      viaBottom.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f,
                              centerX, centerY, centerZ, 1.0f};

      viewer3d::domain::LayerRecord* layerRec
          = getOrCreateLayer(snapshot, viaLayerName + "_bottom", bottomZ);
      if (layerRec) {
        viaBottom.layerId = layerRec->layerId;
      }
      snapshot.objects.push_back(viaBottom);
    }

    // Create via_cut object (the cut layer between metals)
    {
      viewer3d::domain::ObjectRecord viaCut;
      viaCut.objectId = generateObjectId("via_cut");
      viaCut.type = viewer3d::domain::ObjectType::Via;
      viaCut.displayName = via->getName() + "_" + viaLayerName + "_cut";
      viaCut.styleRef = "via_cut";

      viaCut.hasBbox = true;
      viaCut.bboxMin[0] = dbuToMicrons(rect.xMin());
      viaCut.bboxMin[1] = dbuToMicrons(rect.yMin());
      viaCut.bboxMin[2] = viaZ;
      viaCut.bboxMax[0] = dbuToMicrons(rect.xMax());
      viaCut.bboxMax[1] = dbuToMicrons(rect.yMax());
      viaCut.bboxMax[2] = viaZ + 0.05f;  // Very thin cut layer

      float centerX = (viaCut.bboxMin[0] + viaCut.bboxMax[0]) * 0.5f;
      float centerY = (viaCut.bboxMin[1] + viaCut.bboxMax[1]) * 0.5f;
      float centerZ = (viaCut.bboxMin[2] + viaCut.bboxMax[2]) * 0.5f;
      viaCut.transform = {1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f,
                          centerX, centerY, centerZ, 1.0f};

      viewer3d::domain::LayerRecord* layerRec
          = getOrCreateLayer(snapshot, viaLayerName + "_cut", viaZ);
      if (layerRec) {
        viaCut.layerId = layerRec->layerId;
      }
      snapshot.objects.push_back(viaCut);
    }
  }
}

viewer3d::domain::SceneSnapshot OdbSceneBuilder::build() {
  viewer3d::domain::SceneSnapshot snapshot;
  snapshot.sourceTag = "openroad_db";

  if (!db_) {
    return snapshot;
  }

  processInstances(snapshot);
  processNets(snapshot);
  processPins(snapshot);
  // Note: processVias removed - actual via instances come from wire iteration in processNets
  // Block vias (library definitions) don't have instance location info

  // Debug: print all layers BEFORE deduplication
  fprintf(stderr, "DEBUG: Layers before dedup (%zu):\n", snapshot.layers.size());
  for (const auto& layer : snapshot.layers) {
    fprintf(stderr, "  - %s (zBase=%.2f)\n", layer.name.c_str(), layer.zBase);
  }

  // Deduplicate layers by name, keeping the first occurrence
  std::vector<viewer3d::domain::LayerRecord> uniqueLayers;
  std::set<std::string> seenLayerNames;
  for (auto& layer : snapshot.layers) {
    if (seenLayerNames.find(layer.name) == seenLayerNames.end()) {
      seenLayerNames.insert(layer.name);
      uniqueLayers.push_back(layer);
    }
  }
  snapshot.layers = uniqueLayers;

  // Sort layers by zBase only
  std::sort(snapshot.layers.begin(), snapshot.layers.end(),
            [](const viewer3d::domain::LayerRecord& a, const viewer3d::domain::LayerRecord& b) {
              return a.zBase < b.zBase;
            });

  return snapshot;
}

}  // namespace openroad3d
}  // namespace gui
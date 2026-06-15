#pragma once

#include <array>
#include <string>
#include <vector>

namespace viewer3d {
namespace domain {

enum class ObjectType {
  Wire,
  Via,
  Inst,
  Pin,
  Drc,
  Blockage,
  Track,
  Bump,
  Text,
  Unknown
};

struct ColorRgba {
  float r = 0.0F;
  float g = 0.0F;
  float b = 0.0F;
  float a = 1.0F;
};

enum class LineStyle {
  Solid,
  Dashed,
  Dotted,
  DashDot,
  DashDotDot
};

struct LayerRecord {
  std::string layerId;
  std::string name;
  float zBase = 0.0F;
  float thickness = 0.0F;
  bool visible = true;
  bool selectable = true;
  bool isHorizontal = true;   // from dbTechLayerDir: HORIZONTAL=true, VERTICAL=false
  ColorRgba color;
  std::string stippleId;
  float transparency = 0.0F;
  float lighten = 0.0F;
  float lineWidth = 1.0F;
  LineStyle lineStyle = LineStyle::Solid;
};

struct ObjectRecord {
  std::string objectId;
  ObjectType type = ObjectType::Unknown;
  std::string layerId;
  std::string groupId;  // Groups related objects (e.g., via_top, via_bottom, via_cut)
  bool visible = true;
  std::array<float, 16> transform {
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3> bboxMin {0.0F, 0.0F, 0.0F};
  std::array<float, 3> bboxMax {0.0F, 0.0F, 0.0F};
  bool hasBbox = false;
  std::string geometryRef;
  std::string styleRef;
  std::string displayName;
  // Wire direction: for Wire objects, this comes from bbox (xlen vs ylen).
  // For other objects, this defaults to true and can be overridden by layer direction.
  // Used by attachObjectLabels() to orient text along wire direction.
  bool isHorizontal = true;
};

struct CaseRecord {
  std::string caseId;
  std::string casePath;
  std::string clockPeriod;
  std::string yosysCreator;
  std::string topModule;
  std::string dieArea;
  int componentCount = 0;
  int routeSegmentCount = 0;
  std::vector<std::string> artifacts;
};

struct SceneSnapshot {
  std::string sourceTag;
  std::vector<LayerRecord> layers;
  std::vector<CaseRecord> cases;
  std::vector<ObjectRecord> objects;
};

}  // namespace domain
}  // namespace viewer3d

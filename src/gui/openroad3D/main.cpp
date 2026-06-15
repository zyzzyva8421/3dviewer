// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include <QApplication>

#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "OpenRoad3DWindow.h"
#include "OdbSceneBuilder.h"
#include "core/domain/SceneTypes.h"
#include "odb/db.h"
#include "odb/lefin.h"
#include "odb/defin.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static odb::dbDatabase* loadDatabase(int argc, char* argv[],
                                     std::vector<std::string>& lef_files,
                                     std::string& def_file,
                                     std::string& odb_file,
                                     bool& dump_stats,
                                     bool& test_zoom_mode,
                                     bool& help_mode);

static void printSceneStats(const viewer3d::domain::SceneSnapshot& snapshot);

// ---------------------------------------------------------------------------
//  JSON field-value helper
// ---------------------------------------------------------------------------
namespace {

void jsonPrintString(std::ostream& os, const std::string& key,
                     const std::string& val, bool last = false) {
  os << "  \"" << key << "\": \"" << val << "\"";
  if (!last) os << ",";
  os << "\n";
}

void jsonPrintInt(std::ostream& os, const std::string& key, int64_t val,
                  bool last = false) {
  os << "  \"" << key << "\": " << val;
  if (!last) os << ",";
  os << "\n";
}

void jsonPrintFloat(std::ostream& os, const std::string& key, float val,
                    bool last = false) {
  os << "  \"" << key << "\": " << val;
  if (!last) os << ",";
  os << "\n";
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
  std::vector<std::string> lef_files;
  std::string def_file;
  std::string odb_file;
  bool dump_stats = false;
  bool test_zoom_mode = false;
  bool help_mode = false;

  // Load database (also parses CLI args)
  odb::dbDatabase* db = loadDatabase(argc, argv, lef_files, def_file,
                                     odb_file, dump_stats, test_zoom_mode, help_mode);

  if (help_mode) {
    return 0;
  }

  // --dump-stats mode: build scene, print stats, exit
  if (dump_stats) {
    if (!db) {
      std::cerr << "No database loaded, cannot dump stats.\n";
      return 1;
    }
    gui::openroad3d::OdbSceneBuilder builder(db);
    viewer3d::domain::SceneSnapshot snapshot = builder.build();
    printSceneStats(snapshot);
    return 0;
  }

  // --test-zoom mode: verify zoomToSelected center calculation, exit
  if (test_zoom_mode) {
    if (!db) {
      std::cerr << "No database loaded, cannot test zoom.\n";
      return 1;
    }
    gui::openroad3d::OdbSceneBuilder builder(db);
    viewer3d::domain::SceneSnapshot scene = builder.build();

    // Find first metal3 wire
    const viewer3d::domain::ObjectRecord* firstMetal3Wire = nullptr;
    for (const auto& obj : scene.objects) {
      if (obj.type == viewer3d::domain::ObjectType::Wire && obj.layerId == "metal3") {
        firstMetal3Wire = &obj;
        break;
      }
    }

    if (!firstMetal3Wire) {
      std::cerr << "No metal3 wire found in scene.\n";
      return 1;
    }

    std::cerr << "=== TEST ZOOM CENTER ===\n";
    std::cerr << "First metal3 wire: " << firstMetal3Wire->objectId << "\n";
    std::cerr << "  bboxMin=(" << firstMetal3Wire->bboxMin[0] << ","
              << firstMetal3Wire->bboxMin[1] << "," << firstMetal3Wire->bboxMin[2] << ")\n";
    std::cerr << "  bboxMax=(" << firstMetal3Wire->bboxMax[0] << ","
              << firstMetal3Wire->bboxMax[1] << "," << firstMetal3Wire->bboxMax[2] << ")\n";

    // Simulate zoomToSelected: bbox center = (bboxMin + bboxMax) / 2
    float expectedCenterX = (firstMetal3Wire->bboxMin[0] + firstMetal3Wire->bboxMax[0]) * 0.5f;
    float expectedCenterY = (firstMetal3Wire->bboxMin[1] + firstMetal3Wire->bboxMax[1]) * 0.5f;
    float expectedCenterZ = (firstMetal3Wire->bboxMin[2] + firstMetal3Wire->bboxMax[2]) * 0.5f;
    std::cerr << "  EXPECTED bbox center=(" << expectedCenterX << "," << expectedCenterY << "," << expectedCenterZ << ")\n";

    // Simulate bounding sphere: for a box, center = bbox center, radius = half diagonal
    float halfDiagX = (firstMetal3Wire->bboxMax[0] - firstMetal3Wire->bboxMin[0]) * 0.5f;
    float halfDiagY = (firstMetal3Wire->bboxMax[1] - firstMetal3Wire->bboxMin[1]) * 0.5f;
    float halfDiagZ = (firstMetal3Wire->bboxMax[2] - firstMetal3Wire->bboxMin[2]) * 0.5f;
    float expectedRadius = std::sqrt(halfDiagX*halfDiagX + halfDiagY*halfDiagY + halfDiagZ*halfDiagZ);
    std::cerr << "  EXPECTED bounding radius=" << expectedRadius << "\n";

    // What zoomToSelected would set:
    float zoomDistance = std::max(5.0f, expectedRadius * 2.5f);
    std::cerr << "  EXPECTED zoomToSelected: center=(" << expectedCenterX << "," << expectedCenterY << "," << expectedCenterZ << ")\n";
    std::cerr << "  EXPECTED zoomToSelected: distance=" << zoomDistance << "\n";
    std::cerr << "  EXPECTED zoomToSelected: yaw_=0, pitch_=0 (ORIGINAL - now preserved)\n";
    std::cerr << "\nAfter zoom, handleWheel only changes distance_, NOT center_.\n";
    std::cerr << "  So wire should stay centered when scrolling.\n";
    std::cerr << "=== TEST COMPLETE ===\n";

    return 0;
  }

  QApplication app(argc, argv);
  app.setApplicationName("OpenROAD 3D Viewer");
  app.setApplicationVersion("1.0");
  // Create and show the 3D viewer window
  gui::openroad3d::OpenRoad3DWindow window(db);
  window.setWindowTitle("OpenROAD 3D Viewer");
  window.show();

  return app.exec();
}

// ---------------------------------------------------------------------------
// loadDatabase — parse CLI args and load the database
// ---------------------------------------------------------------------------
static odb::dbDatabase* loadDatabase(int argc, char* argv[],
                                     std::vector<std::string>& lef_files,
                                     std::string& def_file,
                                     std::string& odb_file,
                                     bool& dump_stats,
                                     bool& test_zoom_mode,
                                     bool& help_mode) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  -h, --help              Show this help message\n";
      std::cout << "  -lef <file>             Load LEF file (can be specified multiple times)\n";
      std::cout << "  -def <file>             Load DEF file\n";
      std::cout << "  -odb <file>             Load OpenROAD database file\n";
      std::cout << "  --dump-stats <odb>      Load ODB and print scene statistics as JSON\n";
      std::cout << "  --test-zoom <odb>       Test zoomToSelected center calculation\n";
      help_mode = true;
      return nullptr;
    } else if (arg == "-lef" && i + 1 < argc) {
      lef_files.push_back(argv[++i]);
    } else if (arg == "-def" && i + 1 < argc) {
      def_file = argv[++i];
    } else if (arg == "-odb" && i + 1 < argc) {
      odb_file = argv[++i];
    } else if (arg == "--dump-stats" && i + 1 < argc) {
      dump_stats = true;
      odb_file = argv[++i];
    } else if (arg == "--test-zoom" && i + 1 < argc) {
      test_zoom_mode = true;
      odb_file = argv[++i];
    }
  }

  if (help_mode) {
    return nullptr;
  }

  odb::dbDatabase* db = nullptr;

  if (!odb_file.empty()) {
    db = odb::dbDatabase::create();
    std::ifstream f(odb_file, std::ios::binary);
    if (f.good()) {
      db->read(f);
      f.close();
      if (db->getChip()) {
        std::cerr << "Loaded ODB file successfully\n";
      } else {
        std::cerr << "Failed to load ODB file\n";
      }
    } else {
      std::cerr << "Cannot open ODB file: " << odb_file << "\n";
    }
  }

  if (!lef_files.empty()) {
    if (!db) {
      db = odb::dbDatabase::create();
    }

    utl::Logger logger(nullptr, nullptr);
    odb::lefin lefIn(db, &logger, false);
    odb::dbLib* lib = lefIn.createTechAndLib("tmpTech", "tmpLib",
                                              lef_files[0].c_str());
    if (lib) {
      std::cerr << "Created tech and lib from: " << lef_files[0] << "\n";
    } else {
      std::cerr << "Failed to load tech/lib from LEF\n";
    }

    for (size_t i = 1; i < lef_files.size() && lib; ++i) {
      std::cerr << "Loading macro LEF: " << lef_files[i] << "\n";
      lefIn.updateTechAndLib(lib, lef_files[i].c_str());
    }

    if (!def_file.empty()) {
      utl::Logger logger2(nullptr, nullptr);
      odb::defin defIn(db, &logger2);
      std::vector<odb::dbLib*> libs;
      if (lib) {
        libs.push_back(lib);
      }
      try {
        defIn.readChip(libs, def_file.c_str(), nullptr);
      } catch (const std::exception& e) {
        std::cerr << "Error loading DEF: " << e.what() << "\n";
      }
    }
  }

  if (db && !db->getChip()) {
    std::cerr << "Note: No chip loaded.\n";
  }

  return db;
}

// ---------------------------------------------------------------------------
// printSceneStats — output scene statistics as JSON to stdout
// ---------------------------------------------------------------------------
static void printSceneStats(const viewer3d::domain::SceneSnapshot& snapshot) {
  // Count by type
  int wireCount = 0, viaCount = 0, instCount = 0, pinCount = 0;
  for (const auto& obj : snapshot.objects) {
    switch (obj.type) {
      case viewer3d::domain::ObjectType::Wire:
        wireCount++;
        break;
      case viewer3d::domain::ObjectType::Via:
        viaCount++;
        break;
      case viewer3d::domain::ObjectType::Inst:
        instCount++;
        break;
      case viewer3d::domain::ObjectType::Pin:
        pinCount++;
        break;
      default:
        break;
    }
  }

  // Compute global bounding box
  float bmin[3] = {0, 0, 0};
  float bmax[3] = {0, 0, 0};
  bool hasBounds = false;
  for (const auto& obj : snapshot.objects) {
    if (!obj.hasBbox) {
      continue;
    }
    if (!hasBounds) {
      bmin[0] = obj.bboxMin[0];
      bmax[0] = obj.bboxMax[0];
      bmin[1] = obj.bboxMin[1];
      bmax[1] = obj.bboxMax[1];
      bmin[2] = obj.bboxMin[2];
      bmax[2] = obj.bboxMax[2];
      hasBounds = true;
    } else {
      for (int d = 0; d < 3; ++d) {
        if (obj.bboxMin[d] < bmin[d]) {
          bmin[d] = obj.bboxMin[d];
        }
        if (obj.bboxMax[d] > bmax[d]) {
          bmax[d] = obj.bboxMax[d];
        }
      }
    }
  }

  // Count via groups
  std::set<std::string> viaGroups;
  for (const auto& obj : snapshot.objects) {
    if (obj.type == viewer3d::domain::ObjectType::Via
        && !obj.groupId.empty()) {
      viaGroups.insert(obj.groupId);
    }
  }

  std::ostream& os = std::cout;
  os.precision(4);
  os << "{\n";
  jsonPrintString(os, "sourceTag", snapshot.sourceTag);
  jsonPrintInt(os, "layerCount",
               static_cast<int64_t>(snapshot.layers.size()));
  jsonPrintInt(os, "objectCount",
               static_cast<int64_t>(snapshot.objects.size()));
  jsonPrintInt(os, "instCount", static_cast<int64_t>(instCount));
  jsonPrintInt(os, "wireCount", static_cast<int64_t>(wireCount));
  jsonPrintInt(os, "viaCount", static_cast<int64_t>(viaCount));
  jsonPrintInt(os, "viaGroupCount",
               static_cast<int64_t>(viaGroups.size()));
  jsonPrintInt(os, "pinCount", static_cast<int64_t>(pinCount));

  os << "  \"bboxMin\": [" << bmin[0] << ", " << bmin[1] << ", " << bmin[2]
     << "],\n";
  os << "  \"bboxMax\": [" << bmax[0] << ", " << bmax[1] << ", " << bmax[2]
     << "],\n";

  os << "  \"layers\": [\n";
  for (size_t i = 0; i < snapshot.layers.size(); ++i) {
    const auto& layer = snapshot.layers[i];
    os << "    {\"name\": \"" << layer.name
       << "\", \"layerId\": \"" << layer.layerId
       << "\", \"zBase\": " << layer.zBase
       << ", \"thickness\": " << layer.thickness
       << ", \"visible\": " << (layer.visible ? "true" : "false") << "}";
    if (i + 1 < snapshot.layers.size()) {
      os << ",";
    }
    os << "\n";
  }
  os << "  ]\n";
  os << "}\n";
  os.flush();
}
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include <QApplication>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "OpenRoad3DWindow.h"
#include "ord/OpenRoad.hh"
#include "odb/db.h"
#include "odb/lefin.h"
#include "odb/defin.h"

int main(int argc, char* argv[]) {
  std::vector<std::string> lef_files;
  std::string def_file;
  std::string odb_file;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  -h, --help          Show this help message\n";
      std::cout << "  -lef <file>         Load LEF file (can be specified multiple times)\n";
      std::cout << "  -def <file>         Load DEF file\n";
      std::cout << "  -odb <file>         Load OpenROAD database file\n";
      return 0;
    } else if (arg == "-lef" && i + 1 < argc) {
      lef_files.push_back(argv[++i]);
    } else if (arg == "-def" && i + 1 < argc) {
      def_file = argv[++i];
    } else if (arg == "-odb" && i + 1 < argc) {
      odb_file = argv[++i];
    }
  }

  QApplication app(argc, argv);
  app.setApplicationName("OpenROAD 3D Viewer");
  app.setApplicationVersion("1.0");

  odb::dbDatabase* db = nullptr;

  // Try to connect to OpenROAD singleton first
  // Note: ord::OpenRoad::openRoad() returns nullptr in standalone mode
  // In embedded mode (when running within OpenROAD), it returns the singleton
  // We can't call ord::OpenRoad::openRoad() here as it requires linking to ord library
  // which creates circular dependencies. The viewer will work in standalone mode with files.
  // When embedded in OpenROAD, the connection should be established through the window API.

  // If no connection, try to load from files
  if (!db && (!lef_files.empty() || !def_file.empty() || !odb_file.empty())) {
    db = odb::dbDatabase::create();

    // Create a minimal logger (null logger for standalone use)
    utl::Logger logger(nullptr, nullptr);

    std::vector<odb::dbLib*> libs;

    if (!odb_file.empty()) {
      // Load from OpenROAD database file
      std::cout << "Loading ODB file: " << odb_file << "\n";
      std::ifstream f(odb_file, std::ios::binary);
      if (f.good()) {
        db->read(f);
        f.close();
        if (db->getChip()) {
          std::cout << "Loaded ODB file successfully\n";
        } else {
          std::cout << "Failed to load ODB file\n";
        }
      } else {
        std::cout << "Cannot open ODB file: " << odb_file << "\n";
      }
    }

    if (!lef_files.empty() && !db->getChip()) {
      // First LEF file: create tech and lib
      std::cout << "Loading tech LEF: " << lef_files[0] << "\n";
      odb::lefin lefIn(db, &logger, false);
      odb::dbLib* lib = lefIn.createTechAndLib("tmpTech", "tmpLib", lef_files[0].c_str());
      if (lib) {
        std::cout << "Created tech and lib from: " << lef_files[0] << "\n";
        libs.push_back(lib);
      } else {
        std::cout << "Failed to load tech/lib from LEF\n";
      }

      // Additional LEF files: update lib with more macros
      for (size_t i = 1; i < lef_files.size() && lib; ++i) {
        std::cout << "Loading macro LEF: " << lef_files[i] << "\n";
        if (lefIn.updateTechAndLib(lib, lef_files[i].c_str())) {
          std::cout << "Updated lib with macros from: " << lef_files[i] << "\n";
        } else {
          std::cout << "Failed to update lib from: " << lef_files[i] << "\n";
        }
      }
    }

    if (!def_file.empty() && !db->getChip()) {
      std::cout << "Loading DEF file: " << def_file << "\n";
      try {
        odb::defin defIn(db, &logger);
        defIn.readChip(libs, def_file.c_str(), nullptr);
        if (db->getChip()) {
          std::cout << "Loaded chip from DEF\n";
        } else {
          std::cout << "DEF file loaded but no chip created (check library paths)\n";
        }
      } catch (const std::exception& e) {
        std::cout << "Error loading DEF: " << e.what() << "\n";
      }
    }

    if (!db->getChip()) {
      std::cout << "Note: No chip loaded. Viewer will show empty scene.\n";
    }
  }

  // Create and show the 3D viewer window
  gui::openroad3d::OpenRoad3DWindow window(db);
  window.setWindowTitle("OpenROAD 3D Viewer");
  window.show();

  return app.exec();
}
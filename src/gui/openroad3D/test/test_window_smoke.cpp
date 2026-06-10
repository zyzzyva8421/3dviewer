// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors
//
// Smoke test for OpenRoad3DWindow.
// Verifies the window can be constructed without crashing and that the
// expected UI elements are present.
//
// Requires: QT_QPA_PLATFORM=offscreen (or a running X server)

#include <gtest/gtest.h>

#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "OpenRoad3DWindow.h"

namespace gui {
namespace openroad3d {
namespace test {

// ---------------------------------------------------------------------------
//  Setup — resolve ODB path for real-data tests
// ---------------------------------------------------------------------------
static std::string findOdbPath() {
  const char* env = std::getenv("OPENROAD_3D_TEST_ODB");
  if (env && env[0] != '\0') {
    return env;
  }
  const char* home = std::getenv("OPENROAD_HOME");
  if (home) {
    std::string candidate = std::string(home)
        + "/flow/results/nangate45/gcd/base/6_final.odb";
    std::ifstream probe(candidate, std::ios::binary);
    if (probe.good()) {
      probe.close();
      return candidate;
    }
  }
  return {};
}

static odb::dbDatabase* loadOdb(const std::string& path) {
  if (path.empty()) {
    return nullptr;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f.good()) {
    return nullptr;
  }
  odb::dbDatabase* db = odb::dbDatabase::create();
  db->read(f);
  f.close();
  return db;
}

// ---------------------------------------------------------------------------
//  Smoke test: window creation
// ---------------------------------------------------------------------------
TEST(WindowSmokeTest, CreateWithNullDb) {
  // Construct with null db — must not crash
  OpenRoad3DWindow window(nullptr);

  EXPECT_EQ(window.windowTitle().toStdString(), "OpenROAD 3D Viewer");
  EXPECT_GE(window.minimumWidth(), 800);
  EXPECT_GE(window.minimumHeight(), 600);
}

TEST(WindowSmokeTest, CreateWithEmptyDb) {
  odb::dbDatabase* db = odb::dbDatabase::create();

  OpenRoad3DWindow window(db);
  EXPECT_EQ(window.windowTitle().toStdString(), "OpenROAD 3D Viewer");

  odb::dbDatabase::destroy(db);
}

// ---------------------------------------------------------------------------
//  Smoke test: UI controls present
// ---------------------------------------------------------------------------
TEST(WindowSmokeTest, ControlsExist) {
  OpenRoad3DWindow window(nullptr);

  // Tool buttons
  auto* navigateBtn = window.findChild<QPushButton*>("navigateButton");
  ASSERT_NE(navigateBtn, nullptr);
  EXPECT_EQ(navigateBtn->text().toStdString(), "Navigate");

  auto* selectBtn = window.findChild<QPushButton*>("selectButton");
  ASSERT_NE(selectBtn, nullptr);
  EXPECT_EQ(selectBtn->text().toStdString(), "Select");

  // Anchor buttons
  auto* frontBtn = window.findChild<QPushButton*>("anchorFrontButton");
  ASSERT_NE(frontBtn, nullptr);
  EXPECT_EQ(frontBtn->text().toStdString(), "Front");

  auto* topBtn = window.findChild<QPushButton*>("anchorTopButton");
  ASSERT_NE(topBtn, nullptr);
  EXPECT_EQ(topBtn->text().toStdString(), "Top");
}

TEST(WindowSmokeTest, CheckboxesExist) {
  OpenRoad3DWindow window(nullptr);

  // Checkboxes (created without objectName in setupUi, find by text)
  auto allChecks = window.findChildren<QCheckBox*>();

  bool foundInst = false, foundWire = false, foundVia = false;
  for (auto* cb : allChecks) {
    std::string txt = cb->text().toStdString();
    if (txt == "Inst") foundInst = true;
    if (txt == "Wire") foundWire = true;
    if (txt == "Via")  foundVia = true;
  }
  EXPECT_TRUE(foundInst) << "Inst checkbox not found";
  EXPECT_TRUE(foundWire) << "Wire checkbox not found";
  EXPECT_TRUE(foundVia)  << "Via checkbox not found";
}

TEST(WindowSmokeTest, StatusLabelExists) {
  OpenRoad3DWindow window(nullptr);

  auto* status = window.findChild<QLabel*>("statusLabel");
  ASSERT_NE(status, nullptr);
  EXPECT_FALSE(status->text().isEmpty());
}

// ---------------------------------------------------------------------------
//  Smoke test: refresh after file load
// ---------------------------------------------------------------------------
TEST(WindowSmokeTest, DISABLED_RefreshWithRealOdb) {
  // Disabled by default — requires a GL context for the OSG backend.
  // Enable manually: --gtest_also_run_disabled_tests
  std::string path = findOdbPath();
  if (path.empty()) {
    GTEST_SKIP() << "No ODB file found";
  }

  odb::dbDatabase* db = loadOdb(path);
  ASSERT_NE(db, nullptr);

  OpenRoad3DWindow window(db);
  window.refreshView();

  odb::dbDatabase::destroy(db);
}

}  // namespace test
}  // namespace openroad3d
}  // namespace gui

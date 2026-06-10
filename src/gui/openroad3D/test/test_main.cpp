// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors
//
// Custom GTest main: initializes QApplication before any GUI tests.
// Non-GUI tests (like OdbSceneBuilder) can still link against gtest_main
// independently.

#include <gtest/gtest.h>

#include <QApplication>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // QApplication is required by any test that constructs a QWidget.
  // It is created here so all linked tests share one instance.
  QApplication app(argc, argv);
  app.setApplicationName("openroad3d_test");
  app.setApplicationVersion("1.0");

  return RUN_ALL_TESTS();
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <memory>
#include <string>

#include <QCheckBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "LayerControl.h"
#include "OdbSceneBuilder.h"
#include "render/api/IRenderBackend.h"

namespace viewer3d {
namespace render {
}
namespace domain {
struct SceneSnapshot;
}
}  // namespace viewer3d

namespace gui {
namespace openroad3d {

class OpenRoad3DWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit OpenRoad3DWindow(odb::dbDatabase* db, QWidget* parent = nullptr);
  ~OpenRoad3DWindow() override;

  // Refresh the 3D view from the current database state
  void refreshView();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void setupUi();
  void rebuildLayerList();
  void applyLayerVisibilityFromList(int row);
  void applyObjectFilters();
  void setToolMode(viewer3d::render::ToolMode mode);
  void dispatchViewKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
  void onLayerVisibilityChanged(const QString& layerId, bool visible);
  void onLayerSelectabilityChanged(const QString& layerId, bool selectable);
  void onLayerColorChanged(const QString& layerId, const QColor& color);

 private:
  odb::dbDatabase* db_;
  std::unique_ptr<OdbSceneBuilder> sceneBuilder_;

  // UI elements
  QWidget* rootWidget_;
  QWidget* viewContainer_;
  QVBoxLayout* viewLayout_;
  QWidget* currentView_;
  LayerControl* layerControl_;
  bool syncingLayerList_;

  // Toolbar controls
  QPushButton* reloadButton_;
  QLabel* statusLabel_;
  QCheckBox* showInstCheck_;
  QCheckBox* showWireCheck_;
  QCheckBox* showViaCheck_;
  QCheckBox* showNamesCheck_;

  // Tool buttons
  QPushButton* navigateButton_;
  QPushButton* selectButton_;
  QPushButton* rulerButton_;
  QPushButton* zoomToSelectionButton_;
  QPushButton* prevViewButton_;
  QPushButton* nextViewButton_;
  QPushButton* anchorFrontButton_;
  QPushButton* anchorBackButton_;
  QPushButton* anchorLeftButton_;
  QPushButton* anchorRightButton_;
  QPushButton* anchorTopButton_;
  QPushButton* anchorBottomButton_;

  // Backend
  std::unique_ptr<viewer3d::render::IRenderBackend> backend_;
  viewer3d::domain::SceneSnapshot snapshot_;
  bool sceneLoaded_ = false;
  viewer3d::render::ToolMode currentToolMode_;
  QString lastViewActionLabel_;
};

}  // namespace openroad3d
}  // namespace gui
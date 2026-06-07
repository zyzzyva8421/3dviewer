// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "OpenRoad3DWindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QWidget>
#include <set>

#include "LayerControl.h"
#include "odb/db.h"
#include "render/api/BackendFactory.h"
#include "render/api/IRenderBackend.h"
#include "core/domain/SceneTypes.h"

namespace gui {
namespace openroad3d {

OpenRoad3DWindow::OpenRoad3DWindow(odb::dbDatabase* db, QWidget* parent)
    : QMainWindow(parent),
      db_(db),
      syncingLayerList_(false),
      currentToolMode_(viewer3d::render::ToolMode::Navigate),
      lastViewActionLabel_("none") {
  sceneBuilder_ = std::make_unique<OdbSceneBuilder>(db);

  setWindowTitle("OpenROAD 3D Viewer");
  setMinimumSize(980, 680);
  resize(1100, 760);

  setupUi();
  // Don't call refreshView() here - wait until showEvent to ensure backend is initialized
}

void OpenRoad3DWindow::showEvent(QShowEvent* event) {
  // Call refreshView the first time the window is shown to ensure backend is initialized
  static bool firstShow = true;
  if (firstShow) {
    firstShow = false;
    refreshView();
  }
  QMainWindow::showEvent(event);
}

OpenRoad3DWindow::~OpenRoad3DWindow() {
  if (backend_) {
    backend_->shutdown();
  }
}

void OpenRoad3DWindow::setupUi() {
  // Root widget with horizontal layout (main area + side panel)
  rootWidget_ = new QWidget(this);
  auto* rootLayout = new QHBoxLayout(rootWidget_);

  // Main area (left side)
  auto* mainArea = new QWidget(rootWidget_);
  auto* mainLayout = new QVBoxLayout(mainArea);

  // Toolbar layout
  auto* toolbarLayout = new QHBoxLayout();

  reloadButton_ = new QPushButton("Reload Scene", mainArea);
  statusLabel_ = new QLabel("status: ready", mainArea);
  statusLabel_->setObjectName("statusLabel");
  statusLabel_->setFixedWidth(460);
  statusLabel_->setWordWrap(false);
  statusLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  showInstCheck_ = new QCheckBox("Inst", mainArea);
  showInstCheck_->setChecked(true);
  showWireCheck_ = new QCheckBox("Wire", mainArea);
  showWireCheck_->setChecked(true);
  showViaCheck_ = new QCheckBox("Via", mainArea);
  showViaCheck_->setChecked(true);

  toolbarLayout->addWidget(reloadButton_);
  toolbarLayout->addSpacing(8);
  toolbarLayout->addWidget(new QLabel("Types:", mainArea));
  toolbarLayout->addWidget(showInstCheck_);
  toolbarLayout->addWidget(showWireCheck_);
  toolbarLayout->addWidget(showViaCheck_);
  toolbarLayout->addStretch();
  toolbarLayout->addWidget(statusLabel_);

  // Tool layout
  auto* toolLayout = new QHBoxLayout();
  toolLayout->addWidget(new QLabel("Tools:", mainArea));

  navigateButton_ = new QPushButton("Navigate", mainArea);
  selectButton_ = new QPushButton("Select", mainArea);
  rulerButton_ = new QPushButton("Ruler", mainArea);
  zoomToSelectionButton_ = new QPushButton("Zoom to Select", mainArea);
  prevViewButton_ = new QPushButton("Prev View", mainArea);
  nextViewButton_ = new QPushButton("Next View", mainArea);
  anchorFrontButton_ = new QPushButton("Front", mainArea);
  anchorBackButton_ = new QPushButton("Back", mainArea);
  anchorLeftButton_ = new QPushButton("Left", mainArea);
  anchorRightButton_ = new QPushButton("Right", mainArea);
  anchorTopButton_ = new QPushButton("Top", mainArea);
  anchorBottomButton_ = new QPushButton("Bottom", mainArea);

  navigateButton_->setObjectName("navigateButton");
  selectButton_->setObjectName("selectButton");
  rulerButton_->setObjectName("rulerButton");
  zoomToSelectionButton_->setObjectName("zoomToSelectionButton");
  prevViewButton_->setObjectName("prevViewButton");
  nextViewButton_->setObjectName("nextViewButton");
  anchorFrontButton_->setObjectName("anchorFrontButton");
  anchorBackButton_->setObjectName("anchorBackButton");
  anchorLeftButton_->setObjectName("anchorLeftButton");
  anchorRightButton_->setObjectName("anchorRightButton");
  anchorTopButton_->setObjectName("anchorTopButton");
  anchorBottomButton_->setObjectName("anchorBottomButton");

  for (auto* button : {navigateButton_, selectButton_, rulerButton_, zoomToSelectionButton_,
                        prevViewButton_, nextViewButton_, anchorFrontButton_,
                        anchorBackButton_, anchorLeftButton_, anchorRightButton_,
                        anchorTopButton_, anchorBottomButton_}) {
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    toolLayout->addWidget(button);
  }
  toolLayout->addStretch();

  // View container
  viewContainer_ = new QWidget(mainArea);
  viewContainer_->setMinimumSize(600, 400);
  viewLayout_ = new QVBoxLayout(viewContainer_);
  viewLayout_->setContentsMargins(0, 0, 0, 0);

  mainLayout->addLayout(toolbarLayout);
  mainLayout->addLayout(toolLayout);
  mainLayout->addWidget(viewContainer_);
  mainArea->setLayout(mainLayout);

  // Side panel (right side) - layer control
  auto* sidePanel = new QWidget(rootWidget_);
  sidePanel->setFixedWidth(280);
  sidePanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  auto* sideLayout = new QVBoxLayout(sidePanel);
  auto* layerTitle = new QLabel("Layers", sidePanel);
  layerControl_ = new LayerControl(sidePanel);
  sideLayout->addWidget(layerTitle);
  sideLayout->addWidget(layerControl_);
  sidePanel->setLayout(sideLayout);

  rootLayout->addWidget(mainArea, 1);
  rootLayout->addWidget(sidePanel);
  rootWidget_->setLayout(rootLayout);

  setCentralWidget(rootWidget_);

  // Create OSG backend
  backend_ = viewer3d::render::BackendFactory::create(
      viewer3d::render::BackendType::Osg);

  if (backend_) {
    backend_->initialize();
    currentView_ = backend_->createViewWidget(viewContainer_);
    if (currentView_) {
      currentView_->setObjectName("renderView");
      viewLayout_->addWidget(currentView_);
    }
    backend_->setToolMode(currentToolMode_);
  }

  // Connect signals - tools
  connect(navigateButton_, &QPushButton::clicked, this, [this]() {
    setToolMode(viewer3d::render::ToolMode::Navigate);
  });
  connect(selectButton_, &QPushButton::clicked, this, [this]() {
    setToolMode(viewer3d::render::ToolMode::Select);
  });
  connect(rulerButton_, &QPushButton::clicked, this, [this]() {
    setToolMode(viewer3d::render::ToolMode::Ruler);
  });
  connect(zoomToSelectionButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_Z);
  });
  connect(prevViewButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_Left, Qt::AltModifier);
  });
  connect(nextViewButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_Right, Qt::AltModifier);
  });
  connect(anchorFrontButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_1);
  });
  connect(anchorBackButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_2);
  });
  connect(anchorLeftButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_3);
  });
  connect(anchorRightButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_4);
  });
  connect(anchorTopButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_5);
  });
  connect(anchorBottomButton_, &QPushButton::clicked, this, [this]() {
    dispatchViewKey(Qt::Key_6);
  });

  // Connect signals - reload and filters
  connect(reloadButton_, &QPushButton::clicked, this, &OpenRoad3DWindow::refreshView);

  connect(showInstCheck_, &QCheckBox::toggled, this, &OpenRoad3DWindow::applyObjectFilters);
  connect(showWireCheck_, &QCheckBox::toggled, this, &OpenRoad3DWindow::applyObjectFilters);
  connect(showViaCheck_, &QCheckBox::toggled, this, &OpenRoad3DWindow::applyObjectFilters);

  // Connect signals - layer control
  connect(layerControl_, &LayerControl::layerVisibilityChanged, this, &OpenRoad3DWindow::onLayerVisibilityChanged);
  connect(layerControl_, &LayerControl::layerSelectabilityChanged, this, &OpenRoad3DWindow::onLayerSelectabilityChanged);
  connect(layerControl_, &LayerControl::layerColorChanged, this, &OpenRoad3DWindow::onLayerColorChanged);
}

void OpenRoad3DWindow::refreshView() {
  if (!sceneBuilder_) {
    return;
  }

  statusLabel_->setText("Loading...");

  // Build scene from database
  snapshot_ = sceneBuilder_->build();

  // Update backend with new scene
  if (backend_) {
    backend_->loadScene(snapshot_);
    for (const auto& layer : snapshot_.layers) {
      backend_->updateLayerState(layer);
    }
  }

  // Rebuild layer list
  rebuildLayerList();

  // Count wires and vias - count via by groupId (top+bottom+cut = 1 via)
  int wireCount = 0;
  std::set<std::string> viaGroups;
  for (const auto& obj : snapshot_.objects) {
    if (obj.type == viewer3d::domain::ObjectType::Wire) {
      wireCount++;
    } else if (obj.type == viewer3d::domain::ObjectType::Via) {
      if (!obj.groupId.empty()) {
        viaGroups.insert(obj.groupId);
      }
    }
  }

  // Debug: print all layers with layerId
  fprintf(stderr, "DEBUG: Layers (%zu):\n", snapshot_.layers.size());
  for (const auto& layer : snapshot_.layers) {
    fprintf(stderr, "  - %s (layerId=%s, zBase=%.2f)\n", layer.name.c_str(), layer.layerId.c_str(), layer.zBase);
  }

  statusLabel_->setText(
      QString("status: %1 wires, %2 vias, %3 objects, %4 layers, backend=%5")
          .arg(wireCount)
          .arg((int)viaGroups.size())
          .arg(snapshot_.objects.size())
          .arg(snapshot_.layers.size())
          .arg(backend_ ? QString::fromStdString(backend_->name()) : "none"));
}

void OpenRoad3DWindow::rebuildLayerList() {
  layerControl_->blockSignals(true);

  for (const auto& layer : snapshot_.layers) {
    LayerSettings settings;
    settings.layerId = layer.layerId;
    settings.name = layer.name;
    settings.color = QColor(int(layer.color.r * 255), int(layer.color.g * 255), int(layer.color.b * 255));
    settings.visible = layer.visible;
    settings.selectable = layer.selectable;
    layerControl_->addLayer(settings);
  }

  layerControl_->blockSignals(false);
}

void OpenRoad3DWindow::onLayerVisibilityChanged(const QString& layerId, bool visible) {
  // Update layer visibility in snapshot
  for (auto& layer : snapshot_.layers) {
    if (layer.layerId == layerId.toStdString()) {
      layer.visible = visible;
      if (backend_) {
        backend_->updateLayerState(layer);
      }
      break;
    }
  }
}

void OpenRoad3DWindow::onLayerSelectabilityChanged(const QString& layerId, bool selectable) {
  // Update layer selectability in snapshot
  for (auto& layer : snapshot_.layers) {
    if (layer.layerId == layerId.toStdString()) {
      layer.selectable = selectable;
      if (backend_) {
        backend_->updateLayerState(layer);
      }
      break;
    }
  }
}

void OpenRoad3DWindow::onLayerColorChanged(const QString& layerId, const QColor& color) {
  // Update layer color in snapshot
  for (auto& layer : snapshot_.layers) {
    if (layer.layerId == layerId.toStdString()) {
      layer.color.r = color.red() / 255.0f;
      layer.color.g = color.green() / 255.0f;
      layer.color.b = color.blue() / 255.0f;
      if (backend_) {
        backend_->updateLayerState(layer);
      }
      break;
    }
  }
}

void OpenRoad3DWindow::applyObjectFilters() {
  if (!backend_) {
    return;
  }

  // Filter objects by type based on checkboxes
  for (auto& obj : snapshot_.objects) {
    switch (obj.type) {
      case viewer3d::domain::ObjectType::Inst:
        obj.hasBbox = showInstCheck_->isChecked();
        break;
      case viewer3d::domain::ObjectType::Wire:
        obj.hasBbox = showWireCheck_->isChecked();
        break;
      case viewer3d::domain::ObjectType::Via:
        obj.hasBbox = showViaCheck_->isChecked();
        break;
      default:
        break;
    }
  }

  backend_->loadScene(snapshot_);
}

void OpenRoad3DWindow::setToolMode(viewer3d::render::ToolMode mode) {
  currentToolMode_ = mode;
  if (backend_) {
    backend_->setToolMode(mode);
  }
}

void OpenRoad3DWindow::dispatchViewKey(int key, Qt::KeyboardModifiers modifiers) {
  if (!currentView_) {
    return;
  }

  // Map key to action name
  if (key == Qt::Key_Z) {
    lastViewActionLabel_ = "zoom-selected";
  } else if (key == Qt::Key_Left && (modifiers & Qt::AltModifier)) {
    lastViewActionLabel_ = "view-previous";
  } else if (key == Qt::Key_Right && (modifiers & Qt::AltModifier)) {
    lastViewActionLabel_ = "view-next";
  } else if (key == Qt::Key_1) {
    lastViewActionLabel_ = "anchor-front";
  } else if (key == Qt::Key_2) {
    lastViewActionLabel_ = "anchor-back";
  } else if (key == Qt::Key_3) {
    lastViewActionLabel_ = "anchor-left";
  } else if (key == Qt::Key_4) {
    lastViewActionLabel_ = "anchor-right";
  } else if (key == Qt::Key_5) {
    lastViewActionLabel_ = "anchor-top";
  } else if (key == Qt::Key_6) {
    lastViewActionLabel_ = "anchor-bottom";
  }

  // Send key events directly to the view widget
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(currentView_, &press);
  QApplication::sendEvent(currentView_, &release);
}

}  // namespace openroad3d
}  // namespace gui
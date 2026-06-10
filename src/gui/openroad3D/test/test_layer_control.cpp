// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors
//
// Unit tests for LayerControl widget (pure Qt, no OSG needed).

#include <gtest/gtest.h>

#include <QApplication>
#include <QStandardItemModel>
#include <QTreeView>
#include <memory>
#include <string>

#ifdef HAS_QT_TEST
#include <QSignalSpy>
#endif

#include "LayerControl.h"

namespace gui {
namespace openroad3d {
namespace test {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static LayerSettings makeLayer(const std::string& id,
                               const std::string& name,
                               const QColor& color,
                               bool visible = true,
                               bool selectable = true) {
  LayerSettings s;
  s.layerId = id;
  s.name = name;
  s.color = color;
  s.visible = visible;
  s.selectable = selectable;
  return s;
}

// ---------------------------------------------------------------------------
//  LayerControlTest — fixture that owns a LayerControl
// ---------------------------------------------------------------------------
class LayerControlTest : public ::testing::Test {
 protected:
  std::unique_ptr<LayerControl> control_;

  void SetUp() override {
    control_ = std::make_unique<LayerControl>();
  }

  void TearDown() override {
    control_.reset();
  }
};

// ---------------------------------------------------------------------------
//  Basic CRUD
// ---------------------------------------------------------------------------

TEST_F(LayerControlTest, InitiallyEmpty) {
  auto layers = control_->getAllLayers();
  EXPECT_TRUE(layers.empty());
}

TEST_F(LayerControlTest, AddSingleLayer) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));
  auto layers = control_->getAllLayers();
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].layerId, "metal1");
  EXPECT_EQ(layers[0].name, "Metal 1");
}

TEST_F(LayerControlTest, AddMultipleLayers) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));
  control_->addLayer(makeLayer("metal2", "Metal 2", QColor(50, 200, 50)));
  control_->addLayer(makeLayer("via1",   "Via 1",   QColor(200, 200, 50)));

  auto layers = control_->getAllLayers();
  ASSERT_EQ(layers.size(), 3u);
  EXPECT_EQ(layers[0].layerId, "metal1");
  EXPECT_EQ(layers[1].layerId, "metal2");
  EXPECT_EQ(layers[2].layerId, "via1");
}

TEST_F(LayerControlTest, AddLayerTwiceUpdates) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));
  control_->addLayer(makeLayer("metal1", "Metal 1 Updated", QColor(50, 200, 200)));

  // Must be one entry, with updated color
  auto layers = control_->getAllLayers();
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].name, "Metal 1 Updated");  // name is set by LayerSettings
  EXPECT_EQ(layers[0].color, QColor(50, 200, 200));
}

// ---------------------------------------------------------------------------
//  getLayerSettings
// ---------------------------------------------------------------------------

TEST_F(LayerControlTest, GetLayerSettingsFound) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));
  auto settings = control_->getLayerSettings("metal1");
  EXPECT_EQ(settings.layerId, "metal1");
  EXPECT_EQ(settings.name, "Metal 1");
}

TEST_F(LayerControlTest, GetLayerSettingsNotFound) {
  auto settings = control_->getLayerSettings("nonexistent");
  // Should return a default-constructed LayerSettings
  EXPECT_TRUE(settings.layerId.empty());
}

// ---------------------------------------------------------------------------
//  updateLayer
// ---------------------------------------------------------------------------

TEST_F(LayerControlTest, UpdateLayerVisibility) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));

  auto updated = makeLayer("metal1", "Metal 1", QColor(200, 50, 50));
  updated.visible = false;
  control_->updateLayer(updated);

  auto settings = control_->getLayerSettings("metal1");
  EXPECT_FALSE(settings.visible);
}

TEST_F(LayerControlTest, UpdateLayerSelectability) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));

  auto updated = makeLayer("metal1", "Metal 1", QColor(200, 50, 50));
  updated.selectable = false;
  control_->updateLayer(updated);

  auto settings = control_->getLayerSettings("metal1");
  EXPECT_FALSE(settings.selectable);
}

TEST_F(LayerControlTest, UpdateLayerColor) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));

  auto updated = makeLayer("metal1", "Metal 1", QColor(50, 200, 200));
  control_->updateLayer(updated);

  auto settings = control_->getLayerSettings("metal1");
  EXPECT_EQ(settings.color, QColor(50, 200, 200));
}

// ---------------------------------------------------------------------------
//  Model row state
// ---------------------------------------------------------------------------

TEST_F(LayerControlTest, LayerRowCountAfterAdd) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));
  control_->addLayer(makeLayer("metal2", "Metal 2", QColor(50, 200, 50)));

  // Access the underlying model through the view
  // The tree view is private — we verify indirectly via getAllLayers
  auto layers = control_->getAllLayers();
  ASSERT_EQ(layers.size(), 2u);
}

TEST_F(LayerControlTest, VisibilityAndSelectabilityDefaults) {
  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));

  auto settings = control_->getLayerSettings("metal1");
  EXPECT_TRUE(settings.visible);
  EXPECT_TRUE(settings.selectable);
}

// ---------------------------------------------------------------------------
//  Signal emission (via QSignalSpy) — only when Qt5::Test is available
// ---------------------------------------------------------------------------

#ifdef HAS_QT_TEST
TEST_F(LayerControlTest, VisibilityChangedSignal) {
  QSignalSpy spy(control_.get(), &LayerControl::layerVisibilityChanged);

  control_->addLayer(makeLayer("metal1", "Metal 1", QColor(200, 50, 50)));

  // Update visibility via model programmatically — the model's setData
  // triggers the signal through LayerControlModel.
  // The addLayer -> updateLayer path doesn't emit signals.
  // To test signal emission, we manipulate the model directly.
  auto layers = control_->getAllLayers();
  ASSERT_EQ(layers.size(), 1u);

  QStandardItemModel* model = control_->findChild<QStandardItemModel*>();
  ASSERT_NE(model, nullptr);

  // Find the visibility column item
  for (int row = 0; row < model->rowCount(); ++row) {
    QStandardItem* nameItem
        = dynamic_cast<QStandardItem*>(model->item(row, LayerControlModel::Name));
    if (nameItem && nameItem->data(Qt::UserRole + 1).toString()
                        == QString::fromStdString("metal1")) {
      QModelIndex visibleIdx = model->index(row, LayerControlModel::Visible);
      model->setData(visibleIdx, Qt::Unchecked, Qt::CheckStateRole);
      break;
    }
  }

  // Signal should have been emitted
  EXPECT_GE(spy.count(), 1);
  if (spy.count() > 0) {
    QString layerId = spy.at(0).at(0).toString();
    bool visible = spy.at(0).at(1).toBool();
    EXPECT_EQ(layerId.toStdString(), "metal1");
    EXPECT_FALSE(visible);
  }
}
#endif  // HAS_QT_TEST

}  // namespace test
}  // namespace openroad3d
}  // namespace gui

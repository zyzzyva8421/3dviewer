// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

namespace viewer3d {
namespace domain {
struct LayerRecord;
}
}  // namespace viewer3d

namespace gui {
namespace openroad3d {

// Layer settings for one layer
struct LayerSettings {
  std::string layerId;
  std::string name;
  QColor color;
  bool visible = true;
  bool selectable = true;
};

// Callback when layer settings change
using LayerChangeCallback = std::function<void(const LayerSettings&)>;

class LayerControlModel : public QStandardItemModel {
  Q_OBJECT

 public:
  enum Column { Name = 0, Color = 1, Visible = 2, Selectable = 3, ColumnCount = 4 };

  explicit LayerControlModel(QWidget* parent = nullptr);

  QVariant data(const QModelIndex& index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role) override;

 signals:
  void layerVisibilityChanged(const QString& layerId, bool visible);
  void layerSelectabilityChanged(const QString& layerId, bool selectable);
  void layerColorChanged(const QString& layerId, const QColor& color);

 private:
  QString layerIdFromIndex(const QModelIndex& index) const;
};

// Layer control widget - displays layers with visibility, selectability, and color controls
class LayerControl : public QWidget {
  Q_OBJECT

 public:
  explicit LayerControl(QWidget* parent = nullptr);
  ~LayerControl() override;

  // Add a layer to the control
  void addLayer(const LayerSettings& layer);

  // Update a layer's settings
  void updateLayer(const LayerSettings& layer);

  // Get current settings for a layer
  LayerSettings getLayerSettings(const std::string& layerId) const;

  // Get all layers
  std::vector<LayerSettings> getAllLayers() const;

  // Connect to receive layer change callbacks
  void setLayerChangeCallback(LayerChangeCallback callback);

 signals:
  void layerVisibilityChanged(const QString& layerId, bool visible);
  void layerSelectabilityChanged(const QString& layerId, bool selectable);
  void layerColorChanged(const QString& layerId, const QColor& color);

 private slots:
  void onItemChanged(QStandardItem* item);
  void onColorButtonClicked(const QModelIndex& index);

 private:
  void setupUi();
  void updateLayerInModel(const LayerSettings& layer);

  LayerControlModel* model_;
  QTreeView* view_;
  std::map<QString, LayerSettings> layers_;
  LayerChangeCallback layerCallback_;
};

// Color picker button for layer color
class ColorButton : public QWidget {
  Q_OBJECT

 public:
  ColorButton(const QColor& color, QWidget* parent = nullptr);
  ~ColorButton() override;

  QColor color() const { return color_; }
  void setColor(const QColor& color);

 signals:
  void colorChanged(const QColor& color);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  QColor color_;
};

}  // namespace openroad3d
}  // namespace gui

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "LayerControl.h"

#include <QAbstractItemModel>
#include <QBrush>
#include <QColorDialog>
#include <QEvent>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QPainter>
#include <QStandardItem>

namespace gui {
namespace openroad3d {

///////////////////////// LayerControlModel /////////////////////////////

LayerControlModel::LayerControlModel(QWidget* parent)
    : QStandardItemModel(0, ColumnCount, parent) {
  setHorizontalHeaderLabels({tr("Layer"), tr("Color"), tr("Visible"), tr("Selectable")});
}

QVariant LayerControlModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  const int col = index.column();
  QStandardItem* item = QStandardItemModel::item(index.row(), col);
  if (!item) {
    return QVariant();
  }

  if (role == Qt::CheckStateRole && (col == Visible || col == Selectable)) {
    return item->checkState();
  }

  if (role == Qt::BackgroundRole && col == Color) {
    QString layerId = item->data(Qt::UserRole + 1).toString();
    if (!layerId.isEmpty()) {
      QStandardItem* nameItem = itemFromIndex(index.sibling(index.row(), Name));
      if (nameItem) {
        QVariant colorVar = nameItem->data(Qt::UserRole + 2);
        if (colorVar.isValid()) {
          return QBrush(colorVar.value<QColor>());
        }
      }
    }
  }

  return QStandardItemModel::data(index, role);
}

Qt::ItemFlags LayerControlModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags flags = QStandardItemModel::flags(index);
  const int col = index.column();

  if (col == Visible || col == Selectable) {
    return flags | Qt::ItemIsUserCheckable;
  } else if (col == Color) {
    return flags | Qt::ItemIsEditable;
  }

  return flags;
}

bool LayerControlModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (!index.isValid()) {
    return false;
  }

  const int col = index.column();
  QStandardItem* item = QStandardItemModel::item(index.row(), col);
  if (!item) {
    return false;
  }

  if (role == Qt::CheckStateRole && (col == Visible || col == Selectable)) {
    item->setCheckState(static_cast<Qt::CheckState>(value.toInt()));
    QString layerId = item->data(Qt::UserRole + 1).toString();
    if (col == Visible) {
      emit layerVisibilityChanged(layerId, value.toInt() == Qt::Checked);
    } else {
      emit layerSelectabilityChanged(layerId, value.toInt() == Qt::Checked);
    }
    return true;
  }

  return QStandardItemModel::setData(index, value, role);
}

QString LayerControlModel::layerIdFromIndex(const QModelIndex& index) const {
  QStandardItem* nameItem = itemFromIndex(index.sibling(index.row(), Name));
  if (nameItem) {
    return nameItem->data(Qt::UserRole + 1).toString();
  }
  return QString();
}

///////////////////////// LayerControl /////////////////////////////

LayerControl::LayerControl(QWidget* parent) : QWidget(parent) {
  setupUi();
}

LayerControl::~LayerControl() = default;

void LayerControl::setupUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);

  model_ = new LayerControlModel(this);
  view_ = new QTreeView(this);
  view_->setModel(model_);
  view_->setAlternatingRowColors(true);
  view_->header()->setStretchLastSection(false);
  view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  view_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  view_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  view_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
  view_->setColumnWidth(0, 120);
  view_->setColumnWidth(1, 50);
  view_->setColumnWidth(2, 60);
  view_->setColumnWidth(3, 70);
  view_->setItemsExpandable(false);
  view_->setRootIsDecorated(false);

  layout->addWidget(view_);

  connect(model_, &LayerControlModel::layerVisibilityChanged, this, &LayerControl::layerVisibilityChanged);
  connect(model_, &LayerControlModel::layerSelectabilityChanged, this, &LayerControl::layerSelectabilityChanged);
  connect(model_, &LayerControlModel::layerColorChanged, this, &LayerControl::layerColorChanged);
}

void LayerControl::addLayer(const LayerSettings& layer) {
  QString layerId = QString::fromStdString(layer.layerId);
  if (layers_.contains(layerId)) {
    updateLayer(layer);
    return;
  }

  layers_[layerId] = layer;

  QList<QStandardItem*> row;
  
  // Name item
  auto* nameItem = new QStandardItem(QString::fromStdString(layer.name));
  nameItem->setData(layerId, Qt::UserRole + 1);
  nameItem->setData(QVariant::fromValue(layer.color), Qt::UserRole + 2);
  nameItem->setEditable(false);
  row.append(nameItem);

  // Color item (shows as swatch)
  auto* colorItem = new QStandardItem();
  colorItem->setData(QVariant::fromValue(layer.color), Qt::DecorationRole);
  colorItem->setData(layerId, Qt::UserRole + 1);
  colorItem->setEditable(false);
  row.append(colorItem);

  // Visible checkbox
  auto* visibleItem = new QStandardItem();
  visibleItem->setCheckable(true);
  visibleItem->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
  visibleItem->setData(layerId, Qt::UserRole + 1);
  row.append(visibleItem);

  // Selectable checkbox
  auto* selectableItem = new QStandardItem();
  selectableItem->setCheckable(true);
  selectableItem->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
  selectableItem->setData(layerId, Qt::UserRole + 1);
  row.append(selectableItem);

  model_->appendRow(row);
}

void LayerControl::updateLayer(const LayerSettings& layer) {
  QString layerId = QString::fromStdString(layer.layerId);
  layers_[layerId] = layer;
  updateLayerInModel(layer);
}

void LayerControl::updateLayerInModel(const LayerSettings& layer) {
  QString layerId = QString::fromStdString(layer.layerId);
  
  for (int row = 0; row < model_->rowCount(); ++row) {
    QStandardItem* nameItem = model_->item(row, LayerControlModel::Name);
    if (nameItem && nameItem->data(Qt::UserRole + 1).toString() == layerId) {
      // Update color
      QStandardItem* colorItem = model_->item(row, LayerControlModel::Color);
      if (colorItem) {
        colorItem->setData(QVariant::fromValue(layer.color), Qt::DecorationRole);
        nameItem->setData(QVariant::fromValue(layer.color), Qt::UserRole + 2);
      }
      
      // Update visible
      QStandardItem* visibleItem = model_->item(row, LayerControlModel::Visible);
      if (visibleItem) {
        visibleItem->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
      }
      
      // Update selectable
      QStandardItem* selectableItem = model_->item(row, LayerControlModel::Selectable);
      if (selectableItem) {
        selectableItem->setCheckState(layer.selectable ? Qt::Checked : Qt::Unchecked);
      }
      
      return;
    }
  }
}

LayerSettings LayerControl::getLayerSettings(const std::string& layerId) const {
  QString qLayerId = QString::fromStdString(layerId);
  auto it = layers_.find(qLayerId);
  if (it != layers_.end()) {
    return it->second;
  }
  return LayerSettings();
}

std::vector<LayerSettings> LayerControl::getAllLayers() const {
  std::vector<LayerSettings> result;
  for (const auto& pair : layers_) {
    result.push_back(pair.second);
  }
  return result;
}

void LayerControl::setLayerChangeCallback(LayerChangeCallback callback) {
  layerCallback_ = callback;
}

void LayerControl::onItemChanged(QStandardItem* item) {
  if (!item) {
    return;
  }
  
  QString layerId = item->data(Qt::UserRole + 1).toString();
  if (layerId.isEmpty()) {
    return;
  }

  int col = item->column();
  if (col == LayerControlModel::Visible) {
    bool visible = item->checkState() == Qt::Checked;
    layers_[layerId].visible = visible;
    emit layerVisibilityChanged(layerId, visible);
    if (layerCallback_) {
      layerCallback_(layers_[layerId]);
    }
  } else if (col == LayerControlModel::Selectable) {
    bool selectable = item->checkState() == Qt::Checked;
    layers_[layerId].selectable = selectable;
    emit layerSelectabilityChanged(layerId, selectable);
    if (layerCallback_) {
      layerCallback_(layers_[layerId]);
    }
  }
}

void LayerControl::onColorButtonClicked(const QModelIndex& index) {
  if (index.column() != LayerControlModel::Color) {
    return;
  }

  QStandardItem* colorItem = model_->itemFromIndex(index);
  if (!colorItem) {
    return;
  }

  QString layerId = colorItem->data(Qt::UserRole + 1).toString();
  if (layerId.isEmpty()) {
    return;
  }

  QColor currentColor = layers_[layerId].color;
  QColor newColor = QColorDialog::getColor(currentColor, this, "Select Layer Color");
  if (newColor.isValid() && newColor != currentColor) {
    layers_[layerId].color = newColor;
    
    // Update the color swatch
    colorItem->setData(QVariant::fromValue(newColor), Qt::DecorationRole);
    
    // Also update the name item's stored color
    QModelIndex nameIndex = index.sibling(index.row(), LayerControlModel::Name);
    QStandardItem* nameItem = model_->itemFromIndex(nameIndex);
    if (nameItem) {
      nameItem->setData(QVariant::fromValue(newColor), Qt::UserRole + 2);
    }
    
    emit layerColorChanged(layerId, newColor);
    if (layerCallback_) {
      layerCallback_(layers_[layerId]);
    }
  }
}

///////////////////////// ColorButton /////////////////////////////

ColorButton::ColorButton(const QColor& color, QWidget* parent)
    : QWidget(parent), color_(color) {
  setMinimumSize(24, 24);
  setMaximumSize(24, 24);
}

ColorButton::~ColorButton() = default;

void ColorButton::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.fillRect(rect(), color_);
  QPen pen(Qt::black, 1);
  painter.setPen(pen);
  painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ColorButton::mousePressEvent(QMouseEvent* event) {
  QColor newColor = QColorDialog::getColor(color_, this, "Select Color");
  if (newColor.isValid() && newColor != color_) {
    color_ = newColor;
    update();
    emit colorChanged(color_);
  }
}

void ColorButton::setColor(const QColor& color) {
  color_ = color;
  update();
}

}  // namespace openroad3d
}  // namespace gui

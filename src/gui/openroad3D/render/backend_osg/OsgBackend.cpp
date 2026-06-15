#include "render/backend_osg/OsgBackend.h"

#include "render/backend_osg/OsgScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>

#include <QContextMenuEvent>
#include <QMenu>
#include <unordered_set>

#include <QColor>
#include <QImage>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPen>
#include <QSurfaceFormat>
#include <QWidget>

#include <osg/Geode>
#include <osg/LineWidth>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

namespace viewer3d {
namespace render {
namespace backend_osg {

class OsgViewWidget final : public QOpenGLWidget {
 public:
  explicit OsgViewWidget(OsgBackend* backend, QWidget* parent = nullptr)
      : QOpenGLWidget(parent), backend_(backend) {
    setFocusPolicy(Qt::NoFocus);
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);
  }

 protected:
  void initializeGL() override {
    if (backend_) {
      backend_->syncGraphicsContext(width(), height(), defaultFramebufferObject());
      backend_->updateViewport(width(), height());
    }
  }

  void resizeGL(int width, int height) override {
    if (backend_) {
      backend_->syncGraphicsContext(width, height, defaultFramebufferObject());
      backend_->updateViewport(width, height);
    }
  }

  void paintGL() override {
    if (!backend_) {
      return;
    }
    backend_->syncGraphicsContext(width(), height(), defaultFramebufferObject());
    backend_->renderFrame();
  }

 private:
  OsgBackend* backend_ = nullptr;
};

class OsgViewportWidget final : public QWidget {
 public:
  class OverlayWidget final : public QWidget {
   public:
    explicit OverlayWidget(OsgBackend* backend, QWidget* parent = nullptr)
        : QWidget(parent), backend_(backend) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_AlwaysStackOnTop, true);
    }

   protected:
    void paintEvent(QPaintEvent* event) override {
      QWidget::paintEvent(event);
      if (!backend_) {
        return;
      }
      QPainter painter(this);
      painter.setRenderHint(QPainter::Antialiasing, true);
      backend_->drawOverlay(painter, size());
    }

   private:
    OsgBackend* backend_ = nullptr;
  };

  explicit OsgViewportWidget(OsgBackend* backend, QWidget* parent = nullptr)
      : QWidget(parent), backend_(backend) {
    setMinimumSize(640, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    glView_ = new OsgViewWidget(backend, this);
    glView_->setObjectName("osgGlSurface");
    glView_->setGeometry(rect());
    overlay_ = new OverlayWidget(backend, this);
    overlay_->setObjectName("osgOverlay");
    overlay_->setGeometry(rect());
    overlay_->raise();
  }

  OsgViewWidget* glView() const {
    return glView_;
  }

  OverlayWidget* overlay() const {
    return overlay_;
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    if (glView_) {
      glView_->setGeometry(rect());
    }
    if (overlay_) {
      overlay_->setGeometry(rect());
      overlay_->raise();
    }
    QWidget::resizeEvent(event);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (backend_) {
      backend_->handleMousePress(event);
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (backend_) {
      // Tooltip: pick object under cursor when not dragging
      if (!(event->buttons() & Qt::MouseButtonMask)) {
        QSize size = rect().size();
        std::string objId;
        osg::Vec3 worldPt;
        if (backend_->pickObjectAt(event->pos(), size, &objId, &worldPt)) {
          std::string tip;
          for (const auto& obj : backend_->snapshot_.objects) {
            if (obj.objectId == objId) {
              tip = obj.displayName.empty() ? objId : obj.displayName;
              tip += " [" + obj.layerId + "]";
              break;
            }
          }
          if (!tip.empty()) setToolTip(QString::fromStdString(tip));
        } else {
          setToolTip("");
        }
      }
      backend_->handleMouseMove(event);
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (backend_) {
      backend_->handleMouseRelease(event);
    }
    QWidget::mouseReleaseEvent(event);
  }

  void wheelEvent(QWheelEvent* event) override {
    if (backend_) {
      backend_->handleWheel(event);
    }
    QWidget::wheelEvent(event);
  }

  void contextMenuEvent(QContextMenuEvent* event) override {
    if (backend_) {
      backend_->handleContextMenu(event);
    }
    QWidget::contextMenuEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (backend_) {
      backend_->handleKeyPress(event);
    }
    QWidget::keyPressEvent(event);
  }

 private:
  OsgBackend* backend_ = nullptr;
  OsgViewWidget* glView_ = nullptr;
  OverlayWidget* overlay_ = nullptr;
};

namespace {

// These are now O(1) via index maps in OsgBackend
const domain::LayerRecord* findLayerRecord(const domain::SceneSnapshot& /*snapshot*/,
                                           const std::string& /*layerId*/) {
  // Deprecated: use objectIndex_/layerIndex_ in OsgBackend instead
  return nullptr;
}

const domain::ObjectRecord* findObjectRecord(const domain::SceneSnapshot& /*snapshot*/,
                                             const std::string& /*objectId*/) {
  // Deprecated: use objectIndex_/layerIndex_ in OsgBackend instead
  return nullptr;
}

const char* objectTypeText(domain::ObjectType type) {
  switch (type) {
    case domain::ObjectType::Wire:
      return "wire";
    case domain::ObjectType::Via:
      return "via";
    case domain::ObjectType::Inst:
      return "inst";
    case domain::ObjectType::Pin:
      return "pin";
    case domain::ObjectType::Drc:
      return "drc";
    case domain::ObjectType::Blockage:
      return "blockage";
    case domain::ObjectType::Track:
      return "track";
    case domain::ObjectType::Bump:
      return "bump";
    case domain::ObjectType::Text:
      return "text";
    default:
      return "unknown";
  }
}

std::array<osg::Vec3, 8> objectCorners(const domain::ObjectRecord& object,
                                       const domain::LayerRecord* layer) {
  const float zBase = layer ? layer->zBase : 0.0F;
  const float thickness = layer ? std::max(layer->thickness, 0.2F) : 0.2F;

  osg::Vec3 minPoint;
  osg::Vec3 maxPoint;
  if (object.hasBbox) {
    minPoint.set(object.bboxMin[0], object.bboxMin[1], object.bboxMin[2]);
    maxPoint.set(object.bboxMax[0], object.bboxMax[1], object.bboxMax[2]);
  } else {
    minPoint.set(object.transform[12], object.transform[13], zBase);
    maxPoint.set(object.transform[12] + 1.0F, object.transform[13] + 1.0F, zBase + thickness);
  }

  return {osg::Vec3(minPoint.x(), minPoint.y(), minPoint.z()),
          osg::Vec3(maxPoint.x(), minPoint.y(), minPoint.z()),
          osg::Vec3(maxPoint.x(), maxPoint.y(), minPoint.z()),
          osg::Vec3(minPoint.x(), maxPoint.y(), minPoint.z()),
          osg::Vec3(minPoint.x(), minPoint.y(), maxPoint.z()),
          osg::Vec3(maxPoint.x(), minPoint.y(), maxPoint.z()),
          osg::Vec3(maxPoint.x(), maxPoint.y(), maxPoint.z()),
          osg::Vec3(minPoint.x(), maxPoint.y(), maxPoint.z())};
}

}  // namespace

OsgBackend::OsgBackend() {
  profile_ = RenderProfile::Modern;
}

OsgBackend::~OsgBackend() {
  shutdown();
}

std::string OsgBackend::name() const {
  return "osg";
}

RenderProfile OsgBackend::getProfile() const {
  return profile_;
}

QWidget* OsgBackend::createViewWidget(QWidget* parent) {
  if (viewWidget_) {
    return viewWidget_;
  }

  auto* viewport = new OsgViewportWidget(this, parent);
  viewWidget_ = viewport;
  glViewWidget_ = viewport->glView();
  overlayWidget_ = viewport->overlay();
  viewWidget_->setObjectName("osgNativeView");

  // Now initialize the scene since viewWidget_ is ready
  initialize();

  return viewWidget_;
}

void OsgBackend::initialize() {
  if (!viewWidget_) {
    return;
  }

  if (!viewer_) {
    viewer_ = new osgViewer::Viewer();
    viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer_->setLightingMode(osgViewer::View::HEADLIGHT);
    viewer_->setReleaseContextAtEndOfFrameHint(false);
  }

  createSceneGraph();

  if (!scene_) {
    scene_ = new OsgScene();
    objectRoot_ = scene_->getObjectRoot();
    highlightRoot_ = scene_->getHighlightRoot();
    rulerRoot_ = new osg::Group();
    rulerRoot_->setName("RulerObjects");
    root_->addChild(objectRoot_);
    root_->addChild(highlightRoot_);
    root_->addChild(rulerRoot_);
  }

  if (!camera_) {
    camera_ = createCamera();
    viewer_->setCamera(camera_);
  }

  if (!graphicsWindow_) {
    graphicsWindow_ = new osgViewer::GraphicsWindowEmbedded(
      0, 0, std::max(1, glViewWidget_ ? glViewWidget_->width() : viewWidget_->width()),
      std::max(1, glViewWidget_ ? glViewWidget_->height() : viewWidget_->height()));
  }

  camera_->setGraphicsContext(graphicsWindow_);
  camera_->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  camera_->setClearColor(osg::Vec4(0.07F, 0.10F, 0.14F, 1.0F));
  camera_->setDrawBuffer(GL_COLOR_ATTACHMENT0);
  camera_->setReadBuffer(GL_COLOR_ATTACHMENT0);
  updateViewport(std::max(1, glViewWidget_ ? glViewWidget_->width() : viewWidget_->width()),
                 std::max(1, glViewWidget_ ? glViewWidget_->height() : viewWidget_->height()));

  setupLighting();
  if (root_) {
    root_->getOrCreateStateSet()->setMode(GL_MULTISAMPLE, osg::StateAttribute::ON);
  }
  applyViewTransformation();
  viewer_->setSceneData(root_);
  requestRedraw();
}

void OsgBackend::shutdown() {
  QOpenGLWidget* glWidget = glViewWidget_.data();
  if (glWidget) {
    glWidget->makeCurrent();
  }

  if (frameTimer_) {
    frameTimer_->stop();
    frameTimer_ = nullptr;
  }

  selectedNode_ = nullptr;
  scene_ = nullptr;
  highlightRoot_ = nullptr;
  rulerRoot_ = nullptr;
  objectRoot_ = nullptr;
  if (viewer_) {
    viewer_->setSceneData(nullptr);
  }
  if (camera_) {
    camera_->setGraphicsContext(nullptr);
  }
  camera_ = nullptr;
  graphicsWindow_ = nullptr;
  viewer_ = nullptr;
  root_ = nullptr;

  if (glWidget) {
    glWidget->doneCurrent();
  }
  glViewWidget_ = nullptr;
  overlayWidget_ = nullptr;
  viewWidget_ = nullptr;
}

void OsgBackend::loadScene(const domain::SceneSnapshot& snapshot) {
  snapshot_ = snapshot;
  if (!scene_) {
    return;
  }

  // Build index maps for O(1) lookup
  objectIndex_.clear();
  objectIndex_.reserve(snapshot_.objects.size());
  for (const auto& obj : snapshot_.objects) {
    objectIndex_[obj.objectId] = &obj;
  }

  layerIndex_.clear();
  layerIndex_.reserve(snapshot_.layers.size());
  for (const auto& layer : snapshot_.layers) {
    layerIndex_[layer.layerId] = &layer;
  }

  scene_->loadSnapshot(snapshot_);
  fprintf(stderr, "DEBUG loadScene: loaded %zu objects, %zu layers\n",
          scene_->getObjectCount(), snapshot_.layers.size());
  clearSelection();
  fitToScene();
  requestRedraw();
}

void OsgBackend::updateLayerState(const domain::LayerRecord& layer) {
  for (auto& snapshotLayer : snapshot_.layers) {
    if (snapshotLayer.layerId == layer.layerId) {
      snapshotLayer.visible = layer.visible;
      snapshotLayer.selectable = layer.selectable;
      if (layer.lineWidth > 0.0F) {
        snapshotLayer.lineWidth = layer.lineWidth;
      }
      snapshotLayer.lineStyle = layer.lineStyle;
      break;
    }
  }

  if (scene_) {
    scene_->updateLayer(layer);
  }
  requestRedraw();
}

void OsgBackend::updateObjects(const std::vector<domain::ObjectRecord>& objects) {
  if (objects.empty()) {
    return;
  }

  auto equalExceptVisible = [](const domain::ObjectRecord& a,
                               const domain::ObjectRecord& b) {
    return a.objectId == b.objectId && a.type == b.type && a.layerId == b.layerId
           && a.groupId == b.groupId && a.transform == b.transform
           && a.bboxMin == b.bboxMin && a.bboxMax == b.bboxMax
           && a.hasBbox == b.hasBbox && a.geometryRef == b.geometryRef
           && a.styleRef == b.styleRef && a.displayName == b.displayName;
  };

  std::unordered_map<std::string, std::size_t> objectIndexById;
  objectIndexById.reserve(snapshot_.objects.size());
  for (std::size_t index = 0; index < snapshot_.objects.size(); ++index) {
    objectIndexById[snapshot_.objects[index].objectId] = index;
  }

  std::vector<domain::ObjectRecord> fullUpdates;
  fullUpdates.reserve(objects.size());
  std::vector<std::pair<std::string, bool>> visibilityOnlyUpdates;
  visibilityOnlyUpdates.reserve(objects.size());

  for (const auto& object : objects) {
    auto found = objectIndexById.find(object.objectId);
    if (found == objectIndexById.end()) {
      snapshot_.objects.push_back(object);
      objectIndexById[object.objectId] = snapshot_.objects.size() - 1;
      // Also update the index map for O(1) lookup
      objectIndex_[object.objectId] = &snapshot_.objects.back();
      fullUpdates.push_back(object);
    } else {
      auto& existing = snapshot_.objects[found->second];
      if (existing.visible != object.visible && equalExceptVisible(existing, object)) {
        existing.visible = object.visible;
        visibilityOnlyUpdates.emplace_back(object.objectId, object.visible);
      } else {
        existing = object;
        fullUpdates.push_back(object);
        // Update index map pointer
        objectIndex_[object.objectId] = &existing;
      }
    }
  }

  if (scene_) {
    if (!fullUpdates.empty()) {
      scene_->upsertObjects(fullUpdates);
    }
    if (!visibilityOnlyUpdates.empty()) {
      scene_->updateObjectVisibility(visibilityOnlyUpdates);
    }
  }
  requestRedraw();
}

void OsgBackend::removeObjects(const std::vector<std::string>& objectIds) {
  if (objectIds.empty()) {
    return;
  }

  std::unordered_set<std::string> idSet;
  idSet.reserve(objectIds.size());
  for (const auto& id : objectIds) {
    idSet.insert(id);
  }

  snapshot_.objects.erase(
      std::remove_if(snapshot_.objects.begin(),
                     snapshot_.objects.end(),
                     [&](const domain::ObjectRecord& object) {
                       return idSet.find(object.objectId) != idSet.end();
                     }),
      snapshot_.objects.end());

  if (scene_) {
    scene_->removeObjectsById(objectIds);
  }

  if (!selectedObjectId_.empty() && idSet.find(selectedObjectId_) != idSet.end()) {
    clearSelection();
    return;
  }

  requestRedraw();
}

void OsgBackend::removeLayers(const std::vector<std::string>& layerIds) {
  if (layerIds.empty()) {
    return;
  }

  std::unordered_set<std::string> idSet;
  idSet.reserve(layerIds.size());
  for (const auto& id : layerIds) {
    idSet.insert(id);
  }

  snapshot_.layers.erase(
      std::remove_if(snapshot_.layers.begin(),
                     snapshot_.layers.end(),
                     [&](const domain::LayerRecord& layer) {
                       return idSet.find(layer.layerId) != idSet.end();
                     }),
      snapshot_.layers.end());

  if (scene_) {
    scene_->removeLayersById(layerIds);
  }

  requestRedraw();
}

void OsgBackend::clearScene() {
  snapshot_ = domain::SceneSnapshot{};
  if (scene_) {
    scene_->clear();
  }
  clearSelection();
  requestRedraw();
}

void OsgBackend::setToolMode(ToolMode mode) {
  toolMode_ = mode;
  if (toolMode_ != ToolMode::Ruler) {
    clearRulerGeometry();
  }
  requestRedraw();
}

ToolMode OsgBackend::getToolMode() const {
  return toolMode_;
}

std::string OsgBackend::getSelectedObjectId() const {
  return selectedObjectId_;
}

void OsgBackend::clearSelection() {
  selectedObjectId_.clear();
  selectedNode_ = nullptr;
  if (scene_) {
    scene_->setSelectedObject(std::string());
  }
  requestRedraw();
}

void OsgBackend::setAnchorView(AnchorView view) {
  anchorView_ = view;
  pushViewHistory();

  switch (view) {
    case AnchorView::Front:
      yaw_ = 0.0F;
      pitch_ = 0.0F;
      break;
    case AnchorView::Back:
      yaw_ = static_cast<float>(M_PI);
      pitch_ = 0.0F;
      break;
    case AnchorView::Left:
      yaw_ = -static_cast<float>(M_PI_2);
      pitch_ = 0.0F;
      break;
    case AnchorView::Right:
      yaw_ = static_cast<float>(M_PI_2);
      pitch_ = 0.0F;
      break;
    case AnchorView::Top:
      yaw_ = 0.0F;
      pitch_ = -static_cast<float>(M_PI_2);
      break;
    case AnchorView::Bottom:
      yaw_ = 0.0F;
      pitch_ = static_cast<float>(M_PI_2);
      break;
    case AnchorView::Free:
      break;
  }

  applyViewTransformation();
  requestRedraw();
}

void OsgBackend::zoomToSelected() {
  if (!selectedNode_ && !selectedObjectId_.empty() && scene_) {
    selectedNode_ = scene_->getObjectNode(selectedObjectId_);
  }
  if (!selectedNode_) {
    return;
  }

  pushViewHistory();
  const osg::BoundingSphere bounds = selectedNode_->getBound();
  center_ = bounds.center();
  distance_ = std::max(5.0F, bounds.radius() * 2.5F);
  applyViewTransformation();
  requestRedraw();
}

void OsgBackend::frame() {
  requestRedraw();
}

void OsgBackend::setVSync(bool /*enabled*/) {
}

void OsgBackend::setTargetFrameRate(int /*fps*/) {
}

IScene* OsgBackend::getScene() {
  return scene_.get();
}

IGeometryFactory* OsgBackend::getGeometryFactory() {
  return scene_.get();
}

IMaterialFactory* OsgBackend::getMaterialFactory() {
  return scene_.get();
}

ILightSystem* OsgBackend::getLightSystem() {
  return scene_.get();
}

ITextureFactory* OsgBackend::getTextureFactory() {
  return scene_.get();
}

void OsgBackend::setShadowEnabled(bool enabled) {
  shadowEnabled_ = enabled;
}

bool OsgBackend::isShadowEnabled() const {
  return shadowEnabled_;
}

bool OsgBackend::saveScreenshot(const std::string& filePath) {
  if (!viewWidget_) {
    return false;
  }
  const QImage image = viewWidget_->grab().toImage();
  return !image.isNull() && image.save(QString::fromStdString(filePath));
}

void OsgBackend::setDisplayNamesVisible(bool visible) {
  if (scene_) {
    scene_->setDisplayNamesVisible(visible);
  }
}

bool OsgBackend::isDisplayNamesVisible() const {
  if (scene_) {
    return scene_->isDisplayNamesVisible();
  }
  return false;
}

void OsgBackend::setStippleVisible(bool visible) {
  if (scene_) {
    scene_->setStippleVisible(visible);
  }
  requestRedraw();
}

bool OsgBackend::isStippleVisible() const {
  if (scene_) {
    return scene_->isStippleVisible();
  }
  return false;
}

void OsgBackend::setObjectVisible(const std::string& objectId, bool visible) {
  if (scene_) {
    scene_->setObjectVisible(objectId, visible);
  }
  requestRedraw();
}

bool OsgBackend::isObjectVisible(const std::string& objectId) const {
  if (scene_) {
    return scene_->isObjectVisible(objectId);
  }
  return true;
}

std::string OsgBackend::objectIdAtScreen(float nx, float ny) const {
  if (scene_) {
    return scene_->findObjectIdAtScreen(nx, ny);
  }
  return "";
}

void OsgBackend::setHeightScale(float scale) {
  heightScale_ = std::max(0.1f, scale);
  // Rebuild scene with new scale factor
  if (!snapshot_.objects.empty()) {
    loadScene(snapshot_);
  }
}

float OsgBackend::heightScale() const {
  return heightScale_;
}

void OsgBackend::createSceneGraph() {
  if (!root_) {
    root_ = new osg::Group();
    root_->setName("Root");
  }
}

osg::Camera* OsgBackend::createCamera() {
  osg::Camera* camera = new osg::Camera();
  const double aspect = glViewWidget_ && glViewWidget_->height() > 0
                            ? static_cast<double>(glViewWidget_->width()) /
                                  static_cast<double>(glViewWidget_->height())
                            : 1.0;
  camera->setProjectionMatrixAsPerspective(45.0, aspect, 0.1, 100000.0);
  camera->setViewport(0, 0,
                      std::max(1, glViewWidget_ ? glViewWidget_->width() : 1),
                      std::max(1, glViewWidget_ ? glViewWidget_->height() : 1));
  return camera;
}

void OsgBackend::setupLighting() {
  if (!root_) {
    return;
  }

  for (unsigned int index = 0; index < root_->getNumChildren(); ++index) {
    osg::Node* child = root_->getChild(index);
    if (child && child->getName() == "MainLight") {
      return;
    }
  }

  osg::ref_ptr<osg::Light> light = new osg::Light(0);
  light->setAmbient(osg::Vec4(0.16F, 0.16F, 0.18F, 1.0F));
  light->setDiffuse(osg::Vec4(0.98F, 0.94F, 0.90F, 1.0F));
  light->setSpecular(osg::Vec4(0.45F, 0.45F, 0.45F, 1.0F));
  light->setPosition(osg::Vec4(180.0F, 130.0F, 220.0F, 0.0F));

  osg::ref_ptr<osg::LightSource> lightSource = new osg::LightSource();
  lightSource->setLight(light);
  lightSource->setName("MainLight");
  root_->addChild(lightSource);
}

void OsgBackend::applyViewTransformation() {
  if (!camera_) {
    return;
  }

  const float cosYaw = std::cos(yaw_);
  const float sinYaw = std::sin(yaw_);
  const float cosPitch = std::cos(pitch_);
  const float sinPitch = std::sin(pitch_);

  eye_.x() = center_.x() + distance_ * cosYaw * cosPitch;
  eye_.y() = center_.y() + distance_ * sinYaw * cosPitch;
  eye_.z() = center_.z() + distance_ * sinPitch;

  camera_->setViewMatrixAsLookAt(eye_, center_, osg::Vec3(0.0F, 0.0F, 1.0F));
}

void OsgBackend::pushViewHistory() {
  viewHistoryBack_.push_back(captureViewState());
  if (viewHistoryBack_.size() > 32) {
    viewHistoryBack_.erase(viewHistoryBack_.begin());
  }
  viewHistoryForward_.clear();
}

OsgBackend::ViewState OsgBackend::captureViewState() const {
  return ViewState{center_, yaw_, pitch_, distance_, selectedObjectId_};
}

void OsgBackend::applyViewState(const ViewState& state) {
  center_ = state.center;
  yaw_ = state.yaw;
  pitch_ = state.pitch;
  distance_ = state.distance;
  applyViewTransformation();
  updateSelection(state.selectedId);
}

void OsgBackend::fitToScene() {
  if (!scene_ || scene_->getObjectCount() == 0) {
    center_.set(0.0F, 0.0F, 0.0F);
    distance_ = 100.0F;
    applyViewTransformation();
    return;
  }

  Vec3 minPoint;
  Vec3 maxPoint;
  scene_->getBoundingBox(minPoint, maxPoint);
  
  // Use scene bounding box center (same as ~/3dviewer)
  center_.set((minPoint.x + maxPoint.x) * 0.5F,
              (minPoint.y + maxPoint.y) * 0.5F,
              (minPoint.z + maxPoint.z) * 0.5F);
  
  fprintf(stderr, "DEBUG fitToScene: minPoint=(%f,%f,%f) maxPoint=(%f,%f,%f) center=(%f,%f,%f)\n",
          minPoint.x, minPoint.y, minPoint.z,
          maxPoint.x, maxPoint.y, maxPoint.z,
          center_.x(), center_.y(), center_.z());
  // Clamp Z range to a reasonable value (chip layers typically < 50 units).
  // Some objects (e.g., DRC markers with null-layer fallback) can produce
  // extreme Z values that blow up the bbox.
  const float zExtent = std::min(maxPoint.z - minPoint.z, 50.0f);
  const float extent = std::max({maxPoint.x - minPoint.x, maxPoint.y - minPoint.y, zExtent, 1.0F});
  distance_ = std::max(8.0F, extent * 1.7F);
  applyViewTransformation();
}

void OsgBackend::requestRedraw() {
  if (glViewWidget_) {
    glViewWidget_->update();
  }
  if (overlayWidget_) {
    overlayWidget_->update();
  }
  if (viewWidget_) {
    viewWidget_->update();
  }
}

void OsgBackend::pushCurrentViewToForwardHistory() {
  viewHistoryForward_.push_back(captureViewState());
  if (viewHistoryForward_.size() > 32) {
    viewHistoryForward_.erase(viewHistoryForward_.begin());
  }
}

void OsgBackend::goPreviousView() {
  if (viewHistoryBack_.empty()) {
    return;
  }
  pushCurrentViewToForwardHistory();
  const ViewState state = viewHistoryBack_.back();
  viewHistoryBack_.pop_back();
  applyViewState(state);
  requestRedraw();
}

void OsgBackend::goNextView() {
  if (viewHistoryForward_.empty()) {
    return;
  }
  viewHistoryBack_.push_back(captureViewState());
  const ViewState state = viewHistoryForward_.back();
  viewHistoryForward_.pop_back();
  applyViewState(state);
  requestRedraw();
}

void OsgBackend::handleMousePress(QMouseEvent* event) {
  lastMousePos_ = event->pos();
  pressMousePos_ = event->pos();
  dragAccumulated_ = 0;
  isDragging_ = false;
  isEditing_ = false;
  isEditingRotating_ = false;

  if (toolMode_ == ToolMode::Edit && event->button() == Qt::LeftButton) {
    if (!selectedNode_ && !selectedObjectId_.empty() && scene_) {
      selectedNode_ = scene_->getObjectNode(selectedObjectId_);
    }
    if (selectedNode_) {
      isEditing_ = true;
    }
  }

  if (toolMode_ == ToolMode::Edit && event->button() == Qt::RightButton) {
    if (!selectedNode_ && !selectedObjectId_.empty() && scene_) {
      selectedNode_ = scene_->getObjectNode(selectedObjectId_);
    }
    if (selectedNode_) {
      isEditingRotating_ = true;
    }
  }

  isRotating_ = toolMode_ == ToolMode::Navigate && event->button() == Qt::LeftButton;
}

void OsgBackend::handleMouseMove(QMouseEvent* event) {
  if (isEditing_) {
    if (!selectedNode_ && !selectedObjectId_.empty() && scene_) {
      selectedNode_ = scene_->getObjectNode(selectedObjectId_);
    }

    osg::Transform* osgTransform = selectedNode_ ? selectedNode_->asTransform() : nullptr;
    osg::MatrixTransform* transform = osgTransform ? osgTransform->asMatrixTransform() : nullptr;
    if (!transform) {
      return;
    }

    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();
    if (delta.manhattanLength() == 0) {
      return;
    }

    dragAccumulated_ += delta.manhattanLength();
    const bool beyondDragThreshold = (event->pos() - pressMousePos_).manhattanLength() >= 6 ||
                                     dragAccumulated_ >= 8;
    if (!beyondDragThreshold) {
      return;
    }

    isDragging_ = true;
    const float worldScale = std::max(0.01F, distance_ * 0.0015F);
    const float moveX = static_cast<float>(delta.x()) * worldScale;
    const float moveY = -static_cast<float>(delta.y()) * worldScale;

    osg::Matrixd matrix = transform->getMatrix();
    osg::Vec3d translation = matrix.getTrans();
    translation.x() += moveX;
    translation.y() += moveY;
    matrix.setTrans(translation);
    transform->setMatrix(matrix);

    if (scene_ && !selectedObjectId_.empty()) {
      scene_->setSelectedObject(selectedObjectId_);
    }
    requestRedraw();
    return;
  }

  if (isEditingRotating_) {
    if (!selectedNode_ && !selectedObjectId_.empty() && scene_) {
      selectedNode_ = scene_->getObjectNode(selectedObjectId_);
    }

    osg::Transform* osgTransform = selectedNode_ ? selectedNode_->asTransform() : nullptr;
    osg::MatrixTransform* transform = osgTransform ? osgTransform->asMatrixTransform() : nullptr;
    if (!transform) {
      return;
    }

    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();
    if (delta.manhattanLength() == 0) {
      return;
    }

    dragAccumulated_ += delta.manhattanLength();
    const bool beyondDragThreshold = (event->pos() - pressMousePos_).manhattanLength() >= 6 ||
                                     dragAccumulated_ >= 8;
    if (!beyondDragThreshold) {
      return;
    }

    isDragging_ = true;

    osg::Matrixd matrix = transform->getMatrix();
    osg::Vec3d center = matrix.getTrans();
    osg::Quat existingRot = matrix.getRotate();

    osg::Quat deltaRot;
    deltaRot.makeRotate(osg::DegreesToRadians(static_cast<double>(delta.x()) * 0.5), osg::Vec3d(0, 0, 1));

    osg::Matrixd newMatrix;
    newMatrix.makeRotate(existingRot * deltaRot);
    newMatrix.setTrans(center);
    transform->setMatrix(newMatrix);

    if (scene_ && !selectedObjectId_.empty()) {
      scene_->setSelectedObject(selectedObjectId_);
    }
    requestRedraw();
    return;
  }

  if (!isRotating_) {
    return;
  }

  const QPoint delta = event->pos() - lastMousePos_;
  lastMousePos_ = event->pos();
  if (delta.manhattanLength() == 0) {
    return;
  }

  dragAccumulated_ += delta.manhattanLength();
  const bool beyondDragThreshold = (event->pos() - pressMousePos_).manhattanLength() >= 6 ||
                                   dragAccumulated_ >= 8;

  if (beyondDragThreshold) {
    isDragging_ = true;
  }
  yaw_ += static_cast<float>(delta.x()) * 0.01F;
  pitch_ = std::clamp(pitch_ + static_cast<float>(delta.y()) * 0.01F, -1.2F, 1.2F);
  applyViewTransformation();
  requestRedraw();
}

void OsgBackend::handleMouseRelease(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && !isDragging_) {
    if (toolMode_ == ToolMode::Ruler) {
      setRulerPoint(event->pos());
    } else {
      selectAt(event->pos());
    }
  }
  isRotating_ = false;
  isEditing_ = false;
  isEditingRotating_ = false;
  isDragging_ = false;
}

void OsgBackend::handleWheel(QWheelEvent* event) {
  const int delta = event->angleDelta().y();
  if (delta == 0) {
    return;
  }

  distance_ *= delta > 0 ? 0.9F : 1.1F;
  distance_ = std::clamp(distance_, 2.0F, 100000.0F);
  applyViewTransformation();
  requestRedraw();
}

void OsgBackend::handleContextMenu(QContextMenuEvent* event) {
  // Find object under cursor
  QPoint pos = event->pos();
  QSize size = viewWidget_ ? viewWidget_->size() : QSize(640, 420);
  std::string objId;
  osg::Vec3 worldPt;
  if (!pickObjectAt(pos, size, &objId, &worldPt)) {
    return;  // No object clicked
  }

  // Look up object info
  std::string displayName = objId;
  std::string layerName;
  for (const auto& obj : snapshot_.objects) {
    if (obj.objectId == objId) {
      displayName = obj.displayName.empty() ? objId : obj.displayName;
      layerName = obj.layerId;
      break;
    }
  }

  QMenu menu;
  menu.addAction(QString("Object: %1").arg(QString::fromStdString(displayName)));
  menu.addAction(QString("Layer: %1").arg(QString::fromStdString(layerName)));
  menu.addSeparator();

  bool isHidden = !isObjectVisible(objId);
  QAction* hideAction = menu.addAction(isHidden ? "Show" : "Hide");
  if (hideAction) {
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == hideAction) {
      setObjectVisible(objId, isHidden);
    }
  }
}

void OsgBackend::handleKeyPress(QKeyEvent* event) {
  const float panStep = distance_ * 0.05f;
  const float heightStep = 0.1f;

  switch (event->key()) {
    case Qt::Key_1:
      setAnchorView(AnchorView::Front);
      break;
    case Qt::Key_2:
      setAnchorView(AnchorView::Back);
      break;
    case Qt::Key_3:
      setAnchorView(AnchorView::Left);
      break;
    case Qt::Key_4:
      setAnchorView(AnchorView::Right);
      break;
    case Qt::Key_5:
      setAnchorView(AnchorView::Top);
      break;
    case Qt::Key_6:
      setAnchorView(AnchorView::Bottom);
      break;
    case Qt::Key_Z:
      zoomToSelected();
      break;
    case Qt::Key_Escape:
      clearSelection();
      break;
    case Qt::Key_Left:
      if (event->modifiers() & Qt::AltModifier) {
        goPreviousView();
      } else {
        center_.x() -= panStep;
        applyViewTransformation();
        requestRedraw();
      }
      break;
    case Qt::Key_Right:
      if (event->modifiers() & Qt::AltModifier) {
        goNextView();
      } else {
        center_.x() += panStep;
        applyViewTransformation();
        requestRedraw();
      }
      break;
    case Qt::Key_Up:
      if (!(event->modifiers() & Qt::AltModifier)) {
        center_.y() += panStep;
        applyViewTransformation();
        requestRedraw();
      }
      break;
    case Qt::Key_Down:
      if (!(event->modifiers() & Qt::AltModifier)) {
        center_.y() -= panStep;
        applyViewTransformation();
        requestRedraw();
      }
      break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      setHeightScale(heightScale_ + heightStep);
      break;
    case Qt::Key_Minus:
      setHeightScale(heightScale_ - heightStep);
      break;
    case Qt::Key_0:
      if (event->modifiers() & Qt::AltModifier) {
        pushViewHistory();
      }
      break;
    default:
      break;
  }
}

void OsgBackend::drawOverlay(QPainter& painter, const QSize& size) const {
  int hiddenLayerCount = 0;
  for (const auto& layer : snapshot_.layers) {
    if (!layer.visible) {
      ++hiddenLayerCount;
    }
  }

  const int statusRed = std::clamp(26 + hiddenLayerCount * 72, 0, 255);
  const int statusGreen = std::clamp(48 + static_cast<int>((std::sin(yaw_) + 1.0F) * 38.0F), 0, 255);
  const int statusBlue = std::clamp(72 + static_cast<int>((std::sin(pitch_) + 1.0F) * 46.0F), 0, 255);

  std::ostringstream status;
  status.setf(std::ios::fixed);
  status.precision(2);
  status << "mode=";
  switch (toolMode_) {
    case ToolMode::Navigate:
      status << "nav";
      break;
    case ToolMode::Select:
      status << "select";
      break;
    case ToolMode::Ruler:
      status << "ruler";
      break;
    default:
      status << "other";
      break;
  }
  status << " hidden=" << hiddenLayerCount << " yaw=" << yaw_ << " pitch=" << pitch_;

  painter.setPen(QPen(QColor(235, 245, 250, 230), 1.0));
  painter.setBrush(QColor(statusRed, statusGreen, statusBlue, 190));
  painter.drawRoundedRect(QRectF(10.0, size.height() - 42.0, 360.0, 30.0), 7.0, 7.0);
  painter.drawText(QRectF(20.0, size.height() - 42.0, 340.0, 30.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(status.str()));

  if (!selectedObjectId_.empty()) {
    std::string selectionText = selectedObjectId_;
    if (const domain::ObjectRecord* selected = findObjectById(selectedObjectId_)) {
      std::string name = selected->displayName;
      if (name.empty() &&
          (selected->type == domain::ObjectType::Wire || selected->type == domain::ObjectType::Via)) {
        name = selected->groupId;
      }
      if (name.empty()) {
        name = selectedObjectId_;
      }
      selectionText = std::string(objectTypeText(selected->type)) + ":" + name;
    }

    painter.setPen(QPen(QColor(220, 245, 255, 230), 1.5));
    painter.setBrush(QColor(8, 24, 40, 165));
    painter.drawRoundedRect(QRectF(10.0, 10.0, 380.0, 28.0), 6.0, 6.0);
    painter.drawText(QRectF(18.0, 10.0, 364.0, 28.0), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromStdString("selected: " + selectionText));
  }

  if (rulerHasStart_) {
    const QPointF start = rulerStartScreen_.isNull() ? projectPoint(rulerStart_, size) : rulerStartScreen_;
    painter.setPen(QPen(QColor(120, 255, 180, 230), 2.0));
    painter.setBrush(QColor(120, 255, 180, 180));
    painter.drawEllipse(start, 4.0, 4.0);
    if (rulerHasEnd_) {
      const QPointF end = rulerEndScreen_.isNull() ? projectPoint(rulerEnd_, size) : rulerEndScreen_;
      painter.setPen(QPen(QColor(95, 255, 160, 245), 2.2));
      painter.drawLine(start, end);
      painter.setBrush(QColor(95, 255, 160, 190));
      painter.drawEllipse(end, 4.0, 4.0);
    }
  }
}

void OsgBackend::updateSelection(const std::string& objectId) {
  selectedObjectId_ = objectId;
  selectedNode_ = scene_ ? scene_->getObjectNode(objectId) : nullptr;
  if (scene_) {
    scene_->setSelectedObject(objectId);
  }
}

bool OsgBackend::pickObjectAt(const QPoint& pos,
                              const QSize& size,
                              std::string* objectId,
                              osg::Vec3* worldPoint) const {
  if (camera_) {
    const double winX = static_cast<double>(pos.x());
    const double winY = static_cast<double>(size.height() - pos.y());

    osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector =
        new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, winX, winY);
    osgUtil::IntersectionVisitor visitor(intersector.get());
    camera_->accept(visitor);

    if (intersector->containsIntersections()) {
      for (const auto& hit : intersector->getIntersections()) {
        for (auto nodeIt = hit.nodePath.rbegin(); nodeIt != hit.nodePath.rend(); ++nodeIt) {
          osg::Node* node = *nodeIt;
          if (!node) {
            continue;
          }

          std::string candidateId = node->getName();
          if (candidateId.empty()) {
            continue;
          }

          const std::string geodeSuffix = ":geode";
          if (candidateId.size() > geodeSuffix.size() &&
              candidateId.compare(candidateId.size() - geodeSuffix.size(), geodeSuffix.size(), geodeSuffix) == 0) {
            candidateId.erase(candidateId.size() - geodeSuffix.size());
          }

          const domain::ObjectRecord* objectRecord = findObjectById(candidateId);
          if (!objectRecord) {
            continue;
          }

          // Skip invisible objects
          if (!objectRecord->visible) {
            continue;
          }

          const domain::LayerRecord* layer = findLayerById(objectRecord->layerId);
          if (layer && (!layer->visible || !layer->selectable)) {
            continue;
          }

          if (objectId) {
            *objectId = candidateId;
          }
          if (worldPoint) {
            *worldPoint = hit.getWorldIntersectPoint();
          }
          return true;
        }
      }
    }
  }

  // Fallback: keep coarse heuristic picking for non-intersectable drawables.
  const QPointF cursor(static_cast<double>(pos.x()), static_cast<double>(pos.y()));
  float bestScore = std::numeric_limits<float>::max();
  float bestDistance = std::numeric_limits<float>::max();
  std::string bestId;
  osg::Vec3 bestCenter;

  for (const auto& object : snapshot_.objects) {
    // Skip invisible objects
    if (!object.visible) {
      continue;
    }

    const domain::LayerRecord* layer = findLayerById(object.layerId);
    if (layer && (!layer->visible || !layer->selectable)) {
      continue;
    }

    QRectF bounds;
    osg::Vec3 center;
    if (!objectScreenBounds(object, size, &bounds, &center)) {
      continue;
    }

    const QRectF expanded = bounds.adjusted(-14.0, -14.0, 14.0, 14.0);
    const QPointF screenCenter = bounds.center();
    const float dx = static_cast<float>(cursor.x() - screenCenter.x());
    const float dy = static_cast<float>(cursor.y() - screenCenter.y());
    const float score = std::sqrt(dx * dx + dy * dy);
    const bool insideHitRegion = expanded.contains(cursor) || score <= 60.0F;
    if (!insideHitRegion) {
      continue;
    }

    const float cameraDistance = (center - eye_).length();
    if (cameraDistance < bestDistance - 1e-3F ||
        (std::abs(cameraDistance - bestDistance) <= 1e-3F && score < bestScore)) {
      bestDistance = cameraDistance;
      bestScore = score;
      bestId = object.objectId;
      bestCenter = center;
    }
  }

  if (bestId.empty()) {
    return false;
  }

  if (objectId) {
    *objectId = bestId;
  }
  if (worldPoint) {
    *worldPoint = bestCenter;
  }
  return true;
}

QPointF OsgBackend::projectPoint(const osg::Vec3& worldPoint, const QSize& size) const {
  if (!camera_ || !camera_->getViewport()) {
    return QPointF(size.width() * 0.5, size.height() * 0.5);
  }

  const osg::Matrixd worldToScreen = camera_->getViewMatrix() *
                                     camera_->getProjectionMatrix() *
                                     camera_->getViewport()->computeWindowMatrix();
  const osg::Vec3 screenPoint = worldPoint * worldToScreen;
  return QPointF(screenPoint.x(), static_cast<double>(size.height()) - screenPoint.y());
}

bool OsgBackend::objectScreenBounds(const domain::ObjectRecord& object,
                                    const QSize& size,
                                    QRectF* bounds,
                                    osg::Vec3* center) const {
  if (!camera_ || !bounds || !center) {
    return false;
  }

  const domain::LayerRecord* layer = findLayerRecord(snapshot_, object.layerId);
  const auto corners = objectCorners(object, layer);
  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = -std::numeric_limits<float>::max();
  float maxY = -std::numeric_limits<float>::max();
  osg::Vec3 sum(0.0F, 0.0F, 0.0F);

  for (const auto& corner : corners) {
    const QPointF projected = projectPoint(corner, size);
    minX = std::min(minX, static_cast<float>(projected.x()));
    minY = std::min(minY, static_cast<float>(projected.y()));
    maxX = std::max(maxX, static_cast<float>(projected.x()));
    maxY = std::max(maxY, static_cast<float>(projected.y()));
    sum += corner;
  }

  *bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
  *center = sum / 8.0F;
  return bounds->isValid();
}

void OsgBackend::selectAt(const QPoint& position) {
  std::string objectId;
  osg::Vec3 worldPoint;
  if (!pickObjectAt(position, viewWidget_ ? viewWidget_->size() : QSize(1, 1), &objectId, &worldPoint)) {
    clearSelection();
    return;
  }

  (void)worldPoint;
  updateSelection(objectId);
  requestRedraw();
}

void OsgBackend::setRulerPoint(const QPoint& position) {
  const QPointF screenPoint(static_cast<double>(position.x()), static_cast<double>(position.y()));
  std::string objectId;
  osg::Vec3 worldPoint;
  const bool picked = pickObjectAt(position,
                                   viewWidget_ ? viewWidget_->size() : QSize(1, 1),
                                   &objectId,
                                   &worldPoint);
  if (!picked) {
    worldPoint = center_;
  }

  if (!rulerHasStart_ || (rulerHasStart_ && rulerHasEnd_)) {
    rulerStart_ = worldPoint;
    rulerStartScreen_ = screenPoint;
    rulerStartId_ = objectId;
    rulerHasStart_ = true;
    rulerHasEnd_ = false;
    rulerEndScreen_ = QPointF();
    rulerEndId_.clear();
  } else {
    rulerEnd_ = worldPoint;
    rulerEndScreen_ = screenPoint;
    rulerEndId_ = objectId;
    rulerHasEnd_ = true;
  }
  rebuildRulerGeometry();
  requestRedraw();
}

void OsgBackend::rebuildRulerGeometry() {
  if (!rulerRoot_) {
    return;
  }

  rulerRoot_->removeChildren(0, rulerRoot_->getNumChildren());
  if (!rulerHasStart_) {
    return;
  }

  osg::ref_ptr<osg::Geode> geode = new osg::Geode();
  osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry();
  osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
  osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array();

  const float markerSize = std::max(distance_ * 0.015F, 0.45F);
  auto appendMarker = [&](const osg::Vec3& center, const osg::Vec4& color) {
    const unsigned int base = vertices->size();
    vertices->push_back(center + osg::Vec3(-markerSize, 0.0F, 0.0F));
    vertices->push_back(center + osg::Vec3(markerSize, 0.0F, 0.0F));
    vertices->push_back(center + osg::Vec3(0.0F, -markerSize, 0.0F));
    vertices->push_back(center + osg::Vec3(0.0F, markerSize, 0.0F));
    vertices->push_back(center + osg::Vec3(0.0F, 0.0F, -markerSize));
    vertices->push_back(center + osg::Vec3(0.0F, 0.0F, markerSize));
    colors->push_back(color);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, base, 6));
  };

  appendMarker(rulerStart_, osg::Vec4(0.18F, 1.0F, 0.58F, 1.0F));
  if (rulerHasEnd_) {
    const unsigned int lineBase = vertices->size();
    vertices->push_back(rulerStart_);
    vertices->push_back(rulerEnd_);
    colors->push_back(osg::Vec4(0.05F, 0.95F, 0.50F, 1.0F));
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, lineBase, 2));
    appendMarker(rulerEnd_, osg::Vec4(0.10F, 0.85F, 1.0F, 1.0F));
  }

  geometry->setVertexArray(vertices);
  geometry->setColorArray(colors, osg::Array::BIND_OVERALL);
  osg::StateSet* stateSet = geode->getOrCreateStateSet();
  stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
  stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
  osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth(rulerHasEnd_ ? 4.0F : 5.0F);
  stateSet->setAttributeAndModes(lineWidth, osg::StateAttribute::ON);
  geode->addDrawable(geometry);
  rulerRoot_->addChild(geode);
}

void OsgBackend::clearRulerGeometry() {
  rulerHasStart_ = false;
  rulerHasEnd_ = false;
  rulerStartScreen_ = QPointF();
  rulerEndScreen_ = QPointF();
  rulerStartId_.clear();
  rulerEndId_.clear();
  if (rulerRoot_) {
    rulerRoot_->removeChildren(0, rulerRoot_->getNumChildren());
  }
}

void OsgBackend::syncGraphicsContext(int width, int height, unsigned int defaultFboId) {
  if (!graphicsWindow_ || !camera_) {
    return;
  }

  graphicsWindow_->setDefaultFboId(defaultFboId);
  const int safeWidth = std::max(1, width);
  const int safeHeight = std::max(1, height);
  graphicsWindow_->resized(0, 0, safeWidth, safeHeight);
  camera_->setViewport(0, 0, safeWidth, safeHeight);
}

void OsgBackend::updateViewport(int width, int height) {
  if (!camera_ || !graphicsWindow_) {
    return;
  }

  const int safeWidth = std::max(1, width);
  const int safeHeight = std::max(1, height);
  graphicsWindow_->resized(0, 0, safeWidth, safeHeight);
  camera_->setViewport(0, 0, safeWidth, safeHeight);
  camera_->setProjectionMatrixAsPerspective(45.0,
                                            static_cast<double>(safeWidth) / static_cast<double>(safeHeight),
                                            0.1,
                                            100000.0);
}

void OsgBackend::renderFrame() {
  if (!viewer_ || !camera_ || !graphicsWindow_) {
    return;
  }

  // Only update viewport if size actually changed
  int width = glViewWidget_ ? glViewWidget_->width() : 1;
  int height = glViewWidget_ ? glViewWidget_->height() : 1;
  if (width != lastViewWidth_ || height != lastViewHeight_) {
    lastViewWidth_ = width;
    lastViewHeight_ = height;
    updateViewport(width, height);
  }
  viewer_->frame();
}

const domain::ObjectRecord* OsgBackend::findObjectById(const std::string& objectId) const {
  auto it = objectIndex_.find(objectId);
  if (it != objectIndex_.end()) {
    return it->second;
  }
  return nullptr;
}

const domain::LayerRecord* OsgBackend::findLayerById(const std::string& layerId) const {
  auto it = layerIndex_.find(layerId);
  if (it != layerIndex_.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d

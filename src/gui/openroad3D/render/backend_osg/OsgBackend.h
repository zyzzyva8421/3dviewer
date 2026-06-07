#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>

#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

class QOpenGLWidget;

#include "render/api/IRenderBackend.h"

// English comment.
#include <osgViewer/Viewer>
#include <osgViewer/ViewerBase>
#include <osgViewer/GraphicsWindow>
#include <osg/Camera>
#include <osg/Geode>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Switch>
#include <osg/Geometry>
#include <osg/StateSet>
#include <osg/State>
#include <osg/Material>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/Texture2D>
#include <osg/TexGen>
#include <osgDB/DatabasePager>
#include <osgUtil/Optimizer>
#include <osgUtil/IntersectVisitor>
#include <osgUtil/PolytopeIntersector>

// English comment.
#include <osgShadow/ShadowedScene>
#include <osgShadow/ShadowMap>

namespace viewer3d {
namespace render {
namespace backend_osg {

class OsgScene;
class OsgViewWidget;
class OsgViewportWidget;

/**
 * @brief English description.
 * 
 * English documentation.
 */
class OsgBackend : public IRenderBackend {
 public:
  // English comment.
  struct ViewState {
    osg::Vec3 center;
    float yaw;
    float pitch;
    float distance;
    std::string selectedId;
  };

  OsgBackend();
  virtual ~OsgBackend();

  // English comment.
  
  std::string name() const override;
  RenderProfile getProfile() const override;
  
  QWidget* createViewWidget(QWidget* parent) override;
  
  void initialize() override;
  void shutdown() override;
  
  void loadScene(const domain::SceneSnapshot& snapshot) override;
  void updateLayerState(const domain::LayerRecord& layer) override;
  void clearScene() override;
  
  void setToolMode(ToolMode mode) override;
  ToolMode getToolMode() const override;
  
  std::string getSelectedObjectId() const override;
  void clearSelection() override;
  void setAnchorView(AnchorView view) override;
  void zoomToSelected() override;
  
  void frame() override;
  void setVSync(bool enabled) override;
  void setTargetFrameRate(int fps) override;
  
  IScene* getScene() override;
  IGeometryFactory* getGeometryFactory() override;
  IMaterialFactory* getMaterialFactory() override;
  ILightSystem* getLightSystem() override;
  ITextureFactory* getTextureFactory() override;
  
  void setShadowEnabled(bool enabled) override;
  bool isShadowEnabled() const override;
  
  bool saveScreenshot(const std::string& filePath) override;

 private:
  friend class OsgViewWidget;
  friend class OsgViewportWidget;

  // English comment.
  
  /**
   * @brief English description.
   */
  void createSceneGraph();
  
  /**
   * @brief English description.
   */
  osg::Camera* createCamera();
  
  /**
   * @brief English description.
   */
  void setupLighting();
  
  /**
   * @brief English description.
   */
  void applyViewTransformation();
  
  /**
   * @brief English description.
   */
  void pushViewHistory();
  
  /**
   * @brief English description.
   */
  ViewState captureViewState() const;
  
  /**
   * @brief English description.
   */
  void applyViewState(const ViewState& state);

  void fitToScene();
  void requestRedraw();
  void pushCurrentViewToForwardHistory();
  void goPreviousView();
  void goNextView();
  void selectAt(const QPoint& position);
  void setRulerPoint(const QPoint& position);
  void rebuildRulerGeometry();
  void clearRulerGeometry();
  void syncGraphicsContext(int width, int height, unsigned int defaultFboId);
  void updateViewport(int width, int height);
  void renderFrame();
  void handleMousePress(QMouseEvent* event);
  void handleMouseMove(QMouseEvent* event);
  void handleMouseRelease(QMouseEvent* event);
  void handleWheel(QWheelEvent* event);
  void handleKeyPress(QKeyEvent* event);
  void drawOverlay(class QPainter& painter, const QSize& size) const;
  void updateSelection(const std::string& objectId);
  bool pickObjectAt(const QPoint& pos, const QSize& size, std::string* objectId, osg::Vec3* worldPoint) const;
  QPointF projectPoint(const osg::Vec3& worldPoint, const QSize& size) const;
  bool objectScreenBounds(const domain::ObjectRecord& object, const QSize& size, QRectF* bounds, osg::Vec3* center) const;

  // English comment.
  
  QPointer<QWidget> viewWidget_;
  QPointer<QOpenGLWidget> glViewWidget_;
  QPointer<QWidget> overlayWidget_;
  QPointer<QTimer> frameTimer_;
  osg::ref_ptr<osgViewer::Viewer> viewer_;
  osg::ref_ptr<osgViewer::GraphicsWindow> graphicsWindow_;
  
  osg::ref_ptr<osg::Group> root_;
  osg::ref_ptr<osg::Camera> camera_;
  osg::ref_ptr<osg::Group> objectRoot_;
  osg::ref_ptr<osg::Switch> highlightRoot_;
  osg::ref_ptr<osg::Group> rulerRoot_;
  
  osg::ref_ptr<OsgScene> scene_;
  domain::SceneSnapshot snapshot_;
  
  ToolMode toolMode_ = ToolMode::Navigate;
  AnchorView anchorView_ = AnchorView::Top;
  
  // English comment.
  float yaw_ = 0.0F;
  float pitch_ = -static_cast<float>(M_PI_2);  // English comment.
  float distance_ = 100.0F;
  osg::Vec3 center_;
  osg::Vec3 eye_;
  
  bool shadowEnabled_ = false;
  
  // English comment.
  std::string selectedObjectId_;
  osg::ref_ptr<osg::Node> selectedNode_;
  
  // English comment.
  bool rulerHasStart_ = false;
  bool rulerHasEnd_ = false;
  osg::Vec3 rulerStart_;
  osg::Vec3 rulerEnd_;
  QPointF rulerStartScreen_;
  QPointF rulerEndScreen_;
  std::string rulerStartId_;
  std::string rulerEndId_;
  
  // English comment.
  std::vector<ViewState> viewHistoryBack_;
  std::vector<ViewState> viewHistoryForward_;
  
  // English comment.
  QPoint lastMousePos_;
  QPoint pressMousePos_;
  int dragAccumulated_ = 0;
  bool isDragging_ = false;
  bool isRotating_ = false;
  bool isEditing_ = false;
  bool isEditingRotating_ = false;
  
  // English comment.
  RenderProfile profile_ = RenderProfile::Modern;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d

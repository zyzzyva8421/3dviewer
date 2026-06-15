#pragma once

#include <string>
#include <array>
#include <memory>

#include "core/domain/SceneTypes.h"
#include "RenderCapabilities.h"
#include "IGeometry.h"
#include "IMaterial.h"
#include "ILight.h"
#include "ITexture.h"
#include "IScene.h"

class QWidget;

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 */
enum class ToolMode {
  Navigate,  // English comment.
  Select, // English comment.
  Ruler,     // English comment.
  Edit,      // English comment.
  Highlight  // English comment.
};

/**
 * @brief English description.
 */
enum class AnchorView {
  Free,   // English comment.
  Front,  // English comment.
  Back,   // English comment.
  Left,   // English comment.
  Right,  // English comment.
  Top,    // English comment.
  Bottom  // English comment.
};

/**
 * @brief English description.
 * 
 * English documentation.
 * English documentation.
 * 
 * English documentation.
 * English documentation.
 * English documentation.
 * English documentation.
 * English documentation.
 */
class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;

  // English comment.
  
  /**
   * @brief English description.
   */
  virtual std::string name() const = 0;
  
  /**
   * @brief English description.
   */
  virtual RenderProfile getProfile() const = 0;

  // English comment.
  
  /**
   * @brief English description.
   * @param parent Parameter description.
   * @return Return value description.
   */
  virtual QWidget* createViewWidget(QWidget* parent) = 0;

  // English comment.
  
  /**
   * @brief English description.
   * English documentation.
   */
  virtual void initialize() = 0;
  
  /**
   * @brief English description.
   * English documentation.
   */
  virtual void shutdown() = 0;

  // English comment.
  
  /**
   * @brief English description.
   * @param snapshot Parameter description.
   */
  virtual void loadScene(const domain::SceneSnapshot& snapshot) = 0;
  
  /**
   * @brief English description.
   * @param layer Parameter description.
   */
  virtual void updateLayerState(const domain::LayerRecord& layer) = 0;

  /**
   * @brief Apply incremental object updates without reloading the full scene.
   * @param objects Parameter description.
   */
  virtual void updateObjects(const std::vector<domain::ObjectRecord>& objects) = 0;

  /**
   * @brief Remove objects incrementally by object IDs.
   * @param objectIds Parameter description.
   */
  virtual void removeObjects(const std::vector<std::string>& objectIds) = 0;

  /**
   * @brief Remove layers incrementally by layer IDs.
   * @param layerIds Parameter description.
   */
  virtual void removeLayers(const std::vector<std::string>& layerIds) = 0;
  
  /**
   * @brief English description.
   */
  virtual void clearScene() = 0;

  // English comment.
  
  /**
   * @brief English description.
   */
  virtual void setToolMode(ToolMode mode) = 0;
  
  /**
   * @brief English description.
   */
  virtual ToolMode getToolMode() const = 0;
  
  /**
   * @brief English description.
   */
  virtual std::string getSelectedObjectId() const = 0;
  
  /**
   * @brief English description.
   */
  virtual void clearSelection() = 0;
  
  /**
   * @brief English description.
   */
  virtual void setAnchorView(AnchorView view) = 0;
  
  /**
   * @brief English description.
   */
  virtual void zoomToSelected() = 0;

  // English comment.
  
  /**
   * @brief English description.
   * English documentation.
   */
  virtual void frame() = 0;
  
  /**
   * @brief English description.
   */
  virtual void setVSync(bool enabled) = 0;
  
  /**
   * @brief English description.
   * @param fps Parameter description.
   */
  virtual void setTargetFrameRate(int fps) = 0;

  // English comment.
  
  /**
   * @brief English description.
   * @return Return value description.
   */
  virtual IScene* getScene() = 0;
  
  /**
   * @brief English description.
   * @return Return value description.
   */
  virtual IGeometryFactory* getGeometryFactory() = 0;
  
  /**
   * @brief English description.
   * @return Return value description.
   */
  virtual IMaterialFactory* getMaterialFactory() = 0;
  
  /**
   * @brief English description.
   * @return Return value description.
   */
  virtual ILightSystem* getLightSystem() = 0;
  
  /**
   * @brief English description.
   * @return Return value description.
   */
  virtual ITextureFactory* getTextureFactory() = 0;

  // English comment.
  
  /**
   * @brief English description.
   */
  virtual void setShadowEnabled(bool enabled) = 0;
  
  /**
   * @brief English description.
   */
  virtual bool isShadowEnabled() const = 0;

  // English comment.
  
  /**
   * @brief English description.
   * @param filePath Parameter description.
   * @return Return value description.
   */
  virtual bool saveScreenshot(const std::string& filePath) = 0;  
  /**
   * @brief Set whether display names (labels) are visible on3D objects.
   * @param visible true to show names, false to hide
   */
  virtual void setDisplayNamesVisible(bool visible) = 0;
  
  /**
   * @brief Query whether display names are visible.
   * @return true if names are visible
   */
  virtual bool isDisplayNamesVisible() const = 0;

  /**
   * @brief Show/hide stipple surface patterns on objects.
   * @param visible true to show stipple, false to hide
   */
  virtual void setStippleVisible(bool visible) = 0;

  /**
   * @brief Query whether stipple patterns are visible.
   * @return true if stipple is visible
   */
  virtual bool isStippleVisible() const = 0;

  virtual void setObjectVisible(const std::string& objectId, bool visible) = 0;
  virtual bool isObjectVisible(const std::string& objectId) const = 0;
  virtual std::string objectIdAtScreen(float nx, float ny) const = 0;
  virtual void setHeightScale(float scale) = 0;
  virtual float heightScale() const = 0;};

// English comment.

using IRenderBackendPtr = std::unique_ptr<IRenderBackend>;

}  // namespace render
}  // namespace viewer3d

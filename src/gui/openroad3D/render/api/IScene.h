#pragma once

#include <string>
#include <vector>
#include <memory>
#include "core/domain/SceneTypes.h"
#include "IGeometry.h"
#include "IMaterial.h"

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 * 
 * English documentation.
 */
class ISceneObject {
public:
    virtual ~ISceneObject() = default;
    
    /**
     * @brief English description.
     */
    virtual const std::string& getId() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setGeometry(IGeometry* geometry) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* getGeometry() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setMaterial(IMaterial* material) = 0;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* getMaterial() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setTransform(const std::array<float, 16>& transform) = 0;
    
    /**
     * @brief English description.
     */
    virtual const std::array<float, 16>& getTransform() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setVisible(bool visible) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isVisible() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setSelectable(bool selectable) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isSelectable() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setSelected(bool selected) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isSelected() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setHighlighted(bool highlighted) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isHighlighted() const = 0;
};

/**
 * @brief English description.
 * 
 * English documentation.
 */
class IScene {
public:
    virtual ~IScene() = default;
    
    /**
     * @brief English description.
     */
    virtual void addObject(ISceneObject* object) = 0;
    
    /**
     * @brief English description.
     */
    virtual void removeObject(ISceneObject* object) = 0;
    
    /**
     * @brief English description.
     */
    virtual void clear() = 0;
    
    /**
     * @brief English description.
     */
    virtual size_t getObjectCount() const = 0;
    
    /**
     * @brief English description.
     */
    virtual std::vector<ISceneObject*> getObjectsByLayer(const std::string& layerId) const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setLayerVisible(const std::string& layerId, bool visible) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isLayerVisible(const std::string& layerId) const = 0;
    
    /**
     * @brief English description.
     */
    virtual void updateLayer(const domain::LayerRecord& layer) = 0;
    
    /**
     * @brief English description.
     */
    virtual const domain::LayerRecord* getLayer(const std::string& layerId) const = 0;
    
    /**
     * @brief English description.
     * @param origin Parameter description.
     * @param direction Parameter description.
     * @param maxDistance Parameter description.
     * @return Return value description.
     */
    virtual ISceneObject* raycast(const Vec3& origin, const Vec3& direction, float maxDistance) = 0;
    
    /**
     * @brief English description.
     */
    virtual void getBoundingBox(Vec3& min, Vec3& max) const = 0;
};

}  // namespace render
}  // namespace viewer3d
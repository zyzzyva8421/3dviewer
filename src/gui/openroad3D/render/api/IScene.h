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
 * @brief Runtime scene object abstraction.
 *
 * A scene object combines geometry, material, transform, and interaction flags
 * (visible/selectable/selected/highlighted).
 */
class ISceneObject {
public:
    virtual ~ISceneObject() = default;
    
    /** @brief Stable object identifier used by selection and lookup. */
    virtual const std::string& getId() const = 0;
    
    /** @brief Assign geometry payload. Ownership is implementation-defined. */
    virtual void setGeometry(IGeometry* geometry) = 0;
    
    /** @brief Access current geometry payload. */
    virtual IGeometry* getGeometry() const = 0;
    
    /** @brief Assign material payload. Ownership is implementation-defined. */
    virtual void setMaterial(IMaterial* material) = 0;
    
    /** @brief Access current material payload. */
    virtual IMaterial* getMaterial() const = 0;
    
    /** @brief Set object transform as a 4x4 matrix. */
    virtual void setTransform(const std::array<float, 16>& transform) = 0;
    
    /** @brief Get object transform matrix. */
    virtual const std::array<float, 16>& getTransform() const = 0;
    
    /** @brief Set render visibility. */
    virtual void setVisible(bool visible) = 0;
    
    /** @brief Query render visibility. */
    virtual bool isVisible() const = 0;
    
    /** @brief Set whether this object can be selected. */
    virtual void setSelectable(bool selectable) = 0;
    
    /** @brief Query whether this object can be selected. */
    virtual bool isSelectable() const = 0;
    
    /** @brief Set selected state. */
    virtual void setSelected(bool selected) = 0;
    
    /** @brief Query selected state. */
    virtual bool isSelected() const = 0;
    
    /** @brief Set highlighted state for temporary emphasis. */
    virtual void setHighlighted(bool highlighted) = 0;
    
    /** @brief Query highlighted state. */
    virtual bool isHighlighted() const = 0;
};

/**
 * @brief Scene container abstraction used by renderer backends.
 *
 * The scene owns backend-specific scene objects and supports layer-level
 * visibility and hit-testing operations.
 */
class IScene {
public:
    virtual ~IScene() = default;
    
    /** @brief Add a scene object to the container. */
    virtual void addObject(ISceneObject* object) = 0;
    
    /** @brief Remove a scene object from the container. */
    virtual void removeObject(ISceneObject* object) = 0;
    
    /** @brief Remove all objects and reset scene-owned state. */
    virtual void clear() = 0;
    
    /** @brief Return current number of objects in the scene. */
    virtual size_t getObjectCount() const = 0;
    
    /** @brief Return all objects that belong to a given layer id. */
    virtual std::vector<ISceneObject*> getObjectsByLayer(const std::string& layerId) const = 0;
    
    /** @brief Set layer visibility and propagate to contained objects. */
    virtual void setLayerVisible(const std::string& layerId, bool visible) = 0;
    
    /** @brief Query visibility for a layer id. */
    virtual bool isLayerVisible(const std::string& layerId) const = 0;
    
    /** @brief Update layer style/metadata already tracked by the scene. */
    virtual void updateLayer(const domain::LayerRecord& layer) = 0;
    
    /** @brief Get layer record by layer id, or nullptr if not found. */
    virtual const domain::LayerRecord* getLayer(const std::string& layerId) const = 0;
    
    /**
     * @brief Raycast against visible geometry in world space.
     * @param origin Ray origin in world coordinates.
     * @param direction Ray direction in world coordinates.
     * @param maxDistance Maximum intersection distance.
     * @return Closest intersected object, or nullptr if no hit.
     */
    virtual ISceneObject* raycast(const Vec3& origin, const Vec3& direction, float maxDistance) = 0;
    
    /** @brief Compute world-space axis-aligned bounds for all visible objects. */
    virtual void getBoundingBox(Vec3& min, Vec3& max) const = 0;
};

}  // namespace render
}  // namespace viewer3d
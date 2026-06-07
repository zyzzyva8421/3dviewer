#pragma once

#include "IGeometry.h"

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 */
enum class LightType {
    Ambient,    // English comment.
    Directional, // English comment.
    Point,       // English comment.
    Spot // English comment.
};

/**
 * @brief English description.
 */
class ILight {
public:
    virtual ~ILight() = default;
    
    /**
     * @brief English description.
     */
    virtual void setType(LightType type) = 0;
    
    /**
     * @brief English description.
     */
    virtual LightType getType() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setPosition(float x, float y, float z) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setDirection(float x, float y, float z) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setColor(float r, float g, float b) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setIntensity(float intensity) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setAttenuation(float constant, float linear, float quadratic) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setSpotAngle(float inner, float outer) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setEnabled(bool enabled) = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isEnabled() const = 0;
};

/**
 * @brief English description.
 */
class ILightSystem {
public:
    virtual ~ILightSystem() = default;
    
    /**
     * @brief English description.
     */
    virtual ILight* createLight(LightType type) = 0;
    
    /**
     * @brief English description.
     */
    virtual void destroyLight(ILight* light) = 0;
    
    /**
     * @brief English description.
     */
    virtual size_t getLightCount() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setMainLight(ILight* light) = 0;
    
    /**
     * @brief English description.
     */
    virtual ILight* getMainLight() = 0;
    
    /**
     * @brief English description.
     */
    virtual void update() = 0;
};

}  // namespace render
}  // namespace viewer3d
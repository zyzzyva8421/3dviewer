#pragma once

#include <string>
#include "core/domain/SceneTypes.h"

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 * 
 * English documentation.
 */
class IMaterial {
public:
    virtual ~IMaterial() = default;
    
    /**
     * @brief English description.
     */
    virtual void setColor(float r, float g, float b, float a = 1.0F) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setColor(const domain::ColorRgba& color) = 0;
    
    /**
     * @brief English description.
     */
    virtual void getColor(float& r, float& g, float& b, float& a) const = 0;
    
    /**
     * @brief English description.
     * @param transparency Parameter description.
     */
    virtual void setTransparency(float transparency) = 0;
    
    /**
     * @brief English description.
     */
    virtual float getTransparency() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void setAmbient(float ambient) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setDiffuse(float diffuse) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setSpecular(float specular) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setShininess(float shininess) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setEmission(float r, float g, float b) = 0;
    
    /**
     * @brief English description.
     */
    virtual void apply() = 0;
};

/**
 * @brief English description.
 */
struct MaterialDesc {
    domain::ColorRgba color;
    float transparency = 0.0F;
    float ambient = 0.3F;
    float diffuse = 0.7F;
    float specular = 0.5F;
    float shininess = 32.0F;
    float emissionR = 0.0F;
    float emissionG = 0.0F;
    float emissionB = 0.0F;
    
    std::string diffuseTextureId;
    std::string normalTextureId;
};

/**
 * @brief English description.
 */
class IMaterialFactory {
public:
    virtual ~IMaterialFactory() = default;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* createMaterial() = 0;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* createMaterial(const MaterialDesc& desc) = 0;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* createHighlightMaterial(float r, float g, float b) = 0;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* createSelectionMaterial() = 0;
    
    /**
     * @brief English description.
     */
    virtual IMaterial* createGlowMaterial(float intensity = 1.0F) = 0;
};

}  // namespace render
}  // namespace viewer3d
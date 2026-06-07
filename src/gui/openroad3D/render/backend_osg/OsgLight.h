#pragma once

#include <osg/ref_ptr>
#include <osg/Light>

#include "render/api/ILight.h"

namespace osg {
class Light;
}

namespace viewer3d {
namespace render {
namespace backend_osg {

/**
 * @brief English description.
 */
class OsgLight : public ILight {
public:
    explicit OsgLight(osg::Light* light);
    virtual ~OsgLight();
    
    void setType(LightType type) override;
    LightType getType() const override;
    
    void setPosition(float x, float y, float z) override;
    void setDirection(float x, float y, float z) override;
    void setColor(float r, float g, float b) override;
    void setIntensity(float intensity) override;
    void setAttenuation(float constant, float linear, float quadratic) override;
    void setSpotAngle(float inner, float outer) override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;
    
    osg::Light* getOsgLight() const { return light_; }

private:
    osg::ref_ptr<osg::Light> light_;
    LightType type_ = LightType::Directional;
    bool enabled_ = true;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
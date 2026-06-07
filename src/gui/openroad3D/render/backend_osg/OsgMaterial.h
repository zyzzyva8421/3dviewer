#pragma once

#include <osg/ref_ptr>
#include <osg/Material>

#include "render/api/IMaterial.h"

namespace osg {
class Material;
}

namespace viewer3d {
namespace render {
namespace backend_osg {

/**
 * @brief English description.
 */
class OsgMaterial : public IMaterial {
public:
    OsgMaterial();
    OsgMaterial(const MaterialDesc& desc);
    virtual ~OsgMaterial();
    
    void setColor(float r, float g, float b, float a = 1.0F) override;
    void setColor(const domain::ColorRgba& color) override;
    void getColor(float& r, float& g, float& b, float& a) const override;
    
    void setTransparency(float transparency) override;
    float getTransparency() const override;
    
    void setAmbient(float ambient) override;
    void setDiffuse(float diffuse) override;
    void setSpecular(float specular) override;
    void setShininess(float shininess) override;
    void setEmission(float r, float g, float b) override;
    
    void apply() override;
    
    osg::Material* getOsgMaterial() const { return material_; }

private:
    osg::ref_ptr<osg::Material> material_;
    float transparency_ = 0.0F;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
#include "OsgMaterial.h"

#include <osg/Material>

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgMaterial::OsgMaterial() {
    material_ = new osg::Material();
    material_->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.3f, 0.3f, 0.3f, 1.0f));
    material_->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0.8f, 0.8f, 0.8f, 1.0f));
    material_->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f));
    material_->setShininess(osg::Material::FRONT_AND_BACK, 32.0f);
}

OsgMaterial::OsgMaterial(const MaterialDesc& desc) {
    material_ = new osg::Material();
    
    material_->setAmbient(osg::Material::FRONT_AND_BACK, 
                          osg::Vec4(desc.ambient, desc.ambient, desc.ambient, 1.0f));
    material_->setDiffuse(osg::Material::FRONT_AND_BACK,
                          osg::Vec4(desc.color.r * desc.diffuse,
                                   desc.color.g * desc.diffuse,
                                   desc.color.b * desc.diffuse,
                                   1.0f - desc.transparency));
    material_->setSpecular(osg::Material::FRONT_AND_BACK,
                           osg::Vec4(desc.specular, desc.specular, desc.specular, 1.0f));
    material_->setShininess(osg::Material::FRONT_AND_BACK, desc.shininess);
    material_->setEmission(osg::Material::FRONT_AND_BACK,
                           osg::Vec4(desc.emissionR, desc.emissionG, desc.emissionB, 1.0f));
    
    transparency_ = desc.transparency;
}

OsgMaterial::~OsgMaterial() {
}

void OsgMaterial::setColor(float r, float g, float b, float a) {
    material_->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(r, g, b, a));
    material_->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(r * 0.3f, g * 0.3f, b * 0.3f, a));
}

void OsgMaterial::setColor(const domain::ColorRgba& color) {
    setColor(color.r, color.g, color.b, color.a);
}

void OsgMaterial::getColor(float& r, float& g, float& b, float& a) const {
    osg::Vec4 diffuse = material_->getDiffuse(osg::Material::FRONT_AND_BACK);
    r = diffuse.r();
    g = diffuse.g();
    b = diffuse.b();
    a = diffuse.a();
}

void OsgMaterial::setTransparency(float transparency) {
    transparency_ = transparency;
    osg::Vec4 diffuse = material_->getDiffuse(osg::Material::FRONT_AND_BACK);
    material_->setDiffuse(osg::Material::FRONT_AND_BACK, 
                         osg::Vec4(diffuse.r(), diffuse.g(), diffuse.b(), 1.0f - transparency));
}

float OsgMaterial::getTransparency() const {
    return transparency_;
}

void OsgMaterial::setAmbient(float ambient) {
    osg::Vec4 diffuse = material_->getDiffuse(osg::Material::FRONT_AND_BACK);
    material_->setAmbient(osg::Material::FRONT_AND_BACK, 
                          osg::Vec4(ambient * diffuse.r(), ambient * diffuse.g(), 
                                    ambient * diffuse.b(), diffuse.a()));
}

void OsgMaterial::setDiffuse(float diffuse) {
    osg::Vec4 current = material_->getDiffuse(osg::Material::FRONT_AND_BACK);
    material_->setDiffuse(osg::Material::FRONT_AND_BACK,
                          osg::Vec4(current.r() * diffuse, current.g() * diffuse,
                                    current.b() * diffuse, current.a()));
}

void OsgMaterial::setSpecular(float specular) {
    material_->setSpecular(osg::Material::FRONT_AND_BACK, 
                           osg::Vec4(specular, specular, specular, 1.0f));
}

void OsgMaterial::setShininess(float shininess) {
    material_->setShininess(osg::Material::FRONT_AND_BACK, shininess);
}

void OsgMaterial::setEmission(float r, float g, float b) {
    material_->setEmission(osg::Material::FRONT_AND_BACK, osg::Vec4(r, g, b, 1.0f));
}

void OsgMaterial::apply() {
    // English comment.
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
#include "OsgLight.h"

#include <osg/Light>

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgLight::OsgLight(osg::Light* light)
    : light_(light) {
}

OsgLight::~OsgLight() {
}

void OsgLight::setType(LightType type) {
    type_ = type;
    
    switch (type) {
        case LightType::Ambient:
        case LightType::Directional:
            light_->setPosition(osg::Vec4(0, 0, 1, 0));  // English comment.
            break;
        case LightType::Point:
            light_->setPosition(osg::Vec4(0, 0, 0, 1));  // English comment.
            break;
        case LightType::Spot:
            light_->setPosition(osg::Vec4(0, 0, 0, 1));
            light_->setSpotCutoff(45.0f);
            break;
    }
}

LightType OsgLight::getType() const {
    return type_;
}

void OsgLight::setPosition(float x, float y, float z) {
    osg::Vec4 pos = light_->getPosition();
    light_->setPosition(osg::Vec4(x, y, z, pos.w()));
}

void OsgLight::setDirection(float x, float y, float z) {
    light_->setDirection(osg::Vec3(x, y, z));
}

void OsgLight::setColor(float r, float g, float b) {
    light_->setAmbient(osg::Vec4(r * 0.3f, g * 0.3f, b * 0.3f, 1.0f));
    light_->setDiffuse(osg::Vec4(r, g, b, 1.0f));
    light_->setSpecular(osg::Vec4(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f));
}

void OsgLight::setIntensity(float intensity) {
    osg::Vec4 diffuse = light_->getDiffuse();
    light_->setDiffuse(osg::Vec4(diffuse.r() * intensity, diffuse.g() * intensity,
                                  diffuse.b() * intensity, diffuse.a()));
}

void OsgLight::setAttenuation(float constant, float linear, float quadratic) {
    light_->setConstantAttenuation(constant);
    light_->setLinearAttenuation(linear);
    light_->setQuadraticAttenuation(quadratic);
}

void OsgLight::setSpotAngle(float inner, float outer) {
    light_->setSpotCutoff(outer);
    light_->setSpotExponent((outer - inner) * 2.0f);
}

void OsgLight::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool OsgLight::isEnabled() const {
    return enabled_;
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
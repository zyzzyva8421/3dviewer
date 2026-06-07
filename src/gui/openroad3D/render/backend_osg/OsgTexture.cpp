#include "OsgTexture.h"

#include <osg/Texture2D>

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgTexture::OsgTexture(osg::Texture2D* texture)
    : texture_(texture) {
}

OsgTexture::~OsgTexture() {
}

unsigned int OsgTexture::getTextureId() const {
    return 0;
}

TextureType OsgTexture::getType() const {
    return TextureType::Texture2D;
}

int OsgTexture::getWidth() const {
    if (texture_) {
        return texture_->getTextureWidth();
    }
    return 0;
}

int OsgTexture::getHeight() const {
    if (texture_) {
        return texture_->getTextureHeight();
    }
    return 0;
}

bool OsgTexture::isValid() const {
    return texture_ != nullptr;
}

void OsgTexture::bind(int unit) {
    (void)unit;
}

void OsgTexture::unbind() {
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
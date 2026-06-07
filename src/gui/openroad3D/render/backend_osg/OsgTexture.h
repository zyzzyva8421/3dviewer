#pragma once

#include <osg/ref_ptr>
#include <osg/Texture2D>

#include "render/api/ITexture.h"

namespace osg {
class Texture2D;
}

namespace viewer3d {
namespace render {
namespace backend_osg {

/**
 * @brief English description.
 */
class OsgTexture : public ITexture {
public:
    explicit OsgTexture(osg::Texture2D* texture);
    virtual ~OsgTexture();
    
    unsigned int getTextureId() const override;
    TextureType getType() const override;
    int getWidth() const override;
    int getHeight() const override;
    bool isValid() const override;
    void bind(int unit = 0) override;
    void unbind() override;
    
    osg::Texture2D* getOsgTexture() const { return texture_; }

private:
    osg::ref_ptr<osg::Texture2D> texture_;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
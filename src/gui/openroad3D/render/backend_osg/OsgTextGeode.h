// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <string>

#include <osg/Geode>
#include <osg/Vec4>

namespace viewer3d {
namespace render {
namespace backend_osg {

class OsgFontAtlas;

/**
 * @brief Text geode using glview-style quad mesh rendering
 * 
 * Builds an osg::Geometry with per-character quads using
 * a font atlas texture (stb_truetype approach).
 */
class OsgTextGeode : public osg::Geode {
public:
    /**
     * @brief Create a text geode
     * @param text Text string to render
     * @param fontAtlas Font atlas containing glyph data
     * @param color Text color (used for vertex colors)
     * @param flipY If true, negate vertex Y to flip text orientation (for faces where text appears upside-down)
     * @param scale Scale factor for text (default 1.0 = no scaling, applied to vertices)
     */
    OsgTextGeode(const std::string& text, OsgFontAtlas* fontAtlas, const osg::Vec4& color, float scale = 1.0f);
    
    virtual ~OsgTextGeode() {}
    
    /**
     * @brief Get the text color
     */
    const osg::Vec4& getColor() const { return color_; }
    
private:
    osg::Vec4 color_;
    float scale_;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
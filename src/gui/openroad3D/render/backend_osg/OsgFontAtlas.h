// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <osg/ref_ptr>
#include <osg/Texture2D>

namespace viewer3d {
namespace render {
namespace backend_osg {

struct GlyphInfo {
    float positions[4 * 3];  // 4 vertices, 3 components (x, y, z)
    float uvs[4 * 2];        // 4 vertices, 2 components (u, v)
    float offsetX;
    float offsetY;
};

/**
 * @brief Font atlas generator using stb_truetype
 * 
 * Generates a font atlas texture and provides glyph information
 * for rendering text using quad meshes (glview-style approach).
 */
class OsgFontAtlas {
public:
    OsgFontAtlas();
    ~OsgFontAtlas();
    
    // Disable copy
    OsgFontAtlas(const OsgFontAtlas&) = delete;
    OsgFontAtlas& operator=(const OsgFontAtlas&) = delete;
    
    /**
     * @brief Initialize font from a TTF file
     * @param filename Path to the font file
     * @return true if font loaded successfully
     */
    bool initFont(const std::string& filename);
    
    /**
     * @brief Get glyph info for a character
     * @param c Character code
     * @param offsetX Horizontal offset for next glyph
     * @param offsetY Vertical offset for next glyph
     * @return Glyph info with positions and UVs
     */
    GlyphInfo getGlyphInfo(uint32_t c, float offsetX, float offsetY);
    
    /**
     * @brief Get the OSG texture containing the font atlas
     * @return Texture pointer, or nullptr if not initialized
     */
    osg::Texture2D* getTexture() { return texture_.get(); }
    
    /**
     * @brief Check if atlas is initialized
     */
    bool isValid() const { return texture_.valid() && atlasData_ != nullptr; }
    
    // Atlas dimensions
    static constexpr uint32_t ATLAS_WIDTH = 1024;
    static constexpr uint32_t ATLAS_HEIGHT = 1024;
    static constexpr uint32_t FONT_SIZE = 40;
    static constexpr uint32_t FIRST_CHAR = ' ';
    static constexpr uint32_t CHAR_COUNT = '~' - ' ' + 1;

private:
    std::unique_ptr<uint8_t[]> atlasData_;
    std::vector<uint8_t> charInfo_;  // stbtt_packedchar data
    osg::ref_ptr<osg::Texture2D> texture_;
    
    std::vector<uint8_t> readFile(const std::string& path);
    void createOsgTexture();
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
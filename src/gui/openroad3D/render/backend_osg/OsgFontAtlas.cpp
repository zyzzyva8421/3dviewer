// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#define STB_TRUETYPE_IMPLEMENTATION
#include "OsgFontAtlas.h"

#include <cstdio>
#include <fstream>
#include <iostream>

#include <osg/Texture2D>
#include <osg/Image>

#include "stb_truetype.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

using namespace std;

OsgFontAtlas::OsgFontAtlas() {
}

OsgFontAtlas::~OsgFontAtlas() {
}

std::vector<uint8_t> OsgFontAtlas::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open font file: %s\n", path.c_str());
        return std::vector<uint8_t>();
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(&bytes[0]), size);
    file.close();
    return bytes;
}

bool OsgFontAtlas::initFont(const std::string& filename) {
    auto fontData = readFile(filename);
    if (fontData.empty()) {
        return false;
    }

    // Allocate temp 1-byte-per-pixel buffer for stb_truetype packing
    std::unique_ptr<uint8_t[]> tempAtlas(new uint8_t[ATLAS_WIDTH * ATLAS_HEIGHT]);

    // charInfo_ should be CHAR_COUNT entries of stbtt_packedchar
    charInfo_.resize(CHAR_COUNT * sizeof(stbtt_packedchar));

    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, tempAtlas.get(), ATLAS_WIDTH, ATLAS_HEIGHT, 0, 1, nullptr)) {
        fprintf(stderr, "Failed to initialize font atlas\n");
        return false;
    }

    stbtt_PackSetOversampling(&context, 2, 2);

    stbtt_packedchar* charInfo = reinterpret_cast<stbtt_packedchar*>(charInfo_.data());
    if (!stbtt_PackFontRange(&context, fontData.data(), 0, FONT_SIZE, FIRST_CHAR, CHAR_COUNT, charInfo)) {
        fprintf(stderr, "Failed to pack font atlas\n");
        stbtt_PackEnd(&context);
        return false;
    }

    stbtt_PackEnd(&context);

    // Allocate 4-bytes-per-pixel atlas (RGBA) and convert coverage to RGBA
    // stb_truetype with 2x2 oversampling produces coverage values averaged over 4 sub-pixels
    // A fully covered pixel has coverage ≈ 4 (max), so multiply by 64 to get full 0-255 range
    atlasData_ = std::make_unique<uint8_t[]>(ATLAS_WIDTH * ATLAS_HEIGHT * 4);
    int nonZeroCount = 0;
    for (int i = 0; i < ATLAS_WIDTH * ATLAS_HEIGHT; i++) {
        uint8_t coverage = tempAtlas[i];
        // Scale coverage by 64 to get full 0-255 range for visible white text
        uint8_t scaledCoverage = std::min(255, coverage * 64);
        atlasData_[4 * i] = scaledCoverage;     // R
        atlasData_[4 * i + 1] = scaledCoverage; // G
        atlasData_[4 * i + 2] = scaledCoverage; // B
        atlasData_[4 * i + 3] = scaledCoverage; // A
        if (coverage > 0) nonZeroCount++;
    }
    fprintf(stderr, "DEBUG initFont: atlas populated with %d non-zero pixels (out of %d)\n",
            nonZeroCount, ATLAS_WIDTH * ATLAS_HEIGHT);

    // DEBUG: find first non-zero pixel and check its coverage value
    int firstNonZero = -1;
    for (int i = 0; i < ATLAS_WIDTH * ATLAS_HEIGHT; i++) {
        if (tempAtlas[i] > 0) {
            firstNonZero = i;
            break;
        }
    }
    if (firstNonZero >= 0) {
        fprintf(stderr, "DEBUG initFont: first non-zero pixel at index %d, coverage=%d\n",
                firstNonZero, tempAtlas[firstNonZero]);
    }

    // Create OSG texture from atlas
    createOsgTexture();

    return texture_.valid();
}

void OsgFontAtlas::createOsgTexture() {
    osg::Image* image = new osg::Image();
    // 2D RGBA texture: r=1 (depth), internalFormat=GL_RGBA
    // NO_DELETE: atlasData_ is owned by the unique_ptr member, not by OSG.
    image->setImage(ATLAS_WIDTH, ATLAS_HEIGHT, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, atlasData_.get(), osg::Image::NO_DELETE);

    texture_ = new osg::Texture2D();
    texture_->setImage(image);
    texture_->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    texture_->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    texture_->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture_->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture_->setMaxAnisotropy(1.0f);
}

GlyphInfo OsgFontAtlas::getGlyphInfo(uint32_t c, float offsetX, float offsetY) {
    GlyphInfo info = {};
    
    if (c < FIRST_CHAR || c > FIRST_CHAR + CHAR_COUNT) {
        // Fallback for characters outside our range - use space
        c = ' ';
    }
    
    stbtt_aligned_quad quad;
    stbtt_packedchar* charInfo = reinterpret_cast<stbtt_packedchar*>(charInfo_.data());
    stbtt_GetPackedQuad(charInfo, ATLAS_WIDTH, ATLAS_HEIGHT, c - FIRST_CHAR, &offsetX, &offsetY, &quad, 1);
    
    // Use quad directly like glview - stb_truetype handles coordinate transforms
    // Y is flipped: y0=top, y1=bottom in stb_truetype, negate for OpenGL (Y up)
    float xmin = quad.x0;
    float xmax = quad.x1;
    float ymin = -quad.y1;  // bottom (negate because stb_truetype Y increases downward)
    float ymax = -quad.y0;  // top
    
    info.offsetX = offsetX;
    info.offsetY = offsetY;
    
    // positions[4 * 3]: 4 vertices, x,y,z - quad values (already absolute in atlas coords)
    // After adding cursorX in OsgTextGeode, positions become absolute
    info.positions[0] = xmin;  info.positions[1] = ymin;  info.positions[2] = 0;
    info.positions[3] = xmin;  info.positions[4] = ymax;  info.positions[5] = 0;
    info.positions[6] = xmax;  info.positions[7] = ymax;  info.positions[8] = 0;
    info.positions[9] = xmax;  info.positions[10] = ymin; info.positions[11] = 0;
    
    // uvs[4 * 2]: 4 vertices, u,v - direct from quad like glview
    info.uvs[0] = quad.s0; info.uvs[1] = quad.t1;
    info.uvs[2] = quad.s0; info.uvs[3] = quad.t0;
    info.uvs[4] = quad.s1; info.uvs[5] = quad.t0;
    info.uvs[6] = quad.s1; info.uvs[7] = quad.t1;
    
    return info;
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
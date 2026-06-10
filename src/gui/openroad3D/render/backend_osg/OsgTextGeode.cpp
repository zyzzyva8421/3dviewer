// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "OsgTextGeode.h"

#include <algorithm>
#include <limits>

#include <osg/Array>
#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Geometry>
#include <osg/StateSet>
#include <osg/Vec3>
#include <osg/Vec2>

#include "OsgFontAtlas.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgTextGeode::OsgTextGeode(const std::string& text,
                           OsgFontAtlas* fontAtlas,
                           const osg::Vec4& color)
    : color_(color) {
  setName("TextGeode:" + text);

  fprintf(stderr, "DEBUG OsgTextGeode CTOR: text=%s fontAtlas=%p\n", text.c_str(), (void*)fontAtlas);

  if (!fontAtlas || text.empty()) {
    fprintf(stderr, "DEBUG OsgTextGeode CTOR: early return - fontAtlas=%p text.empty=%d\n", (void*)fontAtlas, text.empty());
    return;
  }

  const size_t numChars = text.length();
  const size_t numVertices = numChars * 4;
  const size_t numIndices = numChars * 6;

  osg::Vec3Array* vertices = new osg::Vec3Array(numVertices);
  osg::Vec3Array* normals = new osg::Vec3Array(numVertices);
  osg::Vec2Array* uvs = new osg::Vec2Array(numVertices);
  osg::Vec4Array* colors = new osg::Vec4Array(numVertices);
  osg::DrawElementsUShort* indices
      = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLES, numIndices);

  float offsetX = 0.0f;
  float offsetY = 0.0f;
  size_t vertexIdx = 0;
  size_t indexIdx = 0;
  uint16_t baseIndex = 0;

  // Track bounding box for centering (glview-style)
  float minX = std::numeric_limits<float>::max();
  float maxX = -std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxY = -std::numeric_limits<float>::max();

  // DEBUG: Dump first character's vertex and UV data
  static int firstCharDebugCount = 0;

  for (char c : text) {
    uint32_t charCode
        = static_cast<uint32_t>(static_cast<unsigned char>(c));
    GlyphInfo glyph
        = fontAtlas->getGlyphInfo(charCode, offsetX, offsetY);

    osg::Vec3f p0(glyph.positions[0], glyph.positions[1],
                  glyph.positions[2]);
    osg::Vec3f p1(glyph.positions[3], glyph.positions[4],
                  glyph.positions[5]);
    osg::Vec3f p2(glyph.positions[6], glyph.positions[7],
                  glyph.positions[8]);
    osg::Vec3f p3(glyph.positions[9], glyph.positions[10],
                  glyph.positions[11]);

    // DEBUG: Log first char's first vertex position
    static int vertexDebugCount = 0;
    if (vertexDebugCount < 3 && text.length() > 0) {
        fprintf(stderr, "DEBUG vertex: text=%s char='%c' idx=%zu p0=(%.1f,%.1f,%.1f) offsetX=%.1f offsetY=%.1f glyph_offsetX=%.1f glyph_offsetY=%.1f\n",
                text.c_str(), c, vertexIdx/4, p0.x(), p0.y(), p0.z(), offsetX, offsetY, glyph.offsetX, glyph.offsetY);
        if (vertexIdx/4 >= text.length() - 1) vertexDebugCount++;
    }

    // Track extents
    minX = std::min({minX, p0.x(), p1.x(), p2.x(), p3.x()});
    maxX = std::max({maxX, p0.x(), p1.x(), p2.x(), p3.x()});
    minY = std::min({minY, p0.y(), p1.y(), p2.y(), p3.y()});
    maxY = std::max({maxY, p0.y(), p1.y(), p2.y(), p3.y()});

    osg::Vec3f edge1 = p1 - p0;
    osg::Vec3f edge2 = p2 - p0;
    osg::Vec3f normal = edge1 ^ edge2;
    normal.normalize();

    // DEBUG: Dump first character's vertex and UV data before storing
    if (firstCharDebugCount < 3 && vertexIdx == 0) {
        fprintf(stderr, "DEBUG charData: text=%s char='%c' p0=(%.2f,%.2f,%.2f) p1=(%.2f,%.2f,%.2f) p2=(%.2f,%.2f,%.2f) p3=(%.2f,%.2f,%.2f)\n",
                text.c_str(), c, p0.x(), p0.y(), p0.z(), p1.x(), p1.y(), p1.z(), p2.x(), p2.y(), p2.z(), p3.x(), p3.y(), p3.z());
        fprintf(stderr, "DEBUG charUVs: text=%s u0=(%.4f,%.4f) u1=(%.4f,%.4f) u2=(%.4f,%.4f) u3=(%.4f,%.4f)\n",
                text.c_str(), 
                glyph.uvs[0], glyph.uvs[1],
                glyph.uvs[2], glyph.uvs[3],
                glyph.uvs[4], glyph.uvs[5],
                glyph.uvs[6], glyph.uvs[7]);
        firstCharDebugCount++;
    }

    (*vertices)[vertexIdx] = p0;
    (*vertices)[vertexIdx + 1] = p1;
    (*vertices)[vertexIdx + 2] = p2;
    (*vertices)[vertexIdx + 3] = p3;

    (*normals)[vertexIdx] = normal;
    (*normals)[vertexIdx + 1] = normal;
    (*normals)[vertexIdx + 2] = normal;
    (*normals)[vertexIdx + 3] = normal;

    (*uvs)[vertexIdx].set(glyph.uvs[0], glyph.uvs[1]);
    (*uvs)[vertexIdx + 1].set(glyph.uvs[2], glyph.uvs[3]);
    (*uvs)[vertexIdx + 2].set(glyph.uvs[4], glyph.uvs[5]);
    (*uvs)[vertexIdx + 3].set(glyph.uvs[6], glyph.uvs[7]);

    (*colors)[vertexIdx] = color_;
    (*colors)[vertexIdx + 1] = color_;
    (*colors)[vertexIdx + 2] = color_;
    (*colors)[vertexIdx + 3] = color_;

    (*indices)[indexIdx++] = baseIndex;
    (*indices)[indexIdx++] = baseIndex + 1;
    (*indices)[indexIdx++] = baseIndex + 2;
    (*indices)[indexIdx++] = baseIndex;
    (*indices)[indexIdx++] = baseIndex + 2;
    (*indices)[indexIdx++] = baseIndex + 3;

    offsetX = glyph.offsetX;
    offsetY = glyph.offsetY;
    baseIndex += 4;
  }

  // Center text around origin (glview-style)
  float centerX = (minX + maxX) * 0.5f;
  float centerY = (minY + maxY) * 0.5f;
  
  // DEBUG: Log positioning info for first text created
  static int textDebugCount = 0;
  if (textDebugCount < 3 && text.length() > 0) {
    fprintf(stderr, "DEBUG TextGeode: text=%s numChars=%zu centerX=%.2f centerY=%.2f minX=%.2f maxX=%.2f minY=%.2f maxY=%.2f\n",
            text.c_str(), numChars, centerX, centerY, minX, maxX, minY, maxY);
    textDebugCount++;
  }
  
  for (size_t i = 0; i < numVertices; ++i) {
    (*vertices)[i].x() -= centerX;
    (*vertices)[i].y() -= centerY;
  }

  // DEBUG: Dump first few vertices after centering
  static int afterCenteringDebugCount = 0;
  if (afterCenteringDebugCount < 3 && text.length() > 0) {
    fprintf(stderr, "DEBUG afterCenter: text=%s first4verts=((%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f))\n",
            text.c_str(),
            (*vertices)[0].x(), (*vertices)[0].y(), (*vertices)[0].z(),
            (*vertices)[1].x(), (*vertices)[1].y(), (*vertices)[1].z(),
            (*vertices)[2].x(), (*vertices)[2].y(), (*vertices)[2].z(),
            (*vertices)[3].x(), (*vertices)[3].y(), (*vertices)[3].z());
    afterCenteringDebugCount++;
  }

  osg::Geometry* geom = new osg::Geometry();
  geom->setVertexArray(vertices);
  geom->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
  geom->setTexCoordArray(0, uvs);
  geom->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
  geom->addPrimitiveSet(indices);
  geom->setName("TextGeometry:" + text);

  addDrawable(geom);

  // Configure rendering state on the Geode
  osg::StateSet* stateSet = getOrCreateStateSet();

  // Bind the font atlas texture
  if (fontAtlas->getTexture()) {
    stateSet->setTextureAttributeAndModes(
        0, fontAtlas->getTexture(), osg::StateAttribute::ON);
  }

  // Text is flat quads — no lighting needed
  stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
  stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
  stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
  stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

  // Set blend function to match glview (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
  osg::BlendFunc* blendFunc = new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  stateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

  // Disable back-face culling so text is visible from all angles
  osg::CullFace* cullFace = new osg::CullFace();
  cullFace->setMode(osg::CullFace::BACK);
  stateSet->setAttributeAndModes(cullFace, osg::StateAttribute::OFF);
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d

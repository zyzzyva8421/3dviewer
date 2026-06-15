// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "OsgTextGeode.h"

#include <algorithm>
#include <limits>

#include <osg/Array>
#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/PolygonOffset>
#include <osg/Geometry>
#include <osg/StateSet>
#include <osg/TexEnv>
#include <osg/Vec3>
#include <osg/Vec2>

#include "OsgFontAtlas.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgTextGeode::OsgTextGeode(const std::string& text,
                           OsgFontAtlas* fontAtlas,
                           const osg::Vec4& color,
                           float scale)
    : color_(color), scale_(scale) {
  setName("TextGeode:" + text);

  if (!fontAtlas || text.empty()) {
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
  int curVertex = 0;
  int curIndex = 0;
  uint16_t lastIndex = 0;

  for (char c : text) {
    uint32_t charCode
        = static_cast<uint32_t>(static_cast<unsigned char>(c));
    GlyphInfo glyph
        = fontAtlas->getGlyphInfo(charCode, offsetX, offsetY);
    offsetX = glyph.offsetX;
    offsetY = glyph.offsetY;

    // FIX: Track bounding box for centering (first pass)
    // We'll use offsetX/offsetY after this loop to compute center

    osg::Vec3f p0(glyph.positions[0], glyph.positions[1],
                  glyph.positions[2]);
    osg::Vec3f p1(glyph.positions[3], glyph.positions[4],
                  glyph.positions[5]);
    osg::Vec3f p2(glyph.positions[6], glyph.positions[7],
                  glyph.positions[8]);
    osg::Vec3f p3(glyph.positions[9], glyph.positions[10],
                  glyph.positions[11]);

    // Normal: cross(p2-p1, p1-p0) — match glview exactly.
    // p0=BL, p1=TL, p2=TR. This gives +Z normal (faces viewer).
    osg::Vec3f edge1 = p2 - p1;
    osg::Vec3f edge2 = p1 - p0;
    osg::Vec3f normal = edge1 ^ edge2;
    normal.normalize();

    (*vertices)[curVertex] = p0 * scale;
    (*vertices)[curVertex + 1] = p1 * scale;
    (*vertices)[curVertex + 2] = p2 * scale;
    (*vertices)[curVertex + 3] = p3 * scale;

    (*normals)[curVertex] = normal;
    (*normals)[curVertex + 1] = normal;
    (*normals)[curVertex + 2] = normal;
    (*normals)[curVertex + 3] = normal;

    (*uvs)[curVertex].set(glyph.uvs[0], glyph.uvs[1]);
    (*uvs)[curVertex + 1].set(glyph.uvs[2], glyph.uvs[3]);
    (*uvs)[curVertex + 2].set(glyph.uvs[4], glyph.uvs[5]);
    (*uvs)[curVertex + 3].set(glyph.uvs[6], glyph.uvs[7]);

    (*colors)[curVertex] = color_;
    (*colors)[curVertex + 1] = color_;
    (*colors)[curVertex + 2] = color_;
    (*colors)[curVertex + 3] = color_;

    (*indices)[curIndex++] = lastIndex;
    (*indices)[curIndex++] = lastIndex + 1;
    (*indices)[curIndex++] = lastIndex + 2;
    (*indices)[curIndex++] = lastIndex;
    (*indices)[curIndex++] = lastIndex + 2;
    (*indices)[curIndex++] = lastIndex + 3;

    curVertex += 4;
    lastIndex += 4;
  }

  // Center text vertices around origin so the text center matches the
  // marker position.  Compute the actual visual glyph bounding box from
  // all vertices rather than using cursor advance (offsetX), which
  // includes side bearings and can shift the visual center.
  float visMinX = 1e10f, visMaxX = -1e10f;
  float visMinY = 1e10f, visMaxY = -1e10f;
  for (size_t i = 0; i < numVertices; ++i) {
    visMinX = std::min(visMinX, (*vertices)[i].x());
    visMaxX = std::max(visMaxX, (*vertices)[i].x());
    visMinY = std::min(visMinY, (*vertices)[i].y());
    visMaxY = std::max(visMaxY, (*vertices)[i].y());
  }
  float centerX = (visMinX + visMaxX) * 0.5f;
  float centerY = (visMinY + visMaxY) * 0.5f;

  // Apply centering to all vertices
  for (size_t i = 0; i < numVertices; ++i) {
    (*vertices)[i].x() -= centerX;
    (*vertices)[i].y() -= centerY;
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

    // Use MODULATE mode: C = vertex_color * texture_color
    // With white texture and colored vertex, text renders as the vertex color.
    osg::TexEnv* texEnv = new osg::TexEnv(osg::TexEnv::MODULATE);
    stateSet->setTextureAttribute(0, texEnv);
  }

  // Text is flat quads
  stateSet->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
  stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
  stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
  stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

  // LEQUAL + no write: text only shows on faces visible to camera,
  // text on hidden/back faces is occluded by geometry.
  osg::Depth* depth = new osg::Depth();
  depth->setWriteMask(false);
  depth->setFunction(osg::Depth::LEQUAL);
  stateSet->setAttributeAndModes(depth, osg::StateAttribute::ON);
  // Small polygon offset ensures text at the same depth as geometry
  // still passes the depth test (prevents z-fighting).
  osg::PolygonOffset* polyOffset = new osg::PolygonOffset(-1.0f, -1.0f);
  stateSet->setAttributeAndModes(polyOffset, osg::StateAttribute::ON);

  // Set blend function (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
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

#include "OsgGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Array>
#include <osg/Geometry>

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgGeometry::OsgGeometry(GeometryType type, float width, float height, float depth)
        : type_(type) {
    init();
    createBox(0.0F, 0.0F, width, height, depth);
}

OsgGeometry::OsgGeometry(GeometryType type, float radius, float height, float /*radius2*/, int segments)
        : type_(type) {
    init();
    if (type == GeometryType::Sphere) {
        createSphere(radius, segments);
    } else if (type == GeometryType::Cylinder) {
        createCylinder(radius, height, segments);
    }
}

OsgGeometry::OsgGeometry(const GeometryParams& params)
        : type_(params.type) {
    init();
    switch (params.type) {
        case GeometryType::Cube:
        case GeometryType::Box:
            createBox(params.position.x, params.position.y, params.size.x, params.size.y, params.size.z);
            break;
        case GeometryType::Polygon:
            createPolygon(params.polygonVertices, params.height);
            break;
        case GeometryType::Polyline:
            createPolyline(params.vertices, params.size.x);
            break;
        default:
            break;
    }
}

OsgGeometry::~OsgGeometry() = default;

void OsgGeometry::init() {
    minBox_ = {0.0F, 0.0F, 0.0F};
    maxBox_ = {1.0F, 1.0F, 1.0F};
    vertices_.clear();
    indices_.clear();
}

void OsgGeometry::createCube() {
    createBox(0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
}

void OsgGeometry::createBox(float x, float y, float width, float height, float depth) {
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> norms = new osg::Vec3Array();

    const float x0 = x;
    const float y0 = y;
    const float z0 = 0.0F;
    const float x1 = x + width;
    const float y1 = y + height;
    const float z1 = depth;

    const osg::Vec3 p[8] = {
            osg::Vec3(x0, y0, z0), osg::Vec3(x1, y0, z0), osg::Vec3(x1, y1, z0), osg::Vec3(x0, y1, z0),
            osg::Vec3(x0, y0, z1), osg::Vec3(x1, y0, z1), osg::Vec3(x1, y1, z1), osg::Vec3(x0, y1, z1)};

    const int faces[6][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, {3, 2, 6, 7},
            {0, 1, 5, 4}, {1, 2, 6, 5}, {0, 3, 7, 4}};
    const osg::Vec3 faceNormals[6] = {
            osg::Vec3(0, 0, -1), osg::Vec3(0, 0, 1), osg::Vec3(0, 1, 0),
            osg::Vec3(0, -1, 0), osg::Vec3(1, 0, 0), osg::Vec3(-1, 0, 0)};

    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 4; ++k) {
            const osg::Vec3 v = p[faces[f][k]];
            verts->push_back(v);
            norms->push_back(faceNormals[f]);
            vertices_.push_back({Vec3{v.x(), v.y(), v.z()}, Vec3{faceNormals[f].x(), faceNormals[f].y(), faceNormals[f].z()}, Vec2{0, 0}, Vec3{1, 0, 0}});
        }
    }

    geometry_ = new osg::Geometry();
    geometry_->setVertexArray(verts);
    geometry_->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geometry_->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, static_cast<int>(verts->size())));

    minBox_ = {x0, y0, z0};
    maxBox_ = {x1, y1, z1};
}

void OsgGeometry::createPolygon(const std::vector<Vec2>& polygon, float height) {
    if (polygon.size() < 3) {
        return;
    }

    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> norms = new osg::Vec3Array();
    osg::ref_ptr<osg::DrawElementsUInt> tris = new osg::DrawElementsUInt(GL_TRIANGLES);

    for (const auto& v : polygon) {
        verts->push_back(osg::Vec3(v.x, v.y, 0.0F));
        norms->push_back(osg::Vec3(0, 0, -1));
    }
    const std::size_t topBase = polygon.size();
    for (const auto& v : polygon) {
        verts->push_back(osg::Vec3(v.x, v.y, height));
        norms->push_back(osg::Vec3(0, 0, 1));
    }

    for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
        tris->push_back(0);
        tris->push_back(static_cast<unsigned int>(i + 1));
        tris->push_back(static_cast<unsigned int>(i));

        tris->push_back(static_cast<unsigned int>(topBase));
        tris->push_back(static_cast<unsigned int>(topBase + i));
        tris->push_back(static_cast<unsigned int>(topBase + i + 1));
    }

    geometry_ = new osg::Geometry();
    geometry_->setVertexArray(verts);
    geometry_->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geometry_->addPrimitiveSet(tris);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (const auto& v : polygon) {
        minX = std::min(minX, v.x);
        minY = std::min(minY, v.y);
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
    }
    minBox_ = {minX, minY, 0.0F};
    maxBox_ = {maxX, maxY, height};
}

void OsgGeometry::createPolyline(const std::vector<Vec3>& points, float /*width*/) {
    if (points.size() < 2) {
        return;
    }
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    for (const auto& p : points) {
        verts->push_back(osg::Vec3(p.x, p.y, p.z));
        vertices_.push_back({p, Vec3{0, 0, 1}, Vec2{0, 0}, Vec3{1, 0, 0}});
    }
    geometry_ = new osg::Geometry();
    geometry_->setVertexArray(verts);
    geometry_->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, static_cast<int>(verts->size())));
}

void OsgGeometry::createSphere(float radius, int segments) {
    if (segments < 4) {
        segments = 4;
    }
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> norms = new osg::Vec3Array();
    osg::ref_ptr<osg::DrawElementsUInt> tris = new osg::DrawElementsUInt(GL_TRIANGLES);

    for (int lat = 0; lat <= segments; ++lat) {
        const float theta = static_cast<float>(lat) * static_cast<float>(M_PI) / static_cast<float>(segments);
        const float st = std::sin(theta);
        const float ct = std::cos(theta);
        for (int lon = 0; lon <= segments; ++lon) {
            const float phi = static_cast<float>(lon) * 2.0F * static_cast<float>(M_PI) / static_cast<float>(segments);
            const float sp = std::sin(phi);
            const float cp = std::cos(phi);
            const float nx = cp * st;
            const float ny = sp * st;
            const float nz = ct;
            verts->push_back(osg::Vec3(nx * radius, ny * radius, nz * radius));
            norms->push_back(osg::Vec3(nx, ny, nz));
        }
    }

    for (int lat = 0; lat < segments; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            const unsigned int a = static_cast<unsigned int>(lat * (segments + 1) + lon);
            const unsigned int b = a + static_cast<unsigned int>(segments + 1);
            tris->push_back(a);
            tris->push_back(b);
            tris->push_back(a + 1);
            tris->push_back(b);
            tris->push_back(b + 1);
            tris->push_back(a + 1);
        }
    }

    geometry_ = new osg::Geometry();
    geometry_->setVertexArray(verts);
    geometry_->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geometry_->addPrimitiveSet(tris);

    minBox_ = {-radius, -radius, -radius};
    maxBox_ = {radius, radius, radius};
}

void OsgGeometry::createCylinder(float radius, float height, int /*segments*/) {
    createBox(-radius, -radius, radius * 2.0F, radius * 2.0F, height);
}

void OsgGeometry::getBoundingBox(Vec3& min, Vec3& max) const {
    min = minBox_;
    max = maxBox_;
}

void OsgGeometry::computeNormals() {
    if (!geometry_) {
        return;
    }
    osg::Vec3Array* verts = dynamic_cast<osg::Vec3Array*>(geometry_->getVertexArray());
    if (!verts) {
        return;
    }
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array(verts->size());
    for (std::size_t i = 0; i < verts->size(); ++i) {
        (*normals)[i] = osg::Vec3(0.0F, 0.0F, 1.0F);
    }
    geometry_->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
}

void OsgGeometry::computeTangents() {
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
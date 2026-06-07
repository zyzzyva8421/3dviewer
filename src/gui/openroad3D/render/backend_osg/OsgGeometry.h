#pragma once

#include <vector>
#include <cstdint>
#include <array>

#include <osg/ref_ptr>
#include <osg/Geometry>

#include "render/api/IGeometry.h"

namespace osg {
class Geometry;
}

namespace viewer3d {
namespace render {
namespace backend_osg {

/**
 * @brief English description.
 */
class OsgGeometry : public IGeometry {
public:
    // English comment.
    OsgGeometry(GeometryType type, float width, float height, float depth);
    
    // English comment.
    OsgGeometry(GeometryType type, float radius, float height, float radius2, int segments);
    
    // English comment.
    OsgGeometry(const GeometryParams& params);
    
    virtual ~OsgGeometry();
    
    GeometryType getType() const override { return type_; }
    const std::vector<Vertex>& getVertices() const override { return vertices_; }
    const std::vector<uint32_t>& getIndices() const override { return indices_; }
    void getBoundingBox(Vec3& min, Vec3& max) const override;
    
    void computeNormals() override;
    void computeTangents() override;
    
    osg::Geometry* getOsgGeometry() const { return geometry_; }

private:
    void init();
    void createCube();
    void createBox(float x, float y, float width, float height, float depth);
    void createPolygon(const std::vector<Vec2>& vertices, float height);
    void createPolyline(const std::vector<Vec3>& points, float width);
    void createSphere(float radius, int segments);
    void createCylinder(float radius, float height, int segments);
    
    GeometryType type_;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    Vec3 minBox_;
    Vec3 maxBox_;
    osg::ref_ptr<osg::Geometry> geometry_;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
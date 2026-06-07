#pragma once

#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 */
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

/**
 * @brief English description.
 */
struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

/**
 * @brief English description.
 */
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec3 tangent;
};

/**
 * @brief English description.
 */
enum class GeometryType {
    Cube,           // English comment.
    Box,            // English comment.
    Polygon,        // English comment.
    Polyline,       // English comment.
    Point,          // English comment.
    Text,           // English comment.
    Sphere,         // English comment.
    Cylinder        // English comment.
};

/**
 * @brief English description.
 */
struct GeometryParams {
    // English comment.
    GeometryType type = GeometryType::Cube;
    
    // English comment.
    Vec3 position;
    std::array<float, 16> transform;  // English comment.
    
    // English comment.
    Vec3 size;           // English comment.
    float height = 0.0F; // English comment.
    
    // English comment.
    std::vector<Vec3> vertices;      // English comment.
    std::vector<uint32_t> indices;   // English comment.
    
    // English comment.
    std::vector<Vec2> polygonVertices;
    
    // English comment.
    std::string text;
    float fontSize = 1.0F;
    
    // English comment.
    float lodDistance = 0.0F;
    int lodLevel = 0;
};

/**
 * @brief English description.
 * 
 * English documentation.
 */
class IGeometry {
public:
    virtual ~IGeometry() = default;
    
    /**
     * @brief English description.
     */
    virtual GeometryType getType() const = 0;
    
    /**
     * @brief English description.
     */
    virtual const std::vector<Vertex>& getVertices() const = 0;
    
    /**
     * @brief English description.
     */
    virtual const std::vector<uint32_t>& getIndices() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void getBoundingBox(Vec3& min, Vec3& max) const = 0;
    
    /**
     * @brief English description.
     */
    virtual void computeNormals() = 0;
    
    /**
     * @brief English description.
     */
    virtual void computeTangents() = 0;
};

/**
 * @brief English description.
 */
class IGeometryFactory {
public:
    virtual ~IGeometryFactory() = default;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createCube(float width, float height, float depth) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createBox(float x, float y, float width, float height, float depth) = 0;
    
    /**
     * @brief English description.
     * @param vertices Parameter description.
     * @param height Parameter description.
     */
    virtual IGeometry* createPolygon(const std::vector<Vec2>& vertices, float height) = 0;
    
    /**
     * @brief English description.
     * @param points Parameter description.
     * @param width Parameter description.
     */
    virtual IGeometry* createPolyline(const std::vector<Vec3>& points, float width) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createPoint(float size) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createSphere(float radius, int segments = 16) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createCylinder(float radius, float height, int segments = 16) = 0;
    
    /**
     * @brief English description.
     */
    virtual IGeometry* createFromData(const GeometryParams& params) = 0;
};

}  // namespace render
}  // namespace viewer3d
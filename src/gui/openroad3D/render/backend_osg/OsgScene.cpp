#include "OsgScene.h"
#include "OsgFontAtlas.h"
#include "OsgGeometry.h"
#include "OsgLight.h"
#include "OsgMaterial.h"
#include "OsgTextGeode.h"
#include "OsgTexture.h"
#include "SpatialIndex.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osg/Material>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/Texture2D>
#include <osg/Texture3D>
#include <osg/Image>
#include <osg/LineWidth>
#include <osg/PolygonMode>
#include <osg/ShadeModel>
#include <osg/LineStipple>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/LOD>
#include <osg/PolygonOffset>
#include <osg/TexGen>
#include <osg/TexEnv>
#include <osg/LineWidth>
#include <osgDB/ReaderWriter>
#include <osgDB/ReadFile>
#include <osgUtil/IntersectVisitor>
#include <osgUtil/LineSegmentIntersector>
#include <osgUtil/SmoothingVisitor>

#include "core/domain/SceneTypes.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

namespace {

struct LocalBoxParams {
    float width = 1.0F;
    float height = 1.0F;
    float depth = 0.3F;
    float xOffset = 0.0F;
    float yOffset = 0.0F;
};

LocalBoxParams computeLocalBoxParamsFromBbox(const domain::ObjectRecord& obj,
                                             float defaultDepth,
                                             float minDepth = 0.01F,
                                             bool offsetFromTransform = true) {
    LocalBoxParams params;
    params.depth = defaultDepth;

    if (!obj.hasBbox) {
        return params;
    }

    params.width = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
    params.height = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
    params.depth = std::max(minDepth, obj.bboxMax[2] - obj.bboxMin[2]);
    if (offsetFromTransform) {
        params.xOffset = obj.bboxMin[0] - obj.transform[12];
        params.yOffset = obj.bboxMin[1] - obj.transform[13];
    } else {
        params.xOffset = obj.bboxMin[0];
        params.yOffset = obj.bboxMin[1];
    }
    return params;
}

bool isDebugLoggingEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("OPENROAD_3D_DEBUG");
        if (!value) {
            return false;
        }
        return value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

std::vector<osg::Vec2> parsePolylinePoints(const std::string& geometryRef) {
    std::vector<osg::Vec2> points;
    const std::size_t polyPos = geometryRef.find("poly:");
    if (polyPos == std::string::npos) {
        return points;
    }

    std::stringstream stream(geometryRef.substr(polyPos + 5));
    std::string token;
    while (std::getline(stream, token, ';')) {
        const std::size_t commaPos = token.find(',');
        if (commaPos == std::string::npos) {
            continue;
        }
        try {
            const float x = std::stof(token.substr(0, commaPos));
            const float y = std::stof(token.substr(commaPos + 1));
            points.emplace_back(x, y);
        } catch (const std::exception&) {
        }
    }
    return points;
}

void applyFlatColor(osg::Geometry* geometry, const osg::Vec4& color) {
    if (!geometry) {
        return;
    }

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array();
    colors->push_back(color);
    geometry->setColorArray(colors, osg::Array::BIND_OVERALL);
}

}  // namespace

// ===========================================================================
// Stipple pattern data (from hi/win/winLayer.c)
// Each pattern is a mono bitmap: bit=1 → dark pixel, bit=0 → transparent.
// ===========================================================================
namespace {

struct StippleDef {
    const char* name;
    int width;
    int height;
    const char* hexData;  // space-separated hex bytes
};

static const StippleDef kStippleDefs[] = {
    {"horizontal", 8, 4,  "0x00 0x00 0x00 0xff"},
    {"vertical",   8, 4,  "0x11 0x11 0x11 0x11"},
    {"grid",       8, 4,  "0x11 0x11 0x11 0xff"},
    {"slash",      8, 8,  "0x80 0x40 0x20 0x10 0x08 0x04 0x02 0x01"},
    {"backslash",  8, 8,  "0x01 0x02 0x04 0x08 0x10 0x20 0x40 0x80"},
    {"cross",      8, 8,  "0x81 0x42 0x24 0x18 0x18 0x24 0x42 0x81"},
    {"brick",     16, 8,  "0xff 0xff 0x40 0x40 0x40 0x40 0x40 0x40 0xff 0xff 0x04 0x04 0x04 0x04 0x04 0x04"},
    {"dot1",       8, 8,  "0x00 0x04 0x00 0x00 0x00 0x00 0x40 0x00"},
    {"dot2",       8, 4,  "0x04 0x00 0x40 0x00"},
    {"dot4",       8, 4,  "0x00 0x22 0x00 0x88"},
    {"slash2",     8, 4,  "0x88 0x44 0x22 0x11"},
    {"backslash2", 8, 4,  "0x11 0x22 0x44 0x88"},
    {"dot8_1",     8, 4,  "0x84 0x21 0x48 0x12"},
    {"dot8_2",     8, 4,  "0x12 0x48 0x21 0x84"},
};

static constexpr int kNumStippleDefs = sizeof(kStippleDefs) / sizeof(kStippleDefs[0]);

// Convert hex string like "0x80 0x40 ..." to RGBA pixel data.
// pattern bit=1 → dark pixel (R=G=B=80, A=255), bit=0 → white pixel (255,255,255,255)
static std::unique_ptr<uint8_t[]> parseStippleData(const StippleDef& def) {
    std::vector<uint8_t> bytes;
    std::string data(def.hexData);
    std::string token;
    std::istringstream iss(data);
    while (iss >> token) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
    }

    auto pixels = std::make_unique<uint8_t[]>(def.width * def.height * 4);
    for (int row = 0; row < def.height; ++row) {
        uint8_t byte = (row < (int)bytes.size()) ? bytes[row] : 0;
        for (int col = 0; col < def.width; ++col) {
            int bit = (byte >> (def.width - 1 - col)) & 1;
            int idx = (row * def.width + col) * 4;
            if (bit) {
                pixels[idx + 0] = 80;    // R
                pixels[idx + 1] = 80;    // G
                pixels[idx + 2] = 80;    // B
                pixels[idx + 3] = 255;   // A
            } else {
                pixels[idx + 0] = 255;   // R
                pixels[idx + 1] = 255;   // G
                pixels[idx + 2] = 255;   // B
                pixels[idx + 3] = 255;   // A
            }
        }
    }
    return pixels;
}

// Map layer name hash to a deterministic stipple index.

// Simple polygon triangulation (ear-clipping) for 2D polygons in the XY plane.
// Returns triangle vertices as (x0,y0, x1,y1, x2,y2, ...).
static std::vector<float> triangulatePolygon(const std::vector<osg::Vec2>& poly) {
    std::vector<float> tris;
    if (poly.size() < 3) return tris;

    // Work on a copy of indices
    std::vector<int> idx(poly.size());
    for (int i = 0; i < (int)poly.size(); ++i) idx[i] = i;

    auto prev = [&](int p) -> int { return (p == 0) ? (int)idx.size()-1 : p-1; };
    auto next = [&](int p) -> int { return (p+1 >= (int)idx.size()) ? 0 : p+1; };

    auto isEar = [&](int p) -> bool {
        int a = prev(p), b = p, c = next(p);
        osg::Vec2 va = poly[idx[a]], vb = poly[idx[b]], vc = poly[idx[c]];
        // Compute signed area of triangle
        float cross = (vb.x()-va.x())*(vc.y()-va.y()) - (vb.y()-va.y())*(vc.x()-va.x());
        if (cross >= 0) return false;  // reflex or degenerate
        // Check no other vertex is inside the triangle
        for (int i = 0; i < (int)idx.size(); ++i) {
            if (i == a || i == b || i == c) continue;
            osg::Vec2 pt = poly[idx[i]];
            // Barycentric check
            float w0 = ((va.y()-vc.y())*(pt.x()-vc.x()) + (vc.x()-va.x())*(pt.y()-vc.y()))
                     / ((va.y()-vc.y())*(vb.x()-vc.x()) + (vc.x()-va.x())*(vb.y()-vc.y()));
            float w1 = ((vc.y()-va.y())*(pt.x()-va.x()) + (va.x()-vc.x())*(pt.y()-va.y()))
                     / ((vc.y()-va.y())*(vb.x()-va.x()) + (va.x()-vc.x())*(vb.y()-va.y()));
            float w2 = 1.0f - w0 - w1;
            if (w0 > 0 && w1 > 0 && w2 > 0) return false;  // inside
        }
        return true;
    };

    int i = 0;
    while (idx.size() > 3 && i < (int)idx.size() * 10) {
        if (isEar(i)) {
            int a = prev(i), b = i, c = next(i);
            tris.push_back(poly[idx[a]].x()); tris.push_back(poly[idx[a]].y());
            tris.push_back(poly[idx[b]].x()); tris.push_back(poly[idx[b]].y());
            tris.push_back(poly[idx[c]].x()); tris.push_back(poly[idx[c]].y());
            idx.erase(idx.begin() + i);
            i = std::max(0, i-1);
            continue;
        }
        i = (i+1) % idx.size();
    }
    // Last triangle
    if (idx.size() == 3) {
        tris.push_back(poly[idx[0]].x()); tris.push_back(poly[idx[0]].y());
        tris.push_back(poly[idx[1]].x()); tris.push_back(poly[idx[1]].y());
        tris.push_back(poly[idx[2]].x()); tris.push_back(poly[idx[2]].y());
    }
    return tris;
}

}  // anonymous namespace (stipple data + polygon helpers)

// English comment.

OsgSceneObject::OsgSceneObject(const std::string& id, osg::MatrixTransform* node)
    : id_(id), node_(node) {
    // English comment.
    transform_.fill(0.0F);
    transform_[0] = transform_[5] = transform_[10] = transform_[15] = 1.0F;
}

OsgSceneObject::~OsgSceneObject() {
    // English comment.
}

void OsgSceneObject::setGeometry(IGeometry* geometry) {
    geometry_ = geometry;
    if (!node_) {
        return;
    }

    if (!geometryNode_) {
        geometryNode_ = new osg::Geode();
        node_->addChild(geometryNode_);
    }

    geometryNode_->removeDrawables(0, geometryNode_->getNumDrawables());
    if (!geometry_) {
        return;
    }

    osg::Geometry* osgGeometry = nullptr;
    OsgGeometry* concreteGeometry = dynamic_cast<OsgGeometry*>(geometry_);
    if (concreteGeometry) {
        osgGeometry = concreteGeometry->getOsgGeometry();
    }

    if (!osgGeometry) {
        const auto& vertices = geometry_->getVertices();
        if (vertices.empty()) {
            return;
        }

        osg::ref_ptr<osg::Vec3Array> osgVertices = new osg::Vec3Array();
        osg::ref_ptr<osg::Vec3Array> osgNormals = new osg::Vec3Array();
        osgVertices->reserve(vertices.size());
        osgNormals->reserve(vertices.size());
        for (const auto& vertex : vertices) {
            osgVertices->push_back(osg::Vec3(vertex.position.x, vertex.position.y, vertex.position.z));
            osgNormals->push_back(osg::Vec3(vertex.normal.x, vertex.normal.y, vertex.normal.z));
        }

        osg::ref_ptr<osg::Geometry> generated = new osg::Geometry();
        generated->setVertexArray(osgVertices);
        generated->setNormalArray(osgNormals, osg::Array::BIND_PER_VERTEX);

        const auto& indices = geometry_->getIndices();
        if (indices.empty()) {
            generated->addPrimitiveSet(
                new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, static_cast<int>(osgVertices->size())));
        } else {
            osg::ref_ptr<osg::DrawElementsUInt> triangles = new osg::DrawElementsUInt(GL_TRIANGLES);
            triangles->reserve(indices.size());
            for (uint32_t index : indices) {
                triangles->push_back(index);
            }
            generated->addPrimitiveSet(triangles);
        }
        osgGeometry = generated.release();
    }

    if (osgGeometry) {
        geometryNode_->addDrawable(osgGeometry);
    }
}

IGeometry* OsgSceneObject::getGeometry() const {
    return geometry_;
}

void OsgSceneObject::setMaterial(IMaterial* material) {
    material_ = material;
    if (!node_) {
        return;
    }

    osg::StateSet* stateset = node_->getOrCreateStateSet();
    if (!stateset) {
        return;
    }

    OsgMaterial* osgMaterial = dynamic_cast<OsgMaterial*>(material_);
    if (!osgMaterial || !osgMaterial->getOsgMaterial()) {
        stateset->removeAttribute(osg::StateAttribute::MATERIAL);
        stateset->setMode(GL_BLEND, osg::StateAttribute::OFF);
        stateset->setRenderingHint(osg::StateSet::DEFAULT_BIN);
        return;
    }

    stateset->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    stateset->setAttributeAndModes(osgMaterial->getOsgMaterial(), osg::StateAttribute::ON);

    const float transparency = std::clamp(material_->getTransparency(), 0.0F, 1.0F);
    if (transparency > 0.0F) {
        stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
        stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    } else {
        stateset->setMode(GL_BLEND, osg::StateAttribute::OFF);
        stateset->setRenderingHint(osg::StateSet::DEFAULT_BIN);
    }

    material_->apply();
}

IMaterial* OsgSceneObject::getMaterial() const {
    return material_;
}

void OsgSceneObject::setTransform(const std::array<float, 16>& transform) {
    transform_ = transform;
    if (node_) {
        osg::Matrixd mat(transform[0], transform[1], transform[2], transform[3],
                        transform[4], transform[5], transform[6], transform[7],
                        transform[8], transform[9], transform[10], transform[11],
                        transform[12], transform[13], transform[14], transform[15]);
        node_->setMatrix(mat);
    }
}

const std::array<float, 16>& OsgSceneObject::getTransform() const {
    return transform_;
}

void OsgSceneObject::setVisible(bool visible) {
    visible_ = visible;
    if (node_) {
        node_->setNodeMask(visible ? 1 : 0);
    }
}

bool OsgSceneObject::isVisible() const {
    return visible_;
}

void OsgSceneObject::setSelectable(bool selectable) {
    selectable_ = selectable;
}

bool OsgSceneObject::isSelectable() const {
    return selectable_;
}

void OsgSceneObject::setSelected(bool selected) {
    selected_ = selected;
}

bool OsgSceneObject::isSelected() const {
    return selected_;
}

void OsgSceneObject::setHighlighted(bool highlighted) {
    highlighted_ = highlighted;
}

bool OsgSceneObject::isHighlighted() const {
    return highlighted_;
}

// English comment.

OsgScene::OsgScene() : osg::Object(true) {
    objectRoot_ = new osg::Group();
    objectRoot_->setName("SceneObjects");

    highlightRoot_ = new osg::Switch();
    highlightRoot_->setName("HighlightObjects");

    // Initialize font atlas for name labels (stb_truetype, glview-style)
    initFontAtlas();
}

OsgScene::~OsgScene() {
    clear();
}

bool OsgScene::initFontAtlas() {
    fontAtlas_ = std::make_unique<OsgFontAtlas>();

    // Try common system font paths
    static const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        nullptr  // sentinel
    };

    for (const char** path = fontPaths; *path; ++path) {
        std::ifstream probe(*path, std::ios::binary);
        if (probe.good()) {
            probe.close();
            if (fontAtlas_->initFont(*path)) {
                return true;
            }
        }
    }

    fprintf(stderr, "WARNING: No font found for text labels\n");
    return false;
}

osg::Object* OsgScene::cloneType() const {
    return new OsgScene();
}

osg::Object* OsgScene::clone(const osg::CopyOp& /*copyop*/) const {
    return new OsgScene();
}

bool OsgScene::isSameKindAs(const osg::Object* obj) const {
    return dynamic_cast<const OsgScene*>(obj) != nullptr;
}

const char* OsgScene::libraryName() const {
    return "viewer3d";
}

const char* OsgScene::className() const {
    return "OsgScene";
}

void OsgScene::addObject(ISceneObject* object) {
    if (!object) return;
    
    OsgSceneObject* osgObj = dynamic_cast<OsgSceneObject*>(object);
    if (!osgObj) return;
    
    osg::Node* node = osgObj->getOsgNode();
    if (node) {
        auto existing = objectsById_.find(object->getId());
        if (existing != objectsById_.end() && existing->second != osgObj) {
            delete existing->second;
        }
        objectRoot_->addChild(node);
        objects_.push_back(node);
        objectsById_[object->getId()] = osgObj;
    }
}

void OsgScene::removeObject(ISceneObject* object) {
    if (!object) return;

    OsgSceneObject* osgObj = dynamic_cast<OsgSceneObject*>(object);
    if (!osgObj) return;

    osg::Node* node = osgObj->getOsgNode();
    if (node) {
        objectRoot_->removeChild(node);
        objects_.erase(std::remove(objects_.begin(), objects_.end(), node), objects_.end());

        // Capture ID before deleting (delete invalidates the `object` pointer)
        const std::string id = object->getId();
        auto byId = objectsById_.find(id);
        if (byId != objectsById_.end()) {
            delete byId->second;
            objectsById_.erase(byId);
        }
        objectLayerById_.erase(id);
    }
}

void OsgScene::clear() {
    objectRoot_->removeChildren(0, objectRoot_->getNumChildren());
    highlightRoot_->removeChildren(0, highlightRoot_->getNumChildren());
    for (auto& entry : objectsById_) {
        delete entry.second;
    }
    objects_.clear();
    objectsById_.clear();
    objectLayerById_.clear();
    layers_.clear();
    highlightNodes_.clear();
    previousSelectedId_.clear();

    // Clear name label state
    textLabelNodes_.clear();
    stippleNodes_.clear();
    objectRecordPtrs_.clear();
    layerStateSetCache_.clear();
    pendingObjects_ = nullptr;
    objectIndexById_.clear();
    layerBatches_.clear();
    batchGeodeToKey_.clear();
    objectIdToBatchKey_.clear();
}

// ============================================================
// BATCHING HELPERS
// ============================================================

void OsgScene::appendBoxToBatch(osg::Vec3Array* verts, osg::Vec3Array* norms,
                                 osg::DrawElementsUInt* tris,
                                 float x, float y, float w, float h, float d,
                                 uint32_t vertOffset, float zOffset) {
    const float z0 = zOffset, x1 = x + w, y1 = y + h, z1 = zOffset + d;
    const auto p000 = osg::Vec3(x,y,z0), p100 = osg::Vec3(x1,y,z0);
    const auto p110 = osg::Vec3(x1,y1,z0), p010 = osg::Vec3(x,y1,z0);
    const auto p001 = osg::Vec3(x,y,z1), p101 = osg::Vec3(x1,y,z1);
    const auto p111 = osg::Vec3(x1,y1,z1), p011 = osg::Vec3(x,y1,z1);
    auto af = [&](const osg::Vec3& a, const osg::Vec3& b, const osg::Vec3& c, const osg::Vec3& d, const osg::Vec3& n) {
        verts->push_back(a); verts->push_back(b); verts->push_back(c); verts->push_back(d);
        norms->push_back(n); norms->push_back(n); norms->push_back(n); norms->push_back(n);
    };
    af(p000,p100,p110,p010, osg::Vec3(0,0,-1)); af(p001,p011,p111,p101, osg::Vec3(0,0,1));
    af(p000,p001,p101,p100, osg::Vec3(0,-1,0)); af(p100,p101,p111,p110, osg::Vec3(1,0,0));
    af(p110,p111,p011,p010, osg::Vec3(0,1,0));  af(p010,p011,p001,p000, osg::Vec3(-1,0,0));
    for (unsigned f = 0; f < 6; ++f) {
        unsigned b = vertOffset + f * 4;
        tris->push_back(b); tris->push_back(b+1); tris->push_back(b+2);
        tris->push_back(b); tris->push_back(b+2); tris->push_back(b+3);
    }
}

void OsgScene::appendWireToBatch(osg::Vec3Array* verts, osg::Vec3Array* norms,
                                  osg::DrawElementsUInt* tris,
                                  const domain::ObjectRecord& obj,
                                  const domain::LayerRecord* /*layer*/,
                                  uint32_t vertOffset, float zOffset) {
    // Wire: render as line from bbox center to edge (simple line for debugging).
    // EDA wires are better represented as boxes (call appendBoxToBatch).
    if (!obj.hasBbox) return;
    // Render wire as a box in world coordinates at the layer's Z plane.
    // XY dimensions come from the wire segment's bbox (typically sub-micron width).
    float wireW = std::max(.01f, obj.bboxMax[0] - obj.bboxMin[0]);
    float wireH = std::max(.01f, obj.bboxMax[1] - obj.bboxMin[1]);
    float wireD = std::max(.01f, obj.bboxMax[2] - obj.bboxMin[2]);
    // Enforce minimum visible size for EDA layout wires (otherwise sub-micron
    // wires are invisible at default zoom). Use lineWidth*2 as minimum screen size.
    // TODO: make minimum configurable
    float minVisible = 0.02f;
    if (wireW < minVisible && wireH < minVisible) {
        wireW = std::max(wireW, minVisible);
        wireH = std::max(wireH, minVisible);
    }
    appendBoxToBatch(verts, norms, tris,
        obj.bboxMin[0], obj.bboxMin[1], wireW, wireH, wireD, vertOffset, zOffset);
}

void OsgScene::buildBatches(const domain::SceneSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(batchMutex_);
    layerBatches_.clear();
    batchGeodeToKey_.clear();
    objectIdToBatchKey_.clear();
    std::unordered_map<std::string, std::vector<const domain::ObjectRecord*>> groups;
    for (const auto& obj : snapshot.objects) {
        if (obj.type != domain::ObjectType::Wire && obj.type != domain::ObjectType::Via) continue;
        auto pit = objectIndexById_.find(obj.objectId);
        if (pit == objectIndexById_.end()) continue;
        std::string key = obj.layerId + "_" + std::to_string(static_cast<int>(obj.type));
        groups[key].push_back(pit->second);
    }
    for (auto& [key, objs] : groups) {
        if (objs.empty()) continue;
        size_t us = key.find_last_of('_');
        std::string lid = key.substr(0, us);
        int ti = std::stoi(key.substr(us + 1));
        domain::ObjectType ot = static_cast<domain::ObjectType>(ti);
        const domain::LayerRecord* layer = getLayer(lid);
        if (!layer) continue;
        LayerBatch batch;
        batch.batchKey = key; batch.layerId = lid; batch.type = ot;
        osg::ref_ptr<osg::Vec3Array> verts(new osg::Vec3Array());
        osg::ref_ptr<osg::Vec3Array> norms(new osg::Vec3Array());
        osg::ref_ptr<osg::DrawElementsUInt> tris(new osg::DrawElementsUInt(GL_TRIANGLES));
        verts->reserve(objs.size() * 24);
        norms->reserve(objs.size() * 24);
        tris->reserve(objs.size() * 36);
        uint32_t tro = 0;
        int addedCount = 0;
        for (auto* op : objs) {
            if (hiddenObjects_.count(op->objectId)) continue;
            uint32_t tb = tris->size();
            uint32_t vertOffset = verts->size();
            if (ot == domain::ObjectType::Via) {
                // World coordinates (batch geode has no transform)
                float x = op->bboxMin[0];
                float y = op->bboxMin[1];
                float w = std::max(.01f, op->bboxMax[0] - op->bboxMin[0]);
                float h = std::max(.01f, op->bboxMax[1] - op->bboxMin[1]);
                float d = std::max(.01f, op->bboxMax[2] - op->bboxMin[2]);
                appendBoxToBatch(verts.get(), norms.get(), tris.get(), x, y, w, h, d, vertOffset, op->bboxMin[2]);
            } else {
                appendWireToBatch(verts.get(), norms.get(), tris.get(), *op, layer, vertOffset, op->bboxMin[2]);
            }
            uint32_t nt = (tris->size() - tb) / 3;
            if (nt > 0) addedCount++;
            batch.primitives.push_back({op->objectId, tro, nt});
            batch.objects.push_back(op);
            objectIdToBatchKey_[op->objectId] = key;
            // Create placeholder OsgSceneObject for batched objects so raycast/selection can find them
            // Note: node is nullptr since geometry is in the batch, not individual node
            if (objectsById_.find(op->objectId) == objectsById_.end()) {
                OsgSceneObject* placeholderObj = new OsgSceneObject(op->objectId, nullptr);
                objectsById_[op->objectId] = placeholderObj;
                objectLayerById_[op->objectId] = lid;
            }
            tro += nt;
        }
        fprintf(stderr, "DEBUG buildBatches: key=%s objs=%zu added=%d verts=%zu tris=%zu\n",
                key.c_str(), objs.size(), addedCount, verts->size(), tris->size()/3);
        if (verts->empty()) continue;
        batch.geometry = new osg::Geometry();
        batch.geometry->setVertexArray(verts.get());
        batch.geometry->setNormalArray(norms.get(), osg::Array::BIND_PER_VERTEX);
        // Set flat color array for the entire batch (GL_LIGHTING OFF uses this)
        {
            osg::Vec4Array* colors = new osg::Vec4Array();
            colors->push_back(osg::Vec4(layer->color.r, layer->color.g, layer->color.b, 1.0f));
            batch.geometry->setColorArray(colors, osg::Array::BIND_OVERALL);
        }
        batch.geometry->addPrimitiveSet(tris.get());
        batch.geometry->setUseVertexBufferObjects(true);
        batch.geode = new osg::Geode();
        batch.geode->addDrawable(batch.geometry.get());
        batch.geode->setName("batch_" + key);
        // Batch flat-shaded StateSet: GL_LIGHTING ON with flat material, depth test ON.
        // Enable depth test and depth writes so proper occlusion happens.
        {
            osg::StateSet* batchSS = new osg::StateSet();
            batchSS->setMode(GL_LIGHTING, osg::StateAttribute::ON);
            batchSS->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
            osg::ref_ptr<osg::Depth> depth = new osg::Depth(osg::Depth::LEQUAL, 0.0, 1.0, true);
            batchSS->setAttributeAndModes(depth.get(), osg::StateAttribute::ON);
            // Set up material for flat shading with layer color
            osg::ref_ptr<osg::Material> mat = new osg::Material();
            mat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.3f, 0.3f, 0.3f, 1.0f));
            mat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(layer->color.r, layer->color.g, layer->color.b, 1.0f));
            mat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
            mat->setShininess(osg::Material::FRONT_AND_BACK, 16.0f);
            batchSS->setAttributeAndModes(mat.get(), osg::StateAttribute::ON);
            batch.geometry->setStateSet(batchSS);
        }
        batch.geode->setNodeMask(0xFFFFFFFF);
        objectRoot_->addChild(batch.geode.get());
        batchGeodeToKey_[batch.geode.get()] = key;
        layerBatches_[key] = std::move(batch);
    }
}

void OsgScene::rebuildBatch(const std::string& batchKey) {
    std::lock_guard<std::mutex> lock(batchMutex_);
    auto it = layerBatches_.find(batchKey);
    if (it == layerBatches_.end()) return;
    LayerBatch& batch = it->second;
    osg::ref_ptr<osg::Vec3Array> verts(new osg::Vec3Array());
    osg::ref_ptr<osg::Vec3Array> norms(new osg::Vec3Array());
    osg::ref_ptr<osg::DrawElementsUInt> tris(new osg::DrawElementsUInt(GL_TRIANGLES));
    const domain::LayerRecord* layer = getLayer(batch.layerId);
    verts->reserve(batch.objects.size() * 24);
    norms->reserve(batch.objects.size() * 24);
    tris->reserve(batch.objects.size() * 36);
    batch.primitives.clear();
    uint32_t tro = 0;
    for (auto* op : batch.objects) {
        if (hiddenObjects_.count(op->objectId)) continue;
        uint32_t tb = tris->size();
        uint32_t vertOffset = verts->size();
        if (batch.type == domain::ObjectType::Via) {
            float x = op->bboxMin[0]; float y = op->bboxMin[1];
            float w = std::max(.01f, op->bboxMax[0] - op->bboxMin[0]); float h = std::max(.01f, op->bboxMax[1] - op->bboxMin[1]);
            float d = std::max(.01f, op->bboxMax[2] - op->bboxMin[2]);
            appendBoxToBatch(verts.get(), norms.get(), tris.get(), x, y, w, h, d, vertOffset, op->bboxMin[2]);
        } else {
            appendWireToBatch(verts.get(), norms.get(), tris.get(), *op, layer, vertOffset, op->bboxMin[2]);
        }
        uint32_t nt = (tris->size() - tb) / 3;
        batch.primitives.push_back({op->objectId, tro, nt});
        tro += nt;
    }
    if (verts->empty()) {
        batch.geode->removeDrawables(0, batch.geode->getNumDrawables());
        batch.geometry = nullptr; return;
    }
    osg::ref_ptr<osg::Geometry> newGeom = new osg::Geometry();
    newGeom->setVertexArray(verts.get());
    newGeom->setNormalArray(norms.get(), osg::Array::BIND_PER_VERTEX);
    {
        osg::Vec4Array* colors = new osg::Vec4Array();
        colors->push_back(osg::Vec4(layer->color.r, layer->color.g, layer->color.b, 1.0f));
        newGeom->setColorArray(colors, osg::Array::BIND_OVERALL);
    }
    newGeom->addPrimitiveSet(tris.get());
    newGeom->setUseVertexBufferObjects(true);
    {
        osg::StateSet* batchSS = new osg::StateSet();
        batchSS->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        batchSS->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
        osg::ref_ptr<osg::Depth> depth = new osg::Depth(osg::Depth::LEQUAL, 0.0, 1.0, true);
        batchSS->setAttributeAndModes(depth.get(), osg::StateAttribute::ON);
        osg::ref_ptr<osg::Material> mat = new osg::Material();
        mat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.3f, 0.3f, 0.3f, 1.0f));
        mat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(layer->color.r, layer->color.g, layer->color.b, 1.0f));
        mat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
        mat->setShininess(osg::Material::FRONT_AND_BACK, 16.0f);
        batchSS->setAttributeAndModes(mat.get(), osg::StateAttribute::ON);
        newGeom->setStateSet(batchSS);
    }
    if (batch.geode->getNumDrawables() > 0) {
        batch.geode->replaceDrawable(batch.geode->getDrawable(0), newGeom.get());
        batch.geometry = newGeom;
    } else {
        batch.geode->addDrawable(newGeom.get());
        batch.geometry = newGeom;
    }
}

const std::string* OsgScene::findBatchObjectId(osg::Geode* geode, uint32_t triIdx) const {
    std::lock_guard<std::mutex> lock(batchMutex_);
    auto it = batchGeodeToKey_.find(geode);
    if (it == batchGeodeToKey_.end()) return nullptr;
    auto bi = layerBatches_.find(it->second);
    if (bi == layerBatches_.end()) return nullptr;
    return bi->second.findObjectIdByTri(triIdx);
}


size_t OsgScene::getObjectCount() const {
    return objects_.size();
}

std::vector<ISceneObject*> OsgScene::getObjectsByLayer(const std::string& layerId) const {
    std::vector<ISceneObject*> result;
    for (const auto& pair : objectsById_) {
        auto layerIt = objectLayerById_.find(pair.first);
        if (layerIt != objectLayerById_.end() && layerIt->second == layerId) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void OsgScene::setLayerVisible(const std::string& layerId, bool visible) {
    auto it = layers_.find(layerId);
    if (it == layers_.end()) return;
    it->second.visible = visible;

    // Collect all object IDs on this layer that need visibility updated
    std::vector<std::string> objectsOnLayer;
    for (const auto& pair : objectsById_) {
        auto layerIt = objectLayerById_.find(pair.first);
        if (layerIt != objectLayerById_.end() && layerIt->second == layerId) {
            objectsOnLayer.push_back(pair.first);
        }
    }

    // For non-batched objects (instances), use setObjectVisible to properly update hiddenObjects_
    for (const auto& objectId : objectsOnLayer) {
        auto batchIt = objectIdToBatchKey_.find(objectId);
        if (batchIt == objectIdToBatchKey_.end()) {
            // Not batched - use setObjectVisible to handle hiddenObjects_ properly
            if (visible) {
                hiddenObjects_.erase(objectId);
            } else {
                hiddenObjects_.insert(objectId);
            }
            auto objIt = objectsById_.find(objectId);
            if (objIt != objectsById_.end() && objIt->second) {
                objIt->second->setVisible(visible);
            }
        }
    }

    // Handle batched objects (wires/vias)
    std::string prefix = layerId + "_";
    for (auto& [key, batch] : layerBatches_) {
        if (key.compare(0, prefix.size(), prefix) == 0 && batch.geode) {
            batch.geode->setNodeMask(visible ? 0xFFFFFFFF : 0);
            if (visible) {
                rebuildBatch(key);
            }
        }
    }
}

bool OsgScene::isLayerVisible(const std::string& layerId) const {
    auto it = layers_.find(layerId);
    return it != layers_.end() ? it->second.visible : true;
}

void OsgScene::updateLayer(const domain::LayerRecord& layer) {
    auto existing = layers_.find(layer.layerId);
    domain::LayerRecord merged = existing != layers_.end() ? existing->second : layer;
    merged.visible = layer.visible;
    merged.selectable = layer.selectable;
    if (layer.lineWidth > 0.0F) {
        merged.lineWidth = layer.lineWidth;
    }
    merged.lineStyle = layer.lineStyle;
    if (!layer.name.empty()) {
        merged.name = layer.name;
    }
    if (!layer.stippleId.empty()) {
        merged.stippleId = layer.stippleId;
    }
    layers_[layer.layerId] = merged;
    setLayerVisible(layer.layerId, merged.visible);
}

const domain::LayerRecord* OsgScene::getLayer(const std::string& layerId) const {
    auto it = layers_.find(layerId);
    return it != layers_.end() ? &it->second : nullptr;
}

ISceneObject* OsgScene::raycast(const Vec3& origin, const Vec3& direction, float maxDistance) {
    if (!objectRoot_) return nullptr;
    osg::Vec3d ro(origin.x, origin.y, origin.z);
    osg::Vec3d rd(direction.x, direction.y, direction.z);
    if (rd.length2() <= 1e-12) return nullptr;
    rd.normalize();
    double dist = maxDistance > 0.0F ? static_cast<double>(maxDistance) : 1e6;
    osg::Vec3d re = ro + rd * dist;
    osg::ref_ptr<osgUtil::LineSegmentIntersector> isect(new osgUtil::LineSegmentIntersector(ro, re));
    osgUtil::IntersectionVisitor visitor(isect.get());
    objectRoot_->accept(visitor);
    if (!isect->containsIntersections()) return nullptr;

    // Distance-priority selection: closest object is selected
    std::vector<std::pair<ISceneObject*, float>> hits;  // (object, distance)
    for (const auto& intersection : isect->getIntersections()) {
        float hd = static_cast<float>(intersection.ratio * dist);
        // Batch geometry hit check
        osg::Geode* geode = intersection.nodePath.empty() ? nullptr
            : dynamic_cast<osg::Geode*>(intersection.nodePath.back());
        if (geode) {
            const std::string* bid = findBatchObjectId(geode, static_cast<uint32_t>(intersection.primitiveIndex));
            if (bid) {
                // Skip if this is the currently selected object (it's shown as highlight)
                if (*bid == selectedObjectId_) continue;
                auto oit = objectsById_.find(*bid);
                if (oit != objectsById_.end() && oit->second) {
                    auto layerIt = objectLayerById_.find(*bid);
                    bool layerVisible = layerIt == objectLayerById_.end() || isLayerVisible(layerIt->second);
                    if (layerVisible && hiddenObjects_.find(*bid) == hiddenObjects_.end()) {
                        hits.emplace_back(oit->second, hd);
                    }
                }
            }
        }
        // Individual node path scan
        for (auto ni = intersection.nodePath.rbegin(); ni != intersection.nodePath.rend(); ++ni) {
            osg::Node* cn = *ni; if (!cn) continue;
            for (auto& [id, sobj] : objectsById_) {
                if (!sobj || !sobj->isSelectable() || !sobj->isVisible()) continue;
                if (sobj->getOsgNode() != cn) continue;
                if (hiddenObjects_.find(id) != hiddenObjects_.end()) continue;
                if (id == selectedObjectId_) continue;  // Skip selected object
                auto layerIt = objectLayerById_.find(id);
                if (layerIt != objectLayerById_.end() && !isLayerVisible(layerIt->second)) continue;
                hits.emplace_back(sobj, hd);
            }
        }
    }
    if (!hits.empty()) {
        // Sort by distance only - closest object is returned
        std::sort(hits.begin(), hits.end(), [](auto& a, auto& b) {
            return a.second < b.second;
        });
        return hits[0].first;
    }
    return nullptr;
}

void OsgScene::getBoundingBox(Vec3& min, Vec3& max) const {
    if (objects_.empty()) {
        min = Vec3{0, 0, 0};
        max = Vec3{0, 0, 0};
        return;
    }
    
    osg::BoundingBox bbox;
    for (auto& node : objects_) {
        osg::BoundingSphere bs = node->getBound();
        if (bs.radius() > 1000.0f || std::abs(bs.center().z()) > 50.0f) {
            fprintf(stderr, "DEBUG hugeBound: node=%s center=(%.1f,%.1f,%.1f) radius=%.1f\n",
                    node->getName().c_str(),
                    bs.center().x(), bs.center().y(), bs.center().z(), bs.radius());
        }
        bbox.expandBy(bs);
        if (isDebugLoggingEnabled()) {
            fprintf(stderr,
                    "DEBUG getBoundingBox: node=%s bound_center=(%f,%f,%f) radius=%f\n",
                    node->getName().c_str(),
                    bs.center().x(),
                    bs.center().y(),
                    bs.center().z(),
                    bs.radius());
        }
    }
    
    min = Vec3{bbox.xMin(), bbox.yMin(), bbox.zMin()};
    max = Vec3{bbox.xMax(), bbox.yMax(), bbox.zMax()};
}

void OsgScene::loadSnapshot(const domain::SceneSnapshot& snapshot) {
    clear();

    for (const auto& layer : snapshot.layers) {
        layers_[layer.layerId] = layer;
    }

    // Store pointer to backend's snapshot objects (no copy)
    pendingObjects_ = &snapshot.objects;
    loadedObjectIds_.clear();
    objectIndexById_.clear();
    objectIndexById_.reserve(pendingObjects_->size());
    for (size_t i = 0; i < pendingObjects_->size(); ++i) {
        objectIndexById_[(*pendingObjects_)[i].objectId] = &(*pendingObjects_)[i];
    }

    // Build spatial index for frustum culling
    if (frustumCullingEnabled_ && !snapshot.objects.empty()) {
        std::vector<std::pair<std::string, AABB>> objectBoxes;
        objectBoxes.reserve(snapshot.objects.size());
        for (const auto& obj : snapshot.objects) {
            if (obj.hasBbox) {
                AABB box({obj.bboxMin[0], obj.bboxMin[1], obj.bboxMin[2]},
                         {obj.bboxMax[0], obj.bboxMax[1], obj.bboxMax[2]});
                objectBoxes.emplace_back(obj.objectId, box);
            }
        }
        if (!objectBoxes.empty()) {
            spatialIndex_.build(objectBoxes, 6);
            fprintf(stderr, "DEBUG: Built spatial index with %zu nodes for %zu objects\n",
                    spatialIndex_.getNodeCount(), objectBoxes.size());
        }
    }

    // Create objects in initial view frustum only (lazy load)
    if (lazyLoadEnabled_) {
        ensureObjectsInFrustum();
    } else {
        // Load all objects immediately
        for (const auto& obj : *pendingObjects_) {
            // Skip batched types (Wire, Via) - geometry in layerBatches_
            if (obj.type == domain::ObjectType::Wire || obj.type == domain::ObjectType::Via) {
                loadedObjectIds_.insert(obj.objectId);
                continue;
            }
            const domain::LayerRecord* layer = getLayer(obj.layerId);
            OsgSceneObject* sceneObj = createSceneObject(obj, layer);
            if (sceneObj) {
                addObject(sceneObj);
                loadedObjectIds_.insert(obj.objectId);
            }
        }
    }

    size_t totalPending = pendingObjects_->size();

    if (isDebugLoggingEnabled()) {
        fprintf(stderr, "DEBUG: loadScene: loaded %zu / %zu objects, %zu layers\n",
                loadedObjectIds_.size(), totalPending, snapshot.layers.size());
    }

    // Build geometry batches for Wire and Via objects
    buildBatches(snapshot);

    // Note: pendingObjects_ points to backend snapshot data
    // objectIndexById_ provides O(1) lookup by storing pointers to ObjectRecord

    // Add XYZ coordinate axes at scene bounding box center
    createCoordinateAxes();
}

void OsgScene::ensureObjectsInFrustum() {
    if (!frustumCullingEnabled_ || !pendingObjects_ || pendingObjects_->empty()) {
        return;
    }

    // Get visible object IDs from spatial index
    std::vector<std::string> visibleIds;
    spatialIndex_.getObjectsInFrustum(currentFrustum_, visibleIds);
    const size_t nVisible = visibleIds.size();
    const size_t nLoaded = loadedObjectIds_.size();
    const size_t nTotal = pendingObjects_->size();

    // Fast path: frustum covers all or nearly all objects.
    // Skip distance sorting since nothing needs prioritized loading,
    // and skip visibleSet construction since nothing can be outside.
    bool allVisible = (nVisible >= nTotal * 0.98f);

    if (allVisible) {
        // Only load newly arrived individual objects (batched types always loaded).
        if (nLoaded < nTotal) {
            for (const auto& objectId : visibleIds) {
                if (loadedObjectIds_.find(objectId) != loadedObjectIds_.end()) {
                    continue;
                }
                auto objIt = objectIndexById_.find(objectId);
                if (objIt == objectIndexById_.end()) continue;
                const domain::ObjectRecord& obj = *objIt->second;
                // Skip batched types (Wire, Via) - geometry in layerBatches_
                if (obj.type == domain::ObjectType::Wire || obj.type == domain::ObjectType::Via) {
                    loadedObjectIds_.insert(objectId);
                    continue;
                }
                // Check if object is hidden or layer is not visible
                if (hiddenObjects_.find(obj.objectId) != hiddenObjects_.end()) {
                    loadedObjectIds_.insert(obj.objectId);
                    continue;
                }
                auto layerIt = objectLayerById_.find(obj.objectId);
                if (layerIt != objectLayerById_.end() && !isLayerVisible(layerIt->second)) {
                    loadedObjectIds_.insert(obj.objectId);
                    continue;
                }
                const domain::LayerRecord* layer = getLayer(obj.layerId);
                OsgSceneObject* sceneObj = createSceneObject(obj, layer);
                if (sceneObj) {
                    addObject(sceneObj);
                    loadedObjectIds_.insert(obj.objectId);
                }
            }
        }
        return;
    }

    // Narrow frustum: sort by distance so nearest objects load first.
    std::vector<std::pair<std::string, float>> objectDistances;
    objectDistances.reserve(nVisible);
    for (const auto& objectId : visibleIds) {
        auto it = objectIndexById_.find(objectId);
        if (it != objectIndexById_.end()) {
            const auto& obj = *it->second;
            float dist = obj.hasBbox ? std::sqrt(obj.bboxMin[0]*obj.bboxMin[0] + obj.bboxMin[1]*obj.bboxMin[1] + obj.bboxMin[2]*obj.bboxMin[2]) : 0.0f;
            objectDistances.emplace_back(objectId, dist);
        }
    }
    std::sort(objectDistances.begin(), objectDistances.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Apply per-frame load cap, then rebuild visibleIds in distance order
    visibleIds.clear();
    size_t pendingNew = 0;
    for (const auto& p : objectDistances) {
        if (loadedObjectIds_.find(p.first) == loadedObjectIds_.end()) {
            if (pendingNew >= kMaxObjectsPerFrame) break;
            pendingNew++;
        }
        visibleIds.push_back(p.first);
    }

    // visibleSet for unload detection (only built on narrow frustum updates)
    std::unordered_set<std::string> visibleSet;
    visibleSet.reserve(visibleIds.size());
    for (const auto& id : visibleIds) {
        visibleSet.insert(id);
    }

    // Unload objects that left the frustum
    std::vector<std::string> toUnload;
    toUnload.reserve(nLoaded);
    for (const auto& objectId : loadedObjectIds_) {
        if (visibleSet.find(objectId) == visibleSet.end()) {
            toUnload.push_back(objectId);
        }
    }
    for (const auto& objectId : toUnload) {
        auto it = objectsById_.find(objectId);
        if (it == objectsById_.end() || !it->second) continue;
        OsgSceneObject* obj = it->second;
        osg::Node* node = obj->getOsgNode();
        if (node) {
            objectRoot_->removeChild(node);
            objects_.erase(std::remove(objects_.begin(), objects_.end(), node), objects_.end());
        }
        textLabelNodes_.erase(objectId);
        stippleNodes_.erase(objectId);
        objectRecordPtrs_.erase(objectId);
        delete obj;
        objectsById_.erase(it);
        objectLayerById_.erase(objectId);
        loadedObjectIds_.erase(objectId);
    }

    // Load newly visible objects within cap (skip batched Wire/Via)
    for (const auto& objectId : visibleIds) {
        if (loadedObjectIds_.find(objectId) != loadedObjectIds_.end()) continue;
        auto objIt = objectIndexById_.find(objectId);
        if (objIt == objectIndexById_.end()) continue;
        const domain::ObjectRecord& obj = *objIt->second;
        // Wire/Via are in layerBatches_, not individual scene objects
        if (obj.type == domain::ObjectType::Wire || obj.type == domain::ObjectType::Via) {
            loadedObjectIds_.insert(objectId);
            continue;
        }
        // Check if object is hidden or layer is not visible
        if (hiddenObjects_.find(objectId) != hiddenObjects_.end()) {
            loadedObjectIds_.insert(objectId);
            continue;
        }
        auto layerIt = objectLayerById_.find(objectId);
        if (layerIt != objectLayerById_.end() && !isLayerVisible(layerIt->second)) {
            loadedObjectIds_.insert(objectId);
            continue;
        }
        const domain::LayerRecord* layer = getLayer(obj.layerId);
        OsgSceneObject* sceneObj = createSceneObject(obj, layer);
        if (sceneObj) {
            addObject(sceneObj);
            loadedObjectIds_.insert(objectId);
        }
    }
}
void OsgScene::upsertObjects(const std::vector<domain::ObjectRecord>& objects) {
    std::unordered_set<std::string> affectedBatches;
    for (const auto& obj : objects) {
        bool isBatchedType = (obj.type == domain::ObjectType::Wire || obj.type == domain::ObjectType::Via);

        auto existing = objectsById_.find(obj.objectId);
        if (existing != objectsById_.end()) {
            OsgSceneObject* existingObj = existing->second;
            osg::Node* node = existingObj ? existingObj->getOsgNode() : nullptr;
            if (node) {
                objectRoot_->removeChild(node);
                objects_.erase(std::remove(objects_.begin(), objects_.end(), node), objects_.end());
            }
            delete existingObj;
            objectsById_.erase(existing);
            objectLayerById_.erase(obj.objectId);
        }

        if (isBatchedType) {
            auto batchIt = objectIdToBatchKey_.find(obj.objectId);
            if (batchIt != objectIdToBatchKey_.end()) {
                affectedBatches.insert(batchIt->second);
                // Bug 1 fix: also update the pointer in batch.objects
                auto bi = layerBatches_.find(batchIt->second);
                if (bi != layerBatches_.end()) {
                    for (auto& op : bi->second.objects) {
                        if (op && op->objectId == obj.objectId) {
                            op = &obj;
                            break;
                        }
                    }
                }
            }
            // Bug 2 fix: for batched types, do NOT create scene object with OSG node.
            // Geometry is in the batch, not in individual scene objects.
            // Just update indices and continue.
            auto idxIt = objectIndexById_.find(obj.objectId);
            if (idxIt != objectIndexById_.end()) {
                idxIt->second = &obj;
            }
            continue;
        }

        const domain::LayerRecord* layer = getLayer(obj.layerId);
        OsgSceneObject* sceneObj = createSceneObject(obj, layer);
        if (sceneObj) {
            addObject(sceneObj);
        }
    }
    for (const auto& key : affectedBatches) {
        rebuildBatch(key);
    }
}

void OsgScene::updateObjectVisibility(
    const std::vector<std::pair<std::string, bool>>& visibilityUpdates) {
    for (const auto& entry : visibilityUpdates) {
        const std::string& objectId = entry.first;
        const bool visible = entry.second;
        setObjectVisible(objectId, visible);
    }
}

void OsgScene::removeObjectsById(const std::vector<std::string>& objectIds) {
    if (objectIds.empty()) {
        return;
    }

    std::unordered_set<std::string> affectedBatches;
    {
        std::lock_guard<std::mutex> lock(batchMutex_);
        for (const auto& id : objectIds) {
            objectIdToBatchKey_.erase(id);
            for (auto& [key, batch] : layerBatches_) {
                auto it = std::remove_if(batch.objects.begin(), batch.objects.end(),
                    [&](const domain::ObjectRecord* p) { return p && p->objectId == id; });
                if (it != batch.objects.end()) {
                    batch.objects.erase(it, batch.objects.end());
                    affectedBatches.insert(key);
                }
            }
        }
    }
    for (const auto& key : affectedBatches) {
        rebuildBatch(key);
    }

    std::unordered_set<std::string> idSet;
    idSet.reserve(objectIds.size());
    for (const auto& id : objectIds) {
        idSet.insert(id);
    }

    for (const auto& id : objectIds) {
        auto existing = objectsById_.find(id);
        if (existing == objectsById_.end()) {
            continue;
        }

        OsgSceneObject* existingObj = existing->second;
        osg::Node* node = existingObj ? existingObj->getOsgNode() : nullptr;
        if (node) {
            objectRoot_->removeChild(node);
            objects_.erase(std::remove(objects_.begin(), objects_.end(), node), objects_.end());
        }

        auto highlight = highlightNodes_.find(id);
        if (highlight != highlightNodes_.end()) {
            osg::Node* highlightNode = highlight->second.get();
            if (highlightNode) {
                osg::Group* parent = highlightNode->getParent(0);
                if (parent) {
                    parent->removeChild(highlightNode);
                }
                highlightRoot_->removeChild(highlightNode);
            }
            highlightNodes_.erase(highlight);
        }

        delete existingObj;
        objectsById_.erase(existing);
        objectLayerById_.erase(id);

        if (previousSelectedId_ == id) {
            previousSelectedId_.clear();
        }
        
        // Clean up name label state
        textLabelNodes_.erase(id);
        stippleNodes_.erase(id);
        objectRecordPtrs_.erase(id);
    }
}

void OsgScene::removeLayersById(const std::vector<std::string>& layerIds) {
    if (layerIds.empty()) {
        return;
    }

    for (const auto& layerId : layerIds) {
        layers_.erase(layerId);
    }
}

void OsgScene::applyGlowMaterial(osg::Node* node) {
    if (!node) return;
    
    osg::StateSet* stateset = node->getOrCreateStateSet();
    
    // English comment.
    osg::Material* material = new osg::Material();
    material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0.3f, 0.3f, 0.3f, 1.0f));
    material->setEmission(osg::Material::FRONT_AND_BACK, osg::Vec4(1.0f, 0.9f, 0.3f, 1.0f));  // English comment.
    material->setShininess(osg::Material::FRONT_AND_BACK, 64.0f);
    stateset->setAttributeAndModes(material, osg::StateAttribute::ON);
    
    // English comment.
    stateset->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    stateset->setMode(GL_BLEND, osg::StateAttribute::OFF);
    stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
}

void OsgScene::restoreObjectMaterial(osg::Node* node) {
    if (!node) return;
    
    osg::StateSet* stateset = node->getStateSet();
    if (!stateset) return;
    
    // English comment.
    stateset->removeAttribute(osg::StateAttribute::MATERIAL);
}

void OsgScene::setSelectedObject(const std::string& objectId) {
    // Clear existing highlights
    for (auto& pair : highlightNodes_) {
        osg::Group* parent = pair.second->getParent(0);
        if (parent) {
            parent->removeChild(pair.second);
        }
        highlightRoot_->removeChild(pair.second);
    }
    highlightNodes_.clear();

    // Restore previous selection if exists
    if (!previousSelectedId_.empty()) {
        auto prevBatchIt = objectIdToBatchKey_.find(previousSelectedId_);
        if (prevBatchIt != objectIdToBatchKey_.end()) {
            // Previous was a batched object - restore it to batch
            hiddenObjects_.erase(previousSelectedId_);
            rebuildBatch(prevBatchIt->second);
        } else {
            auto prevIt = objectsById_.find(previousSelectedId_);
            if (prevIt != objectsById_.end()) {
                osg::Node* prevNode = prevIt->second->getOsgNode();
                if (prevNode) {
                    restoreObjectMaterial(prevNode);
                }
            }
        }
        previousSelectedId_.clear();
    }

    if (objectId.empty()) {
        selectedObjectId_.clear();
        return;
    }

    // Handle new selection
    auto batchIt = objectIdToBatchKey_.find(objectId);
    if (batchIt != objectIdToBatchKey_.end()) {
        // Batched object (Wire/Via)
        std::string batchKey = batchIt->second;
        hiddenObjects_.insert(objectId);
        rebuildBatch(batchKey);

        // Find the ObjectRecord for highlight geometry
        const domain::ObjectRecord* objRec = nullptr;
        auto objIt = objectIndexById_.find(objectId);
        if (objIt != objectIndexById_.end()) {
            objRec = objIt->second;
        }
        if (objRec && objRec->hasBbox) {
            osg::ref_ptr<osg::Vec3Array> verts(new osg::Vec3Array());
            osg::ref_ptr<osg::Vec3Array> norms(new osg::Vec3Array());
            osg::ref_ptr<osg::DrawElementsUInt> tris(new osg::DrawElementsUInt(GL_TRIANGLES));

            float x = objRec->bboxMin[0];
            float y = objRec->bboxMin[1];
            float w = std::max(.01f, objRec->bboxMax[0] - objRec->bboxMin[0]);
            float h = std::max(.01f, objRec->bboxMax[1] - objRec->bboxMin[1]);
            float d = std::max(.01f, objRec->bboxMax[2] - objRec->bboxMin[2]);
            appendBoxToBatch(verts.get(), norms.get(), tris.get(), x, y, w, h, d, 0, objRec->bboxMin[2]);

            osg::Geometry* highlightGeom = new osg::Geometry();
            highlightGeom->setVertexArray(verts.get());
            highlightGeom->setNormalArray(norms.get(), osg::Array::BIND_PER_VERTEX);
            osg::Vec4Array* colors = new osg::Vec4Array();
            colors->push_back(osg::Vec4(1.0f, 0.9f, 0.3f, 1.0f));
            highlightGeom->setColorArray(colors, osg::Array::BIND_OVERALL);
            highlightGeom->addPrimitiveSet(tris.get());
            highlightGeom->setUseVertexBufferObjects(true);

            osg::Geode* geode = new osg::Geode();
            geode->addDrawable(highlightGeom);
            geode->setName("highlight_" + objectId);
            osg::StateSet* ss = geode->getOrCreateStateSet();
            ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
            highlightRoot_->addChild(geode);
            highlightNodes_[objectId] = geode;
        }
        previousSelectedId_ = objectId;
        selectedObjectId_ = objectId;
        return;
    }

    // Non-batched object with OSG node
    auto it = objectsById_.find(objectId);
    if (it != objectsById_.end()) {
        osg::Node* node = it->second->getOsgNode();
        if (node) {
            applyGlowMaterial(node);

            osg::Geometry* highlightGeom = createSelectionGeometry(node);
            osg::Geode* geode = new osg::Geode();
            geode->addDrawable(highlightGeom);

            highlightRoot_->addChild(geode);
            highlightNodes_[objectId] = geode;

            previousSelectedId_ = objectId;
            selectedObjectId_ = objectId;
        }
    }
}

osg::Node* OsgScene::getObjectNode(const std::string& objectId) const {
    auto it = objectsById_.find(objectId);
    return it != objectsById_.end() ? it->second->getOsgNode() : nullptr;
}

OsgSceneObject* OsgScene::createSceneObject(const domain::ObjectRecord& obj, const domain::LayerRecord* layer) {
    osg::MatrixTransform* transform = new osg::MatrixTransform();
    transform->setName(obj.objectId);
    
    if (isDebugLoggingEnabled()) {
        // Debug output for transform (show first 5 wire objects)
        static int wireDebugCount = 0;
        if (obj.objectId.find("wire_") == 0 && wireDebugCount < 5) {
            fprintf(stderr,
                    "DEBUG createSceneObject: %s hasBbox=%d layerId=%s transform=(%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f)\n",
                    obj.objectId.c_str(),
                    obj.hasBbox,
                    obj.layerId.c_str(),
                    obj.transform[0],
                    obj.transform[1],
                    obj.transform[2],
                    obj.transform[3],
                    obj.transform[4],
                    obj.transform[5],
                    obj.transform[6],
                    obj.transform[7],
                    obj.transform[8],
                    obj.transform[9],
                    obj.transform[10],
                    obj.transform[11],
                    obj.transform[12],
                    obj.transform[13],
                    obj.transform[14],
                    obj.transform[15]);
            wireDebugCount++;
        }
        if (obj.objectId.find("debug_") == 0) {
            fprintf(stderr,
                    "DEBUG createSceneObject: %s hasBbox=%d transform=(%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f)\n",
                    obj.objectId.c_str(),
                    obj.hasBbox,
                    obj.transform[0],
                    obj.transform[1],
                    obj.transform[2],
                    obj.transform[3],
                    obj.transform[4],
                    obj.transform[5],
                    obj.transform[6],
                    obj.transform[7],
                    obj.transform[8],
                    obj.transform[9],
                    obj.transform[10],
                    obj.transform[11],
                    obj.transform[12],
                    obj.transform[13],
                    obj.transform[14],
                    obj.transform[15]);
        }
    }
    
    // English comment.
    osg::Matrixd mat(
        obj.transform[0], obj.transform[1], obj.transform[2], obj.transform[3],
        obj.transform[4], obj.transform[5], obj.transform[6], obj.transform[7],
        obj.transform[8], obj.transform[9], obj.transform[10], obj.transform[11],
        obj.transform[12], obj.transform[13], obj.transform[14], obj.transform[15]);
    // Don't add layer->zBase here - the transform already contains the correct Z position
    // from bboxMin[2] which equals getLayerZBase(). Adding it again would double the Z offset.
    transform->setMatrix(mat);
    
    // Debug: check the matrix that was set
    if (isDebugLoggingEnabled() && obj.objectId.find("debug_") == 0) {
        osg::Vec3d t = mat.getTrans();
        fprintf(stderr, "DEBUG createSceneObject: %s matrix translation=(%f,%f,%f)\n",
                obj.objectId.c_str(), t.x(), t.y(), t.z());
    }
    
    OsgSceneObject* sceneObj = new OsgSceneObject(obj.objectId, transform);
    objectLayerById_[obj.objectId] = obj.layerId;
    
    // English comment.
    osg::Geode* geode = new osg::Geode();
    geode->setName(obj.objectId + ":geode");
    

    sceneObj->setVisible((!layer || layer->visible) && obj.visible);
    sceneObj->setSelectable(!layer || layer->selectable);
    attachObjectGeometry(geode, obj, layer);
    
    // English comment.
    if (layer) {
        configureGeodeRenderState(geode, *layer, obj.type);
    }
    
    transform->addChild(geode);

    // Store pointer to object record for label recreation (avoids copying 64-byte transform array)
    objectRecordPtrs_[obj.objectId] = &obj;

    // Attach name labels if display is enabled
    if (displayNamesVisible_ && !obj.displayName.empty()) {
        attachObjectLabels(transform, obj, layer);
    }

    return sceneObj;
}

void OsgScene::setDisplayNamesVisible(bool visible) {
    if (displayNamesVisible_ == visible) {
        return;
    }
    displayNamesVisible_ = visible;

    if (visible) {
        // Rebuild labels for non-batched (Inst, Pin, DRC, Track, Blockage) objects only.
        // Batched types (Wire, Via) don't have individual MatrixTransform nodes.
        for (const auto& pair : objectRecordPtrs_) {
            const std::string& objectId = pair.first;
            const domain::ObjectRecord* obj = pair.second;
            if (obj->type == domain::ObjectType::Wire || obj->type == domain::ObjectType::Via) continue;
            auto objIt = objectsById_.find(objectId);
            if (objIt == objectsById_.end()) {
                continue;
            }
            OsgSceneObject* sceneObj = objIt->second;
            if (!sceneObj) {
                continue;
            }
            osg::MatrixTransform* transform = sceneObj->getOsgNode();
            if (!transform) {
                continue;
            }
            auto layerIt = layers_.find(obj->layerId);
            const domain::LayerRecord* layer = (layerIt != layers_.end()) ? &layerIt->second : nullptr;
            attachObjectLabels(transform, *obj, layer);
        }
    } else {
        // Remove all label nodes
        for (auto& pair : textLabelNodes_) {
            osg::Group* labelGroup = pair.second.get();
            if (labelGroup) {
                osg::Group* parent = labelGroup->getParent(0);
                if (parent) {
                    parent->removeChild(labelGroup);
                }
            }
        }
        textLabelNodes_.clear();

        // Also remove all stipple LOD nodes
        for (auto& pair : stippleNodes_) {
            osg::LOD* stippleLod = pair.second.get();
            if (stippleLod) {
                osg::Group* parent = stippleLod->getParent(0);
                if (parent) {
                    parent->removeChild(stippleLod);
                }
            }
        }
        stippleNodes_.clear();
    }
}

void OsgScene::setStippleVisible(bool visible) {
    if (stippleVisible_ == visible) return;
    stippleVisible_ = visible;

    // Toggle stipple LOD nodes directly without rebuilding labels
    for (auto& pair : stippleNodes_) {
        osg::LOD* stippleLod = pair.second.get();
        if (stippleLod) {
            // Use node mask to show/hide: all bits set = visible, 0 = hidden
            stippleLod->setNodeMask(visible ? 0xFFFFFFFF : 0);
        }
    }
}

void OsgScene::setObjectVisible(const std::string& objectId, bool visible) {
    // Check if it is a batched object (Wire/Via) -> use O(1) reverse index
    auto batchIt = objectIdToBatchKey_.find(objectId);
    if (batchIt != objectIdToBatchKey_.end()) {
        std::string batchKeyToRebuild = batchIt->second;
        if (visible) hiddenObjects_.erase(objectId);
        else hiddenObjects_.insert(objectId);
        // Clean up individual scene object if one was created
        auto it = objectsById_.find(objectId);
        if (it != objectsById_.end()) {
            if (it->second) { osg::Node* n = it->second->getOsgNode(); if (n) objectRoot_->removeChild(n); }
            delete it->second; objectsById_.erase(it); objectLayerById_.erase(objectId); loadedObjectIds_.erase(objectId);
        }
        rebuildBatch(batchKeyToRebuild);
        return;
    }

    // Non-batched object: existing logic
    auto it = objectsById_.find(objectId);
    if (it == objectsById_.end()) return;
    OsgSceneObject* obj = objectsById_[objectId];
    if (!obj) return;
    auto layerIt = objectLayerById_.find(objectId);
    bool layerIsVisible = layerIt != objectLayerById_.end() ? isLayerVisible(layerIt->second) : true;
    bool actuallyVisible = visible && layerIsVisible;
    obj->setVisible(actuallyVisible);
    if (actuallyVisible) hiddenObjects_.erase(objectId);
    else hiddenObjects_.insert(objectId);
}

bool OsgScene::isObjectVisible(const std::string& objectId) const {
    return hiddenObjects_.find(objectId) == hiddenObjects_.end();
}

std::string OsgScene::findObjectIdAtScreen(float nx, float ny) const {
    // Use scene bounding box heuristic: iterate and pick closest visible object
    // For a proper implementation, we'd raycast the scene graph.
    // This simplified version finds any visible object near the screen point.
    for (const auto& pair : objectsById_) {
        if (hiddenObjects_.find(pair.first) != hiddenObjects_.end()) continue;
        if (pair.second && pair.second->isVisible()) {
            return pair.first;
        }
    }
    return "";
}

void OsgScene::updateFrustum(const double* viewMatrix, const double* projectionMatrix) {
    if (!frustumCullingEnabled_) {
        return;
    }

    // Debug: print view matrix
    if (isDebugLoggingEnabled()) {
        fprintf(stderr, "DEBUG View matrix:\n");
        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "  [% .4f % .4f % .4f % .4f]\n",
                    viewMatrix[i*4], viewMatrix[i*4+1], viewMatrix[i*4+2], viewMatrix[i*4+3]);
        }
        fprintf(stderr, "DEBUG Projection matrix:\n");
        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "  [% .4f % .4f % .4f % .4f]\n",
                    projectionMatrix[i*4], projectionMatrix[i*4+1], projectionMatrix[i*4+2], projectionMatrix[i*4+3]);
        }
    }

    // Compute VP = Projection * View (column-major)
    // For column-major: element(row, col) is at index col*4 + row
    // VP(col, row) = sum_k P(k, row) * V(col, k)
    // In 1D: vp[col*4+row] = sum_k P[k*4+row] * V[col*4+k]
    double vp[16] = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            for (int k = 0; k < 4; k++) {
                vp[col * 4 + row] += projectionMatrix[k * 4 + row] * viewMatrix[col * 4 + k];
            }
        }
    }

    // Debug: print VP matrix
    if (isDebugLoggingEnabled()) {
        fprintf(stderr, "DEBUG VP matrix (P*V):\n");
        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "  [% .4f % .4f % .4f % .4f]\n",
                    vp[i*4], vp[i*4+1], vp[i*4+2], vp[i*4+3]);
        }
    }

    // Convert to float for setFromMatrix
    float vpFloat[16];
    for (int i = 0; i < 16; i++) {
        vpFloat[i] = static_cast<float>(vp[i]);
    }
    currentFrustum_.setFromMatrix(vpFloat);

    // Debug: test frustum against FULL scene bounding box
    if (isDebugLoggingEnabled() && pendingObjects_ && !pendingObjects_->empty()) {
        // Compute full scene bbox from all objects
        AABB fullSceneBox;
        for (const auto& obj : *pendingObjects_) {
            if (obj.hasBbox) {
                fullSceneBox.expand({obj.bboxMin[0], obj.bboxMin[1], obj.bboxMin[2]});
                fullSceneBox.expand({obj.bboxMax[0], obj.bboxMax[1], obj.bboxMax[2]});
            }
        }
        fprintf(stderr, "DEBUG fullSceneBox: min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f) center=(%.2f,%.2f,%.2f)\n",
                fullSceneBox.min[0], fullSceneBox.min[1], fullSceneBox.min[2],
                fullSceneBox.max[0], fullSceneBox.max[1], fullSceneBox.max[2],
                fullSceneBox.center()[0], fullSceneBox.center()[1], fullSceneBox.center()[2]);
        FrustumTest test = currentFrustum_.testBox(fullSceneBox);
        fprintf(stderr, "DEBUG frustum vs fullSceneBox: %s\n",
                test == FrustumTest::Inside ? "Inside" :
                test == FrustumTest::Intersect ? "Intersect" : "Outside");
        currentFrustum_.print();
    }
}

void OsgScene::getVisibleObjects(std::vector<std::string>& visibleIds) {
    visibleIds.clear();

    if (!frustumCullingEnabled_) {
        // If culling disabled, return all visible objects
        visibleIds.reserve(objectsById_.size());
        for (const auto& pair : objectsById_) {
            if (hiddenObjects_.find(pair.first) == hiddenObjects_.end()) {
                visibleIds.push_back(pair.first);
            }
        }
        return;
    }

    spatialIndex_.getObjectsInFrustum(currentFrustum_, visibleIds);
}

void OsgScene::attachObjectLabels(osg::MatrixTransform* transform,
                                  const domain::ObjectRecord& obj,
                                  const domain::LayerRecord* layer) {
    if (!transform || obj.displayName.empty()) {
        return;
    }
    
    
    // Remove existing labels for this object
    removeObjectLabels(obj.objectId);
    
    // Compute bbox dimensions
    float xlen = 1.0F;
    float ylen = 1.0F;
    float zlen = 0.3F;
    if (obj.hasBbox) {
        xlen = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
        ylen = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
        zlen = std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2]);

        // DEBUG: Print bbox for net210 (only when debug logging enabled)
        if (isDebugLoggingEnabled() && obj.displayName == "net210") {
            fprintf(stderr, "DEBUG bbox: text=%s bboxMin=(%.4f,%.4f,%.4f) bboxMax=(%.4f,%.4f,%.4f) transform[14]=%.4f\n",
                    obj.displayName.c_str(),
                    obj.bboxMin[0], obj.bboxMin[1], obj.bboxMin[2],
                    obj.bboxMax[0], obj.bboxMax[1], obj.bboxMax[2],
                    obj.transform[14]);
        }
    }

    // Skip labels for tiny objects (charSize would be too small to read or
    // would require a disproportionately large minimum that dwarfs the object).
    float minDim = std::min(xlen, std::min(ylen, zlen));
    if (minDim < 0.05F) {
        return;
    }

    // Text color: white
    osg::Vec4 textColor(1.0F, 1.0F, 1.0F, 1.0F);

    // Text size: fit within the smallest face dimension so text stays on the face.
    // Use the overall min dimension so text fits on all faces.
    float textHeight = std::clamp(std::min({xlen, ylen, zlen}) * 0.4f, 0.05f, 2.0f);
    float charSize = textHeight;

    // Create label group and stipple group
    osg::Group* labelGroup = new osg::Group();
    labelGroup->setName(obj.objectId + ":labels");
    osg::Group* stippleGroup = new osg::Group();
    stippleGroup->setName(obj.objectId + ":stipple");

    // Text on ALL non-cross-section faces that are large enough.
    // Cross-section determined by wire direction (from bbox) for Wire objects,
    // Note: textAlongX is now determined in the face selection block below
    // based on wire bbox longest dimension (glview-style).

    float maxDim = std::max({xlen, ylen, zlen});
    
    // Center point in LOCAL coordinates
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    
    if (obj.hasBbox) {
        centerX = (obj.bboxMax[0] + obj.bboxMin[0]) * 0.5F - obj.transform[12];
        centerY = (obj.bboxMax[1] + obj.bboxMin[1]) * 0.5F - obj.transform[13];
        centerZ = (obj.bboxMax[2] + obj.bboxMin[2]) * 0.5F - obj.transform[14];
    }

    // Surface positions (all 6 faces in LOCAL coords)
    // Text is placed exactly at the surface (no geometric offset).
    // Z-fighting is prevented by PolygonOffset in the text render state.
    float topY    = (obj.hasBbox ? obj.bboxMax[1] : ylen)  - obj.transform[13];
    float bottomY = (obj.hasBbox ? obj.bboxMin[1] : -ylen) - obj.transform[13];
    float rightX  = (obj.hasBbox ? obj.bboxMax[0] : xlen)  - obj.transform[12];
    float leftX   = (obj.hasBbox ? obj.bboxMin[0] : -xlen) - obj.transform[12];
    // Front face is Z-min, Back face is Z-max.
    // NOTE: Geometry Z range in LOCAL coords is [0, zlen] (bottom-aligned,
    // not centered). The front surface is at local Z=0, back at local Z=zlen.
    float frontZ  = 0.0f;                                         // Z-min surface (local Z=0)
    float backZ   = zlen;                                         // Z-max surface (local Z=zlen)
    float zCenterLocal = zlen * 0.5f;                             // Z-center for non-Z face text


    // Minimum face area for text (avoid text on tiny edges)
    float minFaceArea = 0.002f;

    // DEBUG: targeted per-object for metal3 and metal5
    {
      static int dbgObj = 0;
      static FILE* fd = fopen("/tmp/osg_obj_debug.txt", "w");
      if (fd && dbgObj < 10 && layer && (layer->name == "metal3" || layer->name == "metal5")) {
        fprintf(fd, "\n=== OBJ[%d] name='%s' layer='%s' type=%d ===\n",
                dbgObj, obj.displayName.c_str(), layer->name.c_str(), (int)obj.type);
        fprintf(fd, "  WORLD bbox: Min(%.4f,%.4f,%.4f) Max(%.4f,%.4f,%.4f)\n",
                obj.bboxMin[0], obj.bboxMin[1], obj.bboxMin[2],
                obj.bboxMax[0], obj.bboxMax[1], obj.bboxMax[2]);
        fprintf(fd, "  transform[12..14]: (%.4f,%.4f,%.4f)\n",
                obj.transform[12], obj.transform[13], obj.transform[14]);
        fprintf(fd, "  xlen=%.4f ylen=%.4f zlen=%.4f\n", xlen, ylen, zlen);
        fprintf(fd, "  LOCAL center: (%.4f,%.4f,%.4f)\n", centerX, centerY, centerZ);
        fprintf(fd, "  LOCAL surfaces: T=%.4f B=%.4f R=%.4f L=%.4f F=%.4f Bk=%.4f\n",
                topY, bottomY, rightX, leftX, frontZ, backZ);
        fprintf(fd, "  face areas: X=%.5f Y=%.5f Z=%.5f (min=%.5f)\n",
                ylen*zlen, xlen*zlen, xlen*ylen, minFaceArea);
        fprintf(fd, "  charSize=%.4f\n", charSize);
        fflush(fd);
        dbgObj++;
      }
    }

    // glview-style face selection: based on bbox longest dimension, not layer direction.
    // Wire extends along the LONGEST dimension, so:
    //   - xlen longest → wire extends along X (horizontal wire)
    //   - ylen longest → wire extends along Y (vertical wire)
    //   - zlen longest → wire extends along Z (depth wire)
    //
    // Text is placed on faces PERPENDICULAR to wire direction:
    //   - Wire along X → text on front/back (Z) + top/bottom (Y)
    //   - Wire along Y → text on front/back (Z) + right/left (X)
    //   - Wire along Z → text on top/bottom (Y) + right/left (X)
    //
    // Text reading direction follows wire direction:
    //   wire extends along X → text reads along X (textAlongX = true)
    //   wire extends along Y → text reads along Y (textAlongX = false)
    //   wire extends along Z → text reads along Y (fallback, textAlongX = false)

    bool needFrontBack = false;
    bool needTopBottom = false;
    bool needRightLeft = false;
    bool textAlongX = true;  // default: text reads along X

    // Determine by bbox longest dimension (glview style)
    // glview: text on faces PERPENDICULAR to wire direction, based on longest 2 dims
    if (ylen > xlen && ylen > zlen) {
        // Wire extends along Y (vertical wire)
        // Text on Z faces (front/back) + X faces (right/left)
        needFrontBack = true;
        needRightLeft = true;
        textAlongX = false;  // wire extends along Y, text reads along Y
    } else if (zlen > ylen && zlen > xlen) {
        // Wire extends along Z (depth wire)
        // Text on Y faces (top/bottom) + X faces (right/left)
        needTopBottom = true;
        needRightLeft = true;
        textAlongX = false;  // wire extends along Z, text reads along Y (fallback)
    } else {
        // xlen-longest: wire extends along X (horizontal wire)
        // Text on Y faces (top/bottom) + Z faces (front/back)
        needTopBottom = true;
        needFrontBack = true;
        textAlongX = true;  // wire extends along X, text reads along X
    }

    // Stipple quads (only when stipple is enabled and layer info is available)
    float margin = 0.0f;
    osg::Vec4 stippleColor(1,1,1,1);
    std::string stippleId;
    if (stippleVisible_ && layer) {
        float minDim = std::min({xlen, ylen, zlen});
        margin = std::max(minDim * 0.08f, 0.03f);
        stippleColor = osg::Vec4(layer->color.r, layer->color.g, layer->color.b, 1.0f);
        stippleId = layer->stippleId;
    }

    // Apply text + stipple quad to each selected face.
    // Stipple uses fixed S→X, T→Y (chip XY), independent of face rotation.
    if (needFrontBack) {
        float areaZ = xlen * ylen;
        if (areaZ > minFaceArea) {
            auto* sf = makeStippleFaceQuad(centerX, centerY, frontZ,
                                           ylen*0.5f - margin, xlen*0.5f - margin,
                                           1, 0, stippleColor, stippleId);
            if (sf) stippleGroup->addChild(sf);
            osg::Quat frRot = computeFaceRotation(LabelFace::Front, textAlongX, xlen, ylen, zlen);
            auto* lf = makeTextGeodeForFace(obj.displayName, centerX, centerY, frontZ, charSize, textColor, frRot);
            if (lf) labelGroup->addChild(lf);

            auto* sb = makeStippleFaceQuad(centerX, centerY, backZ,
                                           ylen*0.5f - margin, xlen*0.5f - margin,
                                           1, 0, stippleColor, stippleId);
            if (sb) stippleGroup->addChild(sb);
            osg::Quat bkRot = computeFaceRotation(LabelFace::Back, textAlongX, xlen, ylen, zlen);
            auto* lbk = makeTextGeodeForFace(obj.displayName, centerX, centerY, backZ, charSize, textColor, bkRot);
            if (lbk) labelGroup->addChild(lbk);
        }
    }

    if (needTopBottom) {
        float areaY = xlen * zlen;
        if (areaY > minFaceArea) {
            auto* st = makeStippleFaceQuad(centerX, topY, zCenterLocal,
                                           xlen*0.5f - margin, zlen*0.5f - margin,
                                           0, 2, stippleColor, stippleId);
            if (st) stippleGroup->addChild(st);
            osg::Quat topRot = computeFaceRotation(LabelFace::Top, textAlongX, xlen, ylen, zlen);
            auto* lt = makeTextGeodeForFace(obj.displayName, centerX, topY, zCenterLocal, charSize, textColor, topRot);
            if (lt) labelGroup->addChild(lt);

            auto* sbot = makeStippleFaceQuad(centerX, bottomY, zCenterLocal,
                                             xlen*0.5f - margin, zlen*0.5f - margin,
                                             0, 2, stippleColor, stippleId);
            if (sbot) stippleGroup->addChild(sbot);
            osg::Quat botRot = computeFaceRotation(LabelFace::Bottom, textAlongX, xlen, ylen, zlen);
            auto* lbot = makeTextGeodeForFace(obj.displayName, centerX, bottomY, zCenterLocal, charSize, textColor, botRot);
            if (lbot) labelGroup->addChild(lbot);
        }
    }

    if (needRightLeft) {
        float areaX = ylen * zlen;
        if (areaX > minFaceArea) {
            auto* sr = makeStippleFaceQuad(rightX, centerY, zCenterLocal,
                                           ylen*0.5f - margin, zlen*0.5f - margin,
                                           1, 2, stippleColor, stippleId);
            if (sr) stippleGroup->addChild(sr);
            osg::Quat rRot = computeFaceRotation(LabelFace::Right, textAlongX, xlen, ylen, zlen);
            auto* lr = makeTextGeodeForFace(obj.displayName, rightX, centerY, zCenterLocal, charSize, textColor, rRot);
            if (lr) labelGroup->addChild(lr);

            auto* sl = makeStippleFaceQuad(leftX, centerY, zCenterLocal,
                                           ylen*0.5f - margin, zlen*0.5f - margin,
                                           1, 2, stippleColor, stippleId);
            if (sl) stippleGroup->addChild(sl);
            osg::Quat lRot = computeFaceRotation(LabelFace::Left, textAlongX, xlen, ylen, zlen);
            auto* ll = makeTextGeodeForFace(obj.displayName, leftX, centerY, zCenterLocal, charSize, textColor, lRot);
            if (ll) labelGroup->addChild(ll);
        }
    }
    
    // Wrap labels in LOD so they only appear within a distance proportional to
    // the object's longest dimension. Small objects disappear sooner, large
    // objects (instances) keep labels visible from farther away.
    float lodDistance = maxDim * 20.0F;
    lodDistance = std::max(5.0F, lodDistance);

    osg::LOD* lod = new osg::LOD();
    lod->setName(obj.objectId + ":labels");
    lod->setCenter(osg::Vec3(centerX, centerY, centerZ));
    lod->addChild(labelGroup, 0.0F, lodDistance);

    transform->addChild(lod);
    textLabelNodes_[obj.objectId] = lod;

    // Stipple quads in a separate LOD with shorter range (0.6× text LOD).
    float stippleDistance = lodDistance * 0.6f;
    osg::LOD* stippleLod = new osg::LOD();
    stippleLod->setName(obj.objectId + ":stipple");
    stippleLod->setCenter(osg::Vec3(centerX, centerY, centerZ));
    stippleLod->addChild(stippleGroup, 0.0F, stippleDistance);
    // Set initial visibility based on stippleVisible_ flag
    stippleLod->setNodeMask(stippleVisible_ ? 0xFFFFFFFF : 0);
    transform->addChild(stippleLod);
    stippleNodes_[obj.objectId] = stippleLod;
}

osg::Quat OsgScene::computeFaceRotation(LabelFace face,
                                         bool textAlongX,
                                         float xlen,
                                         float ylen,
                                         float zlen) const {
    
    // Text geometry is in the XY plane at the origin, facing +Z.
    // glview rotation for each face (from addTextOnObject analysis):
    //   Right: R_x(90°) * R_z(90°) — text reads along Y, faces +X (always)
    //   Left: R_x(-90°) * R_z(-90°) — text reads along Y, faces -X (always)
    //   Top: R_x(90°) — text reads along Y, faces -Y (toward camera when viewed from above)
    //   Bottom: R_x(90°) * R_z(90°) — text reads along Y, faces -Y (always)
    //   Front: depends on wire direction
    //     ylen-longest (wire along Y): R_z(90°)
    //     zlen-longest (wire along Z): R_y(90°)
    //     xlen-longest (wire along X): identity
    //   Back: depends on wire direction
    //     ylen-longest: R_z(-90°) * R_y(180°)
    //     zlen-longest: R_y(-90°) * R_y(180°)
    //     xlen-longest: R_y(180°)

    osg::Quat rotation;
    switch (face) {
        case LabelFace::Front:
            // For wires along Y or Z (textAlongX=false), text reads along Y on Z faces
            // For wires along X (textAlongX=true), text reads along X on Z faces
            if (textAlongX) {
                // xlen-longest: wire extends along X, text reads along X on Front face
                rotation = osg::Quat(0, osg::Vec3(0,0,1));  // identity
            } else if (ylen > xlen && ylen > zlen) {
                // ylen-longest: wire extends along Y, text reads +Y on Front face.
                // R_z(-90°) orients reading to +Y, then R_y(180°) flips normal +Z→-Z.
                // In OSG: q_y180 * q_z-90 applies q_z-90 first, then q_y180.
                rotation = osg::Quat(osg::PI, osg::Vec3(0,1,0))
                         * osg::Quat(-osg::PI_2, osg::Vec3(0,0,1));
            } else {
                // zlen-longest: wire extends along Z, text reads along Y on Front face
                rotation = osg::Quat(osg::PI_2, osg::Vec3(0,1,0));  // R_y(90°)
            }
            break;
        case LabelFace::Back:
            if (textAlongX) {
                // xlen-longest: text reads along -X on Back face
                rotation = osg::Quat(osg::PI, osg::Vec3(0,1,0));  // R_y(180°)
            } else if (ylen > xlen && ylen > zlen) {
                // ylen-longest: text reads outward from Back face (normal +Z).
                // R_z(-90°) gives read=-Y (downward), up=+X (characters face right).
                rotation = osg::Quat(-osg::PI_2, osg::Vec3(0,0,1));
            } else {
                // zlen-longest: R_y(-90°) * R_y(180°)
                rotation = osg::Quat(-osg::PI_2, osg::Vec3(0,1,0))
                         * osg::Quat(osg::PI, osg::Vec3(0,1,0));
            }
            break;
        case LabelFace::Top:
            // R_x(90°) — text faces +Y (outward from top face)
            rotation = osg::Quat(osg::PI_2, osg::Vec3(1,0,0));
            break;
        case LabelFace::Bottom:
            // R_x(90°) — text faces -Y (outward from bottom), "up" = +Z
            rotation = osg::Quat(osg::PI_2, osg::Vec3(1,0,0));
            break;
        case LabelFace::Right:
            if (!textAlongX) {
                // vertical wire: 120° about (1,1,1): normal +X, read +Y.
                osg::Vec3d axis(1.0 / sqrt(3.0), 1.0 / sqrt(3.0), 1.0 / sqrt(3.0));
                rotation = osg::Quat(osg::PI * 2.0 / 3.0, axis);
            } else {
                // horizontal wire: R_y(-90°): normal +X, read -Z.
                rotation = osg::Quat(-osg::PI_2, osg::Vec3(0,1,0));
            }
            break;
        case LabelFace::Left:
            if (!textAlongX) {
                // vertical wire: 120° about (1,-1,-1): normal -X, read -Y, up +Z.
                osg::Vec3d axis(1.0 / sqrt(3.0), -1.0 / sqrt(3.0), -1.0 / sqrt(3.0));
                rotation = osg::Quat(osg::PI * 2.0 / 3.0, axis);
            } else {
                // horizontal wire: R_y(90°): normal -X, read +Z.
                rotation = osg::Quat(osg::PI_2, osg::Vec3(0,1,0));
            }
            break;
    }

    
    return rotation;
}

osg::Node* OsgScene::makeStippleFaceQuad(float cx, float cy, float cz,
                                          float halfW, float halfH,
                                          int dim1, int dim2,
                                          const osg::Vec4& color,
                                          const std::string& stippleId) {
    if (stippleId.empty() || stippleId == "none" || stippleId == "solid" ||
        halfW <= 0.0f || halfH <= 0.0f) {
        return nullptr;
    }

    // Build 4 vertices in the plane spanned by dim1, dim2 at the face center.
    osg::Vec3f v[4];
    for (int i = 0; i < 4; ++i) {
        v[i] = osg::Vec3f(cx, cy, cz);
        float s1 = (i == 0 || i == 3) ? -halfW : halfW;
        float s2 = (i == 0 || i == 1) ? -halfH : halfH;
        if (dim1 == 0) v[i].x() += s1; else if (dim1 == 1) v[i].y() += s1; else v[i].z() += s1;
        if (dim2 == 0) v[i].x() += s2; else if (dim2 == 1) v[i].y() += s2; else v[i].z() += s2;
    }
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array(4);
    for (int i = 0; i < 4; ++i) (*verts)[i] = v[i];

    // One quad = 2 triangles
    osg::ref_ptr<osg::DrawElementsUInt> tris = new osg::DrawElementsUInt(GL_TRIANGLES, 6);
    (*tris)[0] = 0; (*tris)[1] = 1; (*tris)[2] = 2;
    (*tris)[3] = 0; (*tris)[4] = 2; (*tris)[5] = 3;

    osg::Vec4Array* colors = new osg::Vec4Array(1);
    (*colors)[0] = color;

    osg::Geometry* geom = new osg::Geometry();
    geom->setVertexArray(verts);
    geom->setColorArray(colors, osg::Array::BIND_OVERALL);
    geom->addPrimitiveSet(tris);

    osg::Geode* geode = new osg::Geode();
    geode->addDrawable(geom);

    // Stateset: no lighting, stipple texture via TexGen
    osg::StateSet* ss = geode->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    ss->setMode(GL_BLEND, osg::StateAttribute::OFF);

    osg::Depth* depth = new osg::Depth();
    depth->setWriteMask(false);
    depth->setFunction(osg::Depth::LEQUAL);
    ss->setAttributeAndModes(depth, osg::StateAttribute::ON);

    // PolygonOffset pushes quad slightly toward camera to avoid z-fighting
    osg::PolygonOffset* polyOff = new osg::PolygonOffset(-1.0f, -1.0f);
    ss->setAttributeAndModes(polyOff, osg::StateAttribute::ON);

    // Stipple texture on unit 1 with TexGen (planes based on face orientation)
    osg::Texture2D* tex = getOrCreateStippleTexture(stippleId);
    if (tex) {
        ss->setTextureAttributeAndModes(1, tex, osg::StateAttribute::ON);
        osg::TexEnv* texEnv = new osg::TexEnv(osg::TexEnv::MODULATE);
        ss->setTextureAttribute(1, texEnv);
        // Face-coordinate TexGen: S→dim1, T→dim2. Each face tiles in its
        // own plane — Front/Back (XY) get S→X/T→Y, Top/Bottom (XZ) get
        // S→X/T→Z, Right/Left (YZ) get S→Y/T→Z.
        static constexpr float FREQ = 12.0f;
        osg::Plane sPlane(0,0,0,0), tPlane(0,0,0,0);
        if (dim1 == 0) sPlane = osg::Plane(FREQ, 0, 0, 0);
        else if (dim1 == 1) sPlane = osg::Plane(0, FREQ, 0, 0);
        else sPlane = osg::Plane(0, 0, FREQ, 0);
        if (dim2 == 0) tPlane = osg::Plane(FREQ, 0, 0, 0);
        else if (dim2 == 1) tPlane = osg::Plane(0, FREQ, 0, 0);
        else tPlane = osg::Plane(0, 0, FREQ, 0);
        osg::TexGen* texGen = new osg::TexGen();
        texGen->setMode(osg::TexGen::OBJECT_LINEAR);
        texGen->setPlane(osg::TexGen::S, sPlane);
        texGen->setPlane(osg::TexGen::T, tPlane);
        ss->setTextureAttributeAndModes(1, texGen, osg::StateAttribute::ON);
    }

    return geode;
}

osg::Node* OsgScene::makeTextGeodeForFace(const std::string& text,
                                           float centerX,
                                           float centerY,
                                           float centerZ,
                                           float charSize,
                                           const osg::Vec4& color,
                                           const osg::Quat& rotation) {
    if (text.empty() || !fontAtlas_ || !fontAtlas_->isValid()) {
        return nullptr;
    }

    float scale = charSize / static_cast<float>(OsgFontAtlas::FONT_SIZE);

    OsgTextGeode* textGeode = new OsgTextGeode(text, fontAtlas_.get(), color, scale);
    textGeode->setName("label:" + text);

    osg::MatrixTransform* mt = new osg::MatrixTransform();
    mt->setName("labelXform:" + text);
    // OSG uses row-major convention (v * M, NOT M * v).
    // With v * (T * R) = (v + T) * R, the translation T is rotated by R,
    // so the text center (=origin) goes to T*R, not T. This causes text to
    // appear at wrong positions when rotation is not identity.
    // With v * (R * T) = (v * R) + T, text is first rotated, then shifted
    // by T. The text center (=origin) goes to T. This is correct.
    // Scale is already baked into text vertices via OsgTextGeode(scale)
    osg::Matrixd mat = osg::Matrixd::rotate(rotation)
                     * osg::Matrixd::translate(centerX, centerY, centerZ);
    mt->setMatrix(mat);
    mt->addChild(textGeode);


    return mt;
}

void OsgScene::removeObjectLabels(const std::string& objectId) {
    auto it = textLabelNodes_.find(objectId);
    if (it != textLabelNodes_.end()) {
        osg::Group* labelGroup = it->second.get();
        if (labelGroup) {
            osg::Group* parent = labelGroup->getParent(0);
            if (parent) {
                parent->removeChild(labelGroup);
            }
        }
        textLabelNodes_.erase(it);
    }

    // Also remove stipple LOD if present
    auto stippleIt = stippleNodes_.find(objectId);
    if (stippleIt != stippleNodes_.end()) {
        osg::LOD* stippleLod = stippleIt->second.get();
        if (stippleLod) {
            osg::Group* parent = stippleLod->getParent(0);
            if (parent) {
                parent->removeChild(stippleLod);
            }
        }
        stippleNodes_.erase(stippleIt);
    }
}

void OsgScene::attachObjectGeometry(osg::Geode* geode,
                                    const domain::ObjectRecord& obj,
                                    const domain::LayerRecord* layer) {
    if (!geode) {
        return;
    }

    auto addLayerColoredDrawable = [&](osg::Geometry* geom, float alphaScale = 1.0F) {
        if (!geom) {
            return;
        }
        if (layer) {
            applyFlatColor(geom,
                           osg::Vec4(layer->color.r,
                                     layer->color.g,
                                     layer->color.b,
                                     (1.0F - layer->transparency) * alphaScale));
        }
        geode->addDrawable(geom);
    };

    switch (obj.type) {
        case domain::ObjectType::Inst: {
            const LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.3F);
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            if (geom) {
                if (layer) {
                    const float alpha = std::max(0.15F, 1.0F - std::max(layer->transparency, 0.55F));
                    applyFlatColor(geom, osg::Vec4(layer->color.r, layer->color.g, layer->color.b, alpha));
                }
                geode->addDrawable(geom);
            }
            break;
        }
        case domain::ObjectType::Blockage: {
            // Blockage: extruded polygon if geometryRef has poly data, else box
            auto pts = parsePolylinePoints(obj.geometryRef);
            if (pts.size() >= 3) {
                float thickness = obj.hasBbox
                    ? std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2])
                    : 0.3f;
                float cx = obj.transform[12], cy = obj.transform[13];
                for (auto& p : pts) { p.x() -= cx; p.y() -= cy; }
                osg::Geometry* geom = createPolygonGeometry(pts, thickness);
                addLayerColoredDrawable(geom);
            } else {
                LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.3F);
                osg::Geometry* geom = createBoxGeometry(
                    box.xOffset, box.yOffset, box.width, box.height, box.depth);
                addLayerColoredDrawable(geom);
            }
            break;
        }
        case domain::ObjectType::Pin:
        case domain::ObjectType::Bump: {
            LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.3F);
            if (!obj.hasBbox && layer) {
                box.depth = std::max(layer->lineWidth * 0.5F, 0.05F);
            }
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            addLayerColoredDrawable(geom);
            break;
        }
        case domain::ObjectType::Wire: {
            float xOffset = 0.0F;
            float yOffset = 0.0F;
            if (obj.hasBbox) {
                const float centerX = obj.transform[12];
                const float centerY = obj.transform[13];
                xOffset = obj.bboxMin[0] - centerX;
                yOffset = obj.bboxMin[1] - centerY;
            }
            osg::Geometry* geom = createPolylineGeometry(obj, layer, xOffset, yOffset);
            addLayerColoredDrawable(geom);
            break;
        }
        case domain::ObjectType::Via: {
            const LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.05F);
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            addLayerColoredDrawable(geom);
            break;
        }
        case domain::ObjectType::Drc: {
            // DRC markers: semi-transparent red boxes with wireframe
            LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.05F);
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            if (geom) {
                applyFlatColor(geom, osg::Vec4(1.0F, 0.2F, 0.2F, 0.6F));
                geode->addDrawable(geom);
            }
            break;
        }
        case domain::ObjectType::Track: {
            // Track: render each pair of points as a separate line segment.
            // Parse geometryRef pairs separated by ';' (e.g. "poly:fx1,0;fx1,10000;fx2,0;fx2,10000").
            auto pts = parsePolylinePoints(obj.geometryRef);
            float cx = obj.transform[12], cy = obj.transform[13];
            float z = obj.hasBbox ? (obj.bboxMin[2] + obj.bboxMax[2]) * 0.5f - obj.transform[14] : 0;
            osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
            // Connect points in pairs: (0,1), (2,3), (4,5), ... as line segments
            for (size_t i = 0; i + 1 < pts.size(); i += 2) {
                verts->push_back(osg::Vec3(pts[i].x() - cx, pts[i].y() - cy, z));
                verts->push_back(osg::Vec3(pts[i+1].x() - cx, pts[i+1].y() - cy, z));
            }
            if (!verts->empty()) {
                osg::Geometry* geom = new osg::Geometry();
                geom->setVertexArray(verts);
                geom->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, verts->size()));
                // Apply color with dimmed brightness for track lines
                if (layer) {
                    osg::Vec4 trackColor(layer->color.r*0.6f, layer->color.g*0.6f,
                                         layer->color.b*0.6f, 1.0f);
                    applyFlatColor(geom, trackColor);
                }
                geode->addDrawable(geom);
            }
            break;
        }
        default: {
            const LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.2F, 0.01F, false);
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            addLayerColoredDrawable(geom);
            break;
        }
    }
}

osg::StateSet* OsgScene::getOrCreateLayerStateSet(const domain::LayerRecord& layer,
                                                    domain::ObjectType objectType) {
    // Create cache key from layerId and objectType
    std::string key = layer.layerId + "_" + std::to_string(static_cast<int>(objectType));

    auto it = layerStateSetCache_.find(key);
    if (it != layerStateSetCache_.end()) {
        return it->second.get();
    }

    // Create new StateSet
    osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet();
    stateset->setMode(GL_LIGHTING, osg::StateAttribute::ON);

    osg::Material* material = new osg::Material();
    material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
    const float baseR = std::clamp(layer.color.r, 0.0F, 1.0F);
    const float baseG = std::clamp(layer.color.g, 0.0F, 1.0F);
    const float baseB = std::clamp(layer.color.b, 0.0F, 1.0F);
    const float legoR = std::clamp(baseR * 1.08F, 0.0F, 1.0F);
    const float legoG = std::clamp(baseG * 1.08F, 0.0F, 1.0F);
    const float legoB = std::clamp(baseB * 1.08F, 0.0F, 1.0F);
    const float effectiveTransparency = (objectType == domain::ObjectType::Inst)
                                           ? std::max(layer.transparency, 0.55F)
                                           : layer.transparency;
    material->setAmbient(osg::Material::FRONT_AND_BACK,
                         osg::Vec4(legoR * 0.22F,
                                   legoG * 0.22F,
                                   legoB * 0.22F,
                                   1.0F - effectiveTransparency));
    material->setDiffuse(osg::Material::FRONT_AND_BACK,
                         osg::Vec4(legoR,
                                   legoG,
                                   legoB,
                                   1.0F - effectiveTransparency));
    material->setSpecular(osg::Material::FRONT_AND_BACK,
                          osg::Vec4(0.18F, 0.18F, 0.18F, 1.0F));
    material->setShininess(osg::Material::FRONT_AND_BACK, 36.0F);
    stateset->setAttributeAndModes(material, osg::StateAttribute::ON);

    osg::ShadeModel* shade = new osg::ShadeModel(osg::ShadeModel::SMOOTH);
    stateset->setAttributeAndModes(shade, osg::StateAttribute::ON);

    osg::LineWidth* lw = new osg::LineWidth(layer.lineWidth);
    stateset->setAttributeAndModes(lw, osg::StateAttribute::ON);

    osg::PolygonMode* pm
        = new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::FILL);
    stateset->setAttributeAndModes(pm, osg::StateAttribute::ON);

    applyStipple(stateset.get(), layer.lineStyle, layer.stippleId);

    if (effectiveTransparency > 0.0F) {
        stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
        stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    }

    stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

    // Ensure depth write is ON for correct layer occlusion.
    // OSG's TRANSPARENT_BIN defaults to depth write OFF, which makes
    // transparent instances render on top of opaque metal layers even
    // when they're at lower Z. Explicitly enable depth write.
    osg::Depth* depth = new osg::Depth();
    depth->setWriteMask(true);
    depth->setFunction(osg::Depth::LESS);
    stateset->setAttributeAndModes(depth, osg::StateAttribute::ON);

    layerStateSetCache_[key] = stateset;
    return stateset.get();
}

void OsgScene::configureGeodeRenderState(osg::Geode* geode,
                                         const domain::LayerRecord& layer,
                                         domain::ObjectType objectType) {
    if (!geode) {
        return;
    }
    // Use cached StateSet instead of creating new one per object
    geode->setStateSet(getOrCreateLayerStateSet(layer, objectType));
}

// English comment.

osg::Geometry* OsgScene::createBoxGeometry(float x, float y, float width, float height, float depth) {
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();
    const float z0 = 0.0F;
    const float x1 = x + width;
    const float y1 = y + height;
    const float z1 = depth;

    const osg::Vec3 p000(x, y, z0);
    const osg::Vec3 p100(x1, y, z0);
    const osg::Vec3 p110(x1, y1, z0);
    const osg::Vec3 p010(x, y1, z0);
    const osg::Vec3 p001(x, y, z1);
    const osg::Vec3 p101(x1, y, z1);
    const osg::Vec3 p111(x1, y1, z1);
    const osg::Vec3 p011(x, y1, z1);

    auto addFace = [&](const osg::Vec3& a,
                       const osg::Vec3& b,
                       const osg::Vec3& c,
                       const osg::Vec3& d,
                       const osg::Vec3& normal) {
        verts->push_back(a);
        verts->push_back(b);
        verts->push_back(c);
        verts->push_back(d);
        normals->push_back(normal);
        normals->push_back(normal);
        normals->push_back(normal);
        normals->push_back(normal);
    };

    addFace(p000, p100, p110, p010, osg::Vec3(0.0F, 0.0F, -1.0F));
    addFace(p001, p011, p111, p101, osg::Vec3(0.0F, 0.0F, 1.0F));
    addFace(p000, p001, p101, p100, osg::Vec3(0.0F, -1.0F, 0.0F));
    addFace(p100, p101, p111, p110, osg::Vec3(1.0F, 0.0F, 0.0F));
    addFace(p110, p111, p011, p010, osg::Vec3(0.0F, 1.0F, 0.0F));
    addFace(p010, p011, p001, p000, osg::Vec3(-1.0F, 0.0F, 0.0F));

    osg::ref_ptr<osg::DrawElementsUInt> tris = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (unsigned int face = 0; face < 6; ++face) {
        const unsigned int base = face * 4;
        tris->push_back(base + 0);
        tris->push_back(base + 1);
        tris->push_back(base + 2);
        tris->push_back(base + 0);
        tris->push_back(base + 2);
        tris->push_back(base + 3);
    }

    osg::Geometry* geom = new osg::Geometry();
    geom->setVertexArray(verts);
    geom->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
    geom->addPrimitiveSet(tris);
    return geom;
}

osg::Geometry* OsgScene::createPolylineGeometry(const domain::ObjectRecord& obj, const domain::LayerRecord* layer, float xOffset, float yOffset) {
    // If no layer, wire should not be displayed
    if (!layer) {
        return nullptr;
    }
    
    const float lineWidth = layer->lineWidth;
    const float thickness = obj.hasBbox
        ? std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2])
        : std::max(0.01F, lineWidth * 0.5F);  // use bbox Z thickness when available
    const float halfWidth = lineWidth * 0.5F;  // width = half of lineWidth (same as thickness)

    auto points = parsePolylinePoints(obj.geometryRef);
    if (points.size() < 2) {
        if (!obj.hasBbox) {
            return nullptr;
        }
        // Transform is center, so offset geometry to match bboxMin
        return createBoxGeometry(xOffset, yOffset,
                                 std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]),
                                 std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]),
                                 thickness);
    }

    // Use xOffset/yOffset as local origin instead of transform center
    const float originX = xOffset;
    const float originY = yOffset;
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::DrawElementsUInt> tris = new osg::DrawElementsUInt(GL_TRIANGLES);

    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        osg::Vec2 start(points[index].x() - originX, points[index].y() - originY);
        osg::Vec2 end(points[index + 1].x() - originX, points[index + 1].y() - originY);
        osg::Vec2 direction = end - start;
        const float length = direction.length();
        if (length <= 1e-4F) {
            continue;
        }
        direction /= length;
        const osg::Vec2 normal(-direction.y(), direction.x());
        const osg::Vec2 offset = normal * halfWidth;

        const unsigned int base = verts->size();
        verts->push_back(osg::Vec3(start.x() - offset.x(), start.y() - offset.y(), 0.0F));
        verts->push_back(osg::Vec3(start.x() + offset.x(), start.y() + offset.y(), 0.0F));
        verts->push_back(osg::Vec3(end.x() + offset.x(), end.y() + offset.y(), 0.0F));
        verts->push_back(osg::Vec3(end.x() - offset.x(), end.y() - offset.y(), 0.0F));
        verts->push_back(osg::Vec3(start.x() - offset.x(), start.y() - offset.y(), thickness));
        verts->push_back(osg::Vec3(start.x() + offset.x(), start.y() + offset.y(), thickness));
        verts->push_back(osg::Vec3(end.x() + offset.x(), end.y() + offset.y(), thickness));
        verts->push_back(osg::Vec3(end.x() - offset.x(), end.y() - offset.y(), thickness));

        const unsigned int segmentIndices[] = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0
        };
        for (unsigned int segmentIndex : segmentIndices) {
            tris->push_back(base + segmentIndex);
        }
    }

    if (verts->empty()) {
        return nullptr;
    }

    osg::Geometry* geom = new osg::Geometry();
    geom->setVertexArray(verts);
    geom->addPrimitiveSet(tris);
    osgUtil::SmoothingVisitor::smooth(*geom);
    return geom;
}

osg::Geometry* OsgScene::createPolygonGeometry(const std::vector<osg::Vec2>& points, float thickness) {
    if (points.size() < 3) return nullptr;
    // Triangulate the polygon and extrude to `thickness` in Z
    auto tris = triangulatePolygon(points);
    if (tris.empty()) return nullptr;

    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();

    for (size_t i = 0; i < tris.size(); i += 6) {
        // Bottom face
        verts->push_back(osg::Vec3(tris[i], tris[i+1], 0));
        verts->push_back(osg::Vec3(tris[i+2], tris[i+3], 0));
        verts->push_back(osg::Vec3(tris[i+4], tris[i+5], 0));
        // Top face (same XY, elevated to thickness)
        verts->push_back(osg::Vec3(tris[i], tris[i+1], thickness));
        verts->push_back(osg::Vec3(tris[i+2], tris[i+3], thickness));
        verts->push_back(osg::Vec3(tris[i+4], tris[i+5], thickness));
        // Normals (down for bottom, up for top)
        normals->push_back(osg::Vec3(0,0,-1));
        normals->push_back(osg::Vec3(0,0,-1));
        normals->push_back(osg::Vec3(0,0,-1));
        normals->push_back(osg::Vec3(0,0,1));
        normals->push_back(osg::Vec3(0,0,1));
        normals->push_back(osg::Vec3(0,0,1));
    }

    // Add side faces (extrude each polygon edge)
    for (size_t i = 0; i < points.size(); ++i) {
        const osg::Vec2& a = points[i];
        const osg::Vec2& b = points[(i+1) % points.size()];
        osg::Vec3 edge(a.y()-b.y(), b.x()-a.x(), 0);  // outward normal (in XY)
        edge.normalize();
        verts->push_back(osg::Vec3(a.x(), a.y(), 0));
        verts->push_back(osg::Vec3(b.x(), b.y(), 0));
        verts->push_back(osg::Vec3(a.x(), a.y(), thickness));
        verts->push_back(osg::Vec3(b.x(), b.y(), 0));
        verts->push_back(osg::Vec3(b.x(), b.y(), thickness));
        verts->push_back(osg::Vec3(a.x(), a.y(), thickness));
        for (int j = 0; j < 6; ++j) normals->push_back(edge);
    }

    osg::ref_ptr<osg::DrawElementsUInt> trisElem = new osg::DrawElementsUInt(GL_TRIANGLES);
    trisElem->reserve(verts->size());
    for (unsigned int i = 0; i < verts->size(); ++i) trisElem->push_back(i);

    osg::Geometry* geom = new osg::Geometry();
    geom->setVertexArray(verts);
    geom->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
    geom->addPrimitiveSet(trisElem);
    return geom;
}

osg::Geometry* OsgScene::createPointGeometry(float size) {
    const float actualSize = std::max(size, 0.6F);
    return createBoxGeometry(-actualSize * 0.5F,
                             -actualSize * 0.5F,
                             actualSize,
                             actualSize,
                             actualSize);
}

osg::Geometry* OsgScene::createSelectionGeometry(osg::Node* node) {
    // English comment.
    osg::Matrixd worldMatrix;
    osg::MatrixTransform* transform = dynamic_cast<osg::MatrixTransform*>(node);
    if (transform) {
        worldMatrix = transform->getMatrix();
    }
    
    // English comment.
    osg::BoundingBox worldAABB;
    
    // English comment.
    osg::Group* group = node->asGroup();
    if (group) {
        for (unsigned int i = 0; i < group->getNumChildren(); ++i) {
            osg::Node* child = group->getChild(i);
            osg::Geode* geode = child->asGeode();
            if (geode) {
                // English comment.
                for (unsigned int j = 0; j < geode->getNumDrawables(); ++j) {
                    osg::Drawable* drawable = geode->getDrawable(j);
                    if (drawable) {
                        // English comment.
                        osg::BoundingBox localBB = drawable->getBoundingBox();
                        if (localBB.xMin() < localBB.xMax()) {
                            // English comment.
                            osg::Vec3d corners[8] = {
                                osg::Vec3d(localBB.xMin(), localBB.yMin(), localBB.zMin()),
                                osg::Vec3d(localBB.xMax(), localBB.yMin(), localBB.zMin()),
                                osg::Vec3d(localBB.xMin(), localBB.yMax(), localBB.zMin()),
                                osg::Vec3d(localBB.xMax(), localBB.yMax(), localBB.zMin()),
                                osg::Vec3d(localBB.xMin(), localBB.yMin(), localBB.zMax()),
                                osg::Vec3d(localBB.xMax(), localBB.yMin(), localBB.zMax()),
                                osg::Vec3d(localBB.xMin(), localBB.yMax(), localBB.zMax()),
                                osg::Vec3d(localBB.xMax(), localBB.yMax(), localBB.zMax())
                            };
                            for (int k = 0; k < 8; ++k) {
                                osg::Vec3d worldCorner = corners[k] * worldMatrix;
                                worldAABB.expandBy(worldCorner);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // English comment.
    if (worldAABB.xMin() >= worldAABB.xMax()) {
        osg::BoundingSphere bs = node->getBound();
        worldAABB = osg::BoundingBox(
            bs.center() - osg::Vec3(bs.radius(), bs.radius(), bs.radius()),
            bs.center() + osg::Vec3(bs.radius(), bs.radius(), bs.radius()));
    }
    
    osg::Geometry* geom = new osg::Geometry();
    
    // English comment.
    osg::Vec3Array* vertices = new osg::Vec3Array();
    float x = worldAABB.xMin(), y = worldAABB.yMin(), z = worldAABB.zMin();
    float X = worldAABB.xMax(), Y = worldAABB.yMax(), Z = worldAABB.zMax();
    
    // English comment.
    vertices->push_back(osg::Vec3(x,y,z)); vertices->push_back(osg::Vec3(X,y,z));
    vertices->push_back(osg::Vec3(X,y,z)); vertices->push_back(osg::Vec3(X,Y,z));
    vertices->push_back(osg::Vec3(X,Y,z)); vertices->push_back(osg::Vec3(x,Y,z));
    vertices->push_back(osg::Vec3(x,Y,z)); vertices->push_back(osg::Vec3(x,y,z));
    // English comment.
    vertices->push_back(osg::Vec3(x,y,Z)); vertices->push_back(osg::Vec3(X,y,Z));
    vertices->push_back(osg::Vec3(X,y,Z)); vertices->push_back(osg::Vec3(X,Y,Z));
    vertices->push_back(osg::Vec3(X,Y,Z)); vertices->push_back(osg::Vec3(x,Y,Z));
    vertices->push_back(osg::Vec3(x,Y,Z)); vertices->push_back(osg::Vec3(x,y,Z));
    // English comment.
    vertices->push_back(osg::Vec3(x,y,z)); vertices->push_back(osg::Vec3(x,y,Z));
    vertices->push_back(osg::Vec3(X,y,z)); vertices->push_back(osg::Vec3(X,y,Z));
    vertices->push_back(osg::Vec3(X,Y,z)); vertices->push_back(osg::Vec3(X,Y,Z));
    vertices->push_back(osg::Vec3(x,Y,z)); vertices->push_back(osg::Vec3(x,Y,Z));
    
    geom->setVertexArray(vertices);
    geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, vertices->size()));
    
    osg::Vec4Array* colors = new osg::Vec4Array();
    colors->push_back(osg::Vec4(1, 1, 0.5F, 1));  // English comment.
    geom->setColorArray(colors, osg::Array::BIND_PER_PRIMITIVE_SET);
    
    // English comment.
    osg::StateSet* stateset = geom->getOrCreateStateSet();
    stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    stateset->setAttribute(new osg::LineWidth(2.0f), osg::StateAttribute::ON);
    
    return geom;
}

// English comment.

IGeometry* OsgScene::createCube(float width, float height, float depth) {
    return new OsgGeometry(GeometryType::Cube, width, height, depth);
}

IGeometry* OsgScene::createBox(float x, float y, float width, float height, float depth) {
    GeometryParams params;
    params.type = GeometryType::Box;
    params.position = {x, y, 0};
    params.size = {width, height, depth};
    return createFromData(params);
}

IGeometry* OsgScene::createPolygon(const std::vector<Vec2>& vertices, float height) {
    GeometryParams params;
    params.type = GeometryType::Polygon;
    params.polygonVertices = vertices;
    params.height = height;
    return createFromData(params);
}

IGeometry* OsgScene::createPolyline(const std::vector<Vec3>& points, float width) {
    GeometryParams params;
    params.type = GeometryType::Polyline;
    params.vertices = points;
    params.size = {width, width, width};
    return createFromData(params);
}

IGeometry* OsgScene::createPoint(float size) {
    return new OsgGeometry(GeometryType::Point, size, size, size);
}

IGeometry* OsgScene::createSphere(float radius, int segments) {
    return new OsgGeometry(GeometryType::Sphere, radius, radius, radius, segments);
}

IGeometry* OsgScene::createCylinder(float radius, float height, int segments) {
    return new OsgGeometry(GeometryType::Cylinder, radius, height, radius, segments);
}

IGeometry* OsgScene::createFromData(const GeometryParams& params) {
    return new OsgGeometry(params);
}

// English comment.

IMaterial* OsgScene::createMaterial() {
    return new OsgMaterial();
}

IMaterial* OsgScene::createMaterial(const MaterialDesc& desc) {
    return new OsgMaterial(desc);
}

IMaterial* OsgScene::createHighlightMaterial(float r, float g, float b) {
    MaterialDesc desc;
    desc.color = {r, g, b, 1.0F};
    desc.emissionR = r;
    desc.emissionG = g;
    desc.emissionB = b;
    return new OsgMaterial(desc);
}

IMaterial* OsgScene::createSelectionMaterial() {
    MaterialDesc desc;
    desc.color = {1.0F, 1.0F, 0.5F, 1.0F};
    desc.emissionR = 0.3F;
    desc.emissionG = 0.3F;
    desc.emissionB = 0.1F;
    return new OsgMaterial(desc);
}

IMaterial* OsgScene::createGlowMaterial(float intensity) {
    MaterialDesc desc;
    desc.color = {intensity, intensity, intensity * 0.8F, 1.0F};
    desc.emissionR = intensity * 0.5F;
    desc.emissionG = intensity * 0.5F;
    desc.emissionB = intensity * 0.4F;
    return new OsgMaterial(desc);
}

// English comment.

ILight* OsgScene::createLight(LightType type) {
    osg::Light* light = new osg::Light(lights_.size());
    light->setAmbient(osg::Vec4(0.2F, 0.2F, 0.2F, 1.0F));
    light->setDiffuse(osg::Vec4(0.8F, 0.8F, 0.8F, 1.0F));
    light->setSpecular(osg::Vec4(0.3F, 0.3F, 0.3F, 1.0F));
    lights_.push_back(light);
    OsgLight* osgLight = new OsgLight(light);
    osgLight->setType(type);
    return osgLight;
}

void OsgScene::destroyLight(ILight* light) {
    OsgLight* osgLight = dynamic_cast<OsgLight*>(light);
    if (!osgLight) {
        delete light;
        return;
    }

    osg::Light* target = osgLight->getOsgLight();
    if (target) {
        lights_.erase(std::remove_if(lights_.begin(), lights_.end(),
                                     [target](const osg::ref_ptr<osg::Light>& item) {
                                         return item.get() == target;
                                     }),
                      lights_.end());
        if (mainLight_.get() == target) {
            mainLight_ = nullptr;
        }
    }

    delete osgLight;
}

size_t OsgScene::getLightCount() const {
    return lights_.size();
}

void OsgScene::setMainLight(ILight* light) {
    OsgLight* osgLight = dynamic_cast<OsgLight*>(light);
    if (!osgLight) {
        mainLight_ = nullptr;
        return;
    }
    mainLight_ = osgLight->getOsgLight();
}

ILight* OsgScene::getMainLight() {
    return mainLight_ ? new OsgLight(mainLight_.get()) : nullptr;
}

void OsgScene::update() {
    osg::StateSet* stateset = objectRoot_ ? objectRoot_->getOrCreateStateSet() : nullptr;
    if (!stateset) {
        return;
    }

    stateset->setMode(GL_LIGHTING,
                      lights_.empty() ? osg::StateAttribute::OFF : osg::StateAttribute::ON);
    for (std::size_t i = 0; i < lights_.size(); ++i) {
        lights_[i]->setLightNum(static_cast<int>(i));
        stateset->setAttributeAndModes(lights_[i].get(), osg::StateAttribute::ON);
        stateset->setMode(GL_LIGHT0 + static_cast<GLenum>(i), osg::StateAttribute::ON);
    }
}

// English comment.

ITexture* OsgScene::loadTexture(const TextureLoadParams& params) {
    if (params.filePath.empty()) {
        return nullptr;
    }

    osg::ref_ptr<osg::Image> image = osgDB::readRefImageFile(params.filePath);
    if (!image) {
        return nullptr;
    }

    osg::Texture2D* texture = new osg::Texture2D();
    texture->setImage(image.get());
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MIN_FILTER,
                       params.generateMipmaps ? osg::Texture::LINEAR_MIPMAP_LINEAR
                                              : osg::Texture::LINEAR);
    texture->setMaxAnisotropy(static_cast<float>(std::max(1, params.Anisotropy)));

    return new OsgTexture(texture);
}

ITexture* OsgScene::createTexture(int width, int height, TextureFormat format, const uint8_t* data) {
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    GLenum pixelFormat = GL_RGBA;
    GLenum dataType = GL_UNSIGNED_BYTE;
    int bytesPerPixel = 4;
    switch (format) {
        case TextureFormat::RGB:
            pixelFormat = GL_RGB;
            dataType = GL_UNSIGNED_BYTE;
            bytesPerPixel = 3;
            break;
        case TextureFormat::RGBA:
            pixelFormat = GL_RGBA;
            dataType = GL_UNSIGNED_BYTE;
            bytesPerPixel = 4;
            break;
        case TextureFormat::Float:
            pixelFormat = GL_LUMINANCE;
            dataType = GL_FLOAT;
            bytesPerPixel = static_cast<int>(sizeof(float));
            break;
        case TextureFormat::Depth:
            pixelFormat = GL_DEPTH_COMPONENT;
            dataType = GL_UNSIGNED_INT;
            bytesPerPixel = static_cast<int>(sizeof(uint32_t));
            break;
    }

    osg::ref_ptr<osg::Image> image = new osg::Image();
    image->allocateImage(width, height, 1, pixelFormat, dataType);
    if (data) {
        const std::size_t totalBytes = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height) *
                                       static_cast<std::size_t>(bytesPerPixel);
        std::memcpy(image->data(), data, totalBytes);
    }

    osg::Texture2D* texture = new osg::Texture2D();
    texture->setImage(image.get());
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);

    return new OsgTexture(texture);
}

ITexture* OsgScene::createTexture(int width, int height, TextureFormat format) {
    return createTexture(width, height, format, nullptr);
}

void OsgScene::destroyTexture(ITexture* texture) {
    OsgTexture* osgTexture = dynamic_cast<OsgTexture*>(texture);
    if (osgTexture && defaultTexture_.valid() &&
        osgTexture->getOsgTexture() == defaultTexture_.get()) {
        defaultTexture_ = nullptr;
    }
    delete texture;
}

void OsgScene::setDefaultTexture(ITexture* texture) {
    OsgTexture* osgTexture = dynamic_cast<OsgTexture*>(texture);
    if (!osgTexture) {
        defaultTexture_ = nullptr;
        return;
    }
    defaultTexture_ = osgTexture->getOsgTexture();
}

ITexture* OsgScene::getDefaultTexture() {
    return defaultTexture_ ? new OsgTexture(defaultTexture_.get()) : nullptr;
}

void OsgScene::clearCache() {
    defaultTexture_ = nullptr;
}

void OsgScene::applyStipple(osg::StateSet* stateset, domain::LineStyle lineStyle, const std::string& stippleId) {
    (void)stateset;
    (void)lineStyle;
    (void)stippleId;
}

osg::Texture2D* OsgScene::getOrCreateStippleTexture(const std::string& name) {
    // Return cached texture if already created
    auto it = stippleTextures_.find(name);
    if (it != stippleTextures_.end()) {
        return it->second.get();
    }

    // Look up the stipple definition
    const StippleDef* def = nullptr;
    for (int i = 0; i < kNumStippleDefs; ++i) {
        if (name == kStippleDefs[i].name) {
            def = &kStippleDefs[i];
            break;
        }
    }
    if (!def) return nullptr;

    // Generate RGBA pixel data from the pattern bitmap
    auto pixels = parseStippleData(*def);

    // Create OSG image and texture
    osg::Image* image = new osg::Image();
    image->setImage(def->width, def->height, 1, GL_RGBA, GL_RGBA,
                    GL_UNSIGNED_BYTE, pixels.release(),
                    osg::Image::USE_NEW_DELETE);

    osg::ref_ptr<osg::Texture2D> tex = new osg::Texture2D();
    tex->setImage(image);
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    tex->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);

    stippleTextures_[name] = tex;
    return tex.get();
}


void OsgScene::createCoordinateAxes() {
    // Draw axes at the world origin with a fixed large length so they're
    // visible regardless of scene scale and position.
    osg::Vec3 origin(0,0,0);
    float axisLen = 100.0f;
    
    // X axis - Red - line with arrow tip
    {
        osg::Geode* geode = new osg::Geode();
        geode->setName("CoordAxes");
        
        osg::Vec3Array* verts = new osg::Vec3Array();
        verts->push_back(origin);
        verts->push_back(origin + osg::Vec3(axisLen, 0, 0));
        verts->push_back(origin + osg::Vec3(axisLen, 0, 0));
        verts->push_back(origin + osg::Vec3(axisLen - 5, 3, 0));
        verts->push_back(origin + osg::Vec3(axisLen, 0, 0));
        verts->push_back(origin + osg::Vec3(axisLen - 5, -3, 0));

        osg::Vec4Array* colors = new osg::Vec4Array();
        colors->push_back(osg::Vec4(1, 0, 0, 1));

        osg::Geometry* geom = new osg::Geometry();
        geom->setVertexArray(verts);
        geom->setColorArray(colors, osg::Array::BIND_OVERALL);
        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 6));

        osg::StateSet* ss = geom->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        osg::LineWidth* lw = new osg::LineWidth(5.0f);
        ss->setAttributeAndModes(lw, osg::StateAttribute::ON);

        geode->addDrawable(geom);
        objectRoot_->addChild(geode);
    }

    // Y axis - Green
    {
        osg::Geode* geode = new osg::Geode();
        geode->setName("CoordAxes");

        osg::Vec3Array* verts = new osg::Vec3Array();
        verts->push_back(origin);
        verts->push_back(origin + osg::Vec3(0, axisLen, 0));
        verts->push_back(origin + osg::Vec3(0, axisLen, 0));
        verts->push_back(origin + osg::Vec3(3, axisLen - 5, 0));
        verts->push_back(origin + osg::Vec3(0, axisLen, 0));
        verts->push_back(origin + osg::Vec3(-3, axisLen - 5, 0));

        osg::Vec4Array* colors = new osg::Vec4Array();
        colors->push_back(osg::Vec4(0, 1, 0, 1));

        osg::Geometry* geom = new osg::Geometry();
        geom->setVertexArray(verts);
        geom->setColorArray(colors, osg::Array::BIND_OVERALL);
        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 6));

        osg::StateSet* ss = geom->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        osg::LineWidth* lw = new osg::LineWidth(5.0f);
        ss->setAttributeAndModes(lw, osg::StateAttribute::ON);

        geode->addDrawable(geom);
        objectRoot_->addChild(geode);
    }

    // Z axis - Blue
    {
        osg::Geode* geode = new osg::Geode();
        geode->setName("CoordAxes");
        
        osg::Vec3Array* verts = new osg::Vec3Array();
        verts->push_back(osg::Vec3(0, 0, 0));
        verts->push_back(osg::Vec3(0, 0, axisLen));
        verts->push_back(osg::Vec3(0, 0, axisLen));
        verts->push_back(osg::Vec3(0, 3, axisLen - 5));
        verts->push_back(osg::Vec3(0, 0, axisLen));
        verts->push_back(osg::Vec3(0, -3, axisLen - 5));
        
        osg::Vec4Array* colors = new osg::Vec4Array();
        colors->push_back(osg::Vec4(0, 0, 1, 1));
        
        osg::Geometry* geom = new osg::Geometry();
        geom->setVertexArray(verts);
        geom->setColorArray(colors, osg::Array::BIND_OVERALL);
        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 6));
        
        osg::StateSet* ss = geom->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        osg::LineWidth* lw = new osg::LineWidth(5.0f);
        ss->setAttributeAndModes(lw, osg::StateAttribute::ON);
        
        geode->addDrawable(geom);
        objectRoot_->addChild(geode);
    }
    
    fprintf(stderr, "DEBUG createCoordinateAxes: axes at (%.1f,%.1f,%.1f) length=%.1f\n", origin.x(), origin.y(), origin.z(), axisLen);
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
#include "OsgScene.h"
#include "OsgFontAtlas.h"
#include "OsgGeometry.h"
#include "OsgLight.h"
#include "OsgMaterial.h"
#include "OsgTextGeode.h"
#include "OsgTexture.h"

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
#include <osg/LOD>
#include <osg/PolygonOffset>
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
                fprintf(stderr, "DEBUG: Loaded font: %s\n", *path);
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
    objectRecordsById_.clear();
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
    if (it != layers_.end()) {
        it->second.visible = visible;
        for (const auto& pair : objectsById_) {
            auto layerIt = objectLayerById_.find(pair.first);
            if (layerIt != objectLayerById_.end() && layerIt->second == layerId) {
                pair.second->setVisible(visible);
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
    if (!objectRoot_ || objectsById_.empty()) {
        return nullptr;
    }

    osg::Vec3d rayOrigin(origin.x, origin.y, origin.z);
    osg::Vec3d rayDirection(direction.x, direction.y, direction.z);
    if (rayDirection.length2() <= 1e-12) {
        return nullptr;
    }
    rayDirection.normalize();

    const double distance = maxDistance > 0.0F ? static_cast<double>(maxDistance) : 1e6;
    osg::Vec3d rayEnd = rayOrigin + rayDirection * distance;

    osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector =
        new osgUtil::LineSegmentIntersector(rayOrigin, rayEnd);
    osgUtil::IntersectionVisitor visitor(intersector.get());
    objectRoot_->accept(visitor);

    if (!intersector->containsIntersections()) {
        return nullptr;
    }

    for (const auto& intersection : intersector->getIntersections()) {
        for (auto nodeIt = intersection.nodePath.rbegin(); nodeIt != intersection.nodePath.rend(); ++nodeIt) {
            osg::Node* candidateNode = *nodeIt;
            if (!candidateNode) {
                continue;
            }
            for (const auto& objectPair : objectsById_) {
                OsgSceneObject* candidateObject = objectPair.second;
                if (!candidateObject || !candidateObject->isSelectable() || !candidateObject->isVisible()) {
                    continue;
                }
                if (candidateObject->getOsgNode() == candidateNode) {
                    return candidateObject;
                }
            }
        }
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
    
    // English comment.
    for (const auto& layer : snapshot.layers) {
        layers_[layer.layerId] = layer;
    }
    
    // English comment.
    for (const auto& obj : snapshot.objects) {
        const domain::LayerRecord* layer = getLayer(obj.layerId);
        OsgSceneObject* sceneObj = createSceneObject(obj, layer);
        if (sceneObj) {
            addObject(sceneObj);
        }
    }
}

void OsgScene::upsertObjects(const std::vector<domain::ObjectRecord>& objects) {
    for (const auto& obj : objects) {
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

        const domain::LayerRecord* layer = getLayer(obj.layerId);
        OsgSceneObject* sceneObj = createSceneObject(obj, layer);
        if (sceneObj) {
            addObject(sceneObj);
        }
    }
}

void OsgScene::updateObjectVisibility(
    const std::vector<std::pair<std::string, bool>>& visibilityUpdates) {
    for (const auto& entry : visibilityUpdates) {
        const std::string& objectId = entry.first;
        const bool visible = entry.second;
        auto found = objectsById_.find(objectId);
        if (found != objectsById_.end() && found->second) {
            found->second->setVisible(visible);
        }
    }
}

void OsgScene::removeObjectsById(const std::vector<std::string>& objectIds) {
    if (objectIds.empty()) {
        return;
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
        objectRecordsById_.erase(id);
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
    // English comment.
    for (auto& pair : highlightNodes_) {
        osg::Group* parent = pair.second->getParent(0);
        if (parent) {
            parent->removeChild(pair.second);
        }
        highlightRoot_->removeChild(pair.second);
    }
    highlightNodes_.clear();
    
    // English comment.
    if (!previousSelectedId_.empty()) {
        auto prevIt = objectsById_.find(previousSelectedId_);
        if (prevIt != objectsById_.end()) {
            restoreObjectMaterial(prevIt->second->getOsgNode());
        }
    }
    
    if (objectId.empty()) {
        previousSelectedId_.clear();
        return;
    }
    
    // English comment.
    auto it = objectsById_.find(objectId);
    if (it != objectsById_.end()) {
        osg::Node* node = it->second->getOsgNode();
        if (node) {
            // English comment.
            applyGlowMaterial(node);
            
            // English comment.
            osg::Geometry* highlightGeom = createSelectionGeometry(node);
            osg::Geode* geode = new osg::Geode();
            geode->addDrawable(highlightGeom);
            
            // English comment.
            // English comment.
            highlightRoot_->addChild(geode);
            highlightNodes_[objectId] = geode;
            
            previousSelectedId_ = objectId;
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
    
    // Store object record for label recreation
    objectRecordsById_[obj.objectId] = obj;
    
    // Attach name labels if display is enabled
    if (displayNamesVisible_ && !obj.displayName.empty()) {
        attachObjectLabels(transform, obj, layer);
    }
    
    return sceneObj;
}

void OsgScene::setDisplayNamesVisible(bool visible) {
    fprintf(stderr, "DEBUG setDisplayNamesVisible: visible=%d displayNamesVisible_ was %d\n", visible, displayNamesVisible_);
    if (displayNamesVisible_ == visible) {
        return;
    }
    displayNamesVisible_ = visible;
    
    if (visible) {
        // Rebuild labels for all existing objects
        fprintf(stderr, "DEBUG setDisplayNamesVisible: rebuilding labels for %zu objects\n", objectRecordsById_.size());
        for (const auto& pair : objectRecordsById_) {
            const std::string& objectId = pair.first;
            const domain::ObjectRecord& obj = pair.second;
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
            auto layerIt = layers_.find(obj.layerId);
            const domain::LayerRecord* layer = (layerIt != layers_.end()) ? &layerIt->second : nullptr;
            attachObjectLabels(transform, obj, layer);
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
    }
}

void OsgScene::attachObjectLabels(osg::MatrixTransform* transform,
                                  const domain::ObjectRecord& obj,
                                  const domain::LayerRecord* layer) {
    if (!transform || obj.displayName.empty()) {
        return;
    }
    
    // DEBUG: Show all objects being processed
    static int objDebugCount = 0;
    if (objDebugCount < 10) {
        fprintf(stderr, "DEBUG attach: name=%s displayName=%s type=%d hasBbox=%d displayNamesVisible_=%d\n",
                obj.objectId.c_str(), obj.displayName.c_str(), (int)obj.type, obj.hasBbox, displayNamesVisible_);
        objDebugCount++;
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
    }

    // Skip labels for tiny objects (charSize would be too small to read or
    // would require a disproportionately large minimum that dwarfs the object).
    float minDim = std::min({xlen, ylen, zlen});
    if (minDim < 0.05F) {
        return;
    }

    // Text color: always white now that we have a dark background halo for contrast.
    // (The old brightness-based white/black switch was needed without a halo;
    //  with the halo, white text is readable on any surface.)
    osg::Vec4 textColor(1.0F, 1.0F, 1.0F, 1.0F);

    // Character size: use the middle dimension to avoid extremes.
    // For pins/vias, the bbox can be very large in one direction (extends along wire).
    // For normal objects, we scale with maxDim but cap to prevent giants.
    float maxDim = std::max({xlen, ylen, zlen});
    float midDim;
    if (obj.type == domain::ObjectType::Pin || obj.type == domain::ObjectType::Via) {
        // For pins and vias, use the smallest dimension (thickness) as reference
        // to avoid oversized labels for elongated bbox
        midDim = minDim;
    } else {
        midDim = maxDim;
    }
    // Use glview-style scaling: text should be ~0.16 world units (40px * 0.004 scale)
    // This gives approximately 4% of object dimension vs 20% before
    float charSize = midDim * 0.04F;
    
    // Ensure text is readable with minimum size floor (glview default scale)
    charSize = std::max(0.16F, charSize);
    // Cap to prevent too large labels
    charSize = std::min(1.0F, charSize);
    
    // For pins/vias with long names, further constrain charSize to fit text on small faces
    if (obj.type == domain::ObjectType::Pin || obj.type == domain::ObjectType::Via) {
        // Approximate text width: ~24 world units per char at FONT_SIZE=40
        // Text should fit within the smallest face dimension
        float approxTextWidth = static_cast<float>(obj.displayName.length()) * 24.0F * (charSize / 40.0F);
        float maxTextWidth = minDim * 0.8F;  // Text shouldn't exceed 80% of smallest dimension
        if (approxTextWidth > maxTextWidth) {
            charSize = charSize * maxTextWidth / approxTextWidth;
            charSize = std::max(0.05F, charSize);  // Hard floor for visibility
        }
    }
    
    // DEBUG: Log label info for first few objects
    static int labelDebugCount = 0;
    if (labelDebugCount < 20 && obj.displayName.length() > 0) {
        fprintf(stderr, "DEBUG label: name=%s type=%d xlen=%.3f ylen=%.3f zlen=%.3f minDim=%.3f maxDim=%.3f midDim=%.3f charSize=%.3f frontZ=%.3f\n",
                obj.displayName.c_str(), (int)obj.type, xlen, ylen, zlen, minDim, maxDim, midDim, charSize,
                zlen * 0.5F + charSize * 0.02F);
        labelDebugCount++;
    }
    
    // Create label group
    osg::Group* labelGroup = new osg::Group();
    labelGroup->setName(obj.objectId + ":labels");
    
    // Determine if object is wire-like (one dimension dominates)
    float dimRatio = maxDim / std::max(0.01F, minDim);
    bool isWireLike = (dimRatio > 10.0F);
    
    // Face selection: 
    // For normal objects: 2 pairs of faces (4 faces total)
    // For wire-like objects: only top face (1 face) to avoid clutter
    bool needFrontBack = !isWireLike;
    bool needTopBottom = !isWireLike;
    bool needRightLeft = false;
    
    if (isWireLike) {
        // Wire-like: show on top face only
        needTopBottom = true;
    } else if (ylen > xlen && ylen > zlen) {
        // Long in vertical - use front/back + right/left
        needFrontBack = true;
        needRightLeft = true;
    } else if (zlen > ylen && zlen > xlen) {
        // Long in depth - use top/bottom + right/left
        needTopBottom = true;
        needRightLeft = true;
    } else {
        // Long in X or cube - use front/back + top/bottom
        needFrontBack = true;
        needTopBottom = true;
    }
    
    // Center point in LOCAL coordinates (labelGroup is under object transform)
    // Geometry is positioned at (xOffset, yOffset, zBase) where:
    // xOffset = bboxMin[0] - transform[12]
    // yOffset = bboxMin[1] - transform[13]
    // So bboxMin in LOCAL coords is (bboxMin[0] - transform[12], bboxMin[1] - transform[13], bboxMin[2])
    // and bbox center in LOCAL coords is ((bboxMax[0]+bboxMin[0])/2 - transform[12], ...)
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    
    if (obj.hasBbox) {
        centerX = (obj.bboxMax[0] + obj.bboxMin[0]) * 0.5F - obj.transform[12];
        centerY = (obj.bboxMax[1] + obj.bboxMin[1]) * 0.5F - obj.transform[13];
        centerZ = (obj.bboxMax[2] + obj.bboxMin[2]) * 0.5F - obj.transform[14];
    }
    
    float offset = charSize * 0.02F;  // Small offset proportional to text size
    
    // Surface positions in LOCAL coordinates where geometry is at bboxMin
    // front face: bboxMax[2] - transform[14] + offset
    // back face: bboxMin[2] - transform[14] - offset  
    // top face: bboxMax[1] - transform[13] + offset
    // bottom face: bboxMin[1] - transform[13] - offset
    // right face: bboxMax[0] - transform[12] + offset
    // left face: bboxMin[0] - transform[12] - offset
    float frontZ = (obj.hasBbox ? obj.bboxMax[2] : zlen) - obj.transform[14] + offset;
    float backZ = (obj.hasBbox ? obj.bboxMin[2] : -zlen) - obj.transform[14] - offset;
    float topY = (obj.hasBbox ? obj.bboxMax[1] : ylen) - obj.transform[13] + offset;
    float bottomY = (obj.hasBbox ? obj.bboxMin[1] : -ylen) - obj.transform[13] - offset;
    float rightX = (obj.hasBbox ? obj.bboxMax[0] : xlen) - obj.transform[12] + offset;
    float leftX = (obj.hasBbox ? obj.bboxMin[0] : -xlen) - obj.transform[12] - offset;
    
    // Add text on selected faces with correct rotation
    if (needFrontBack) {
        osg::Quat frontRot = computeFaceRotation(LabelFace::Front, xlen, ylen, zlen);
        osg::Quat backRot = computeFaceRotation(LabelFace::Back, xlen, ylen, zlen);

        // Front face (top Z surface)
        osg::Node* frontLabel = makeTextGeodeForFace(obj.displayName, centerX, centerY, frontZ, charSize, textColor, frontRot);
        if (frontLabel) {
            labelGroup->addChild(frontLabel);
        }
        // Back face
        osg::Node* backLabel = makeTextGeodeForFace(obj.displayName, centerX, centerY, backZ, charSize, textColor, backRot);
        if (backLabel) {
            labelGroup->addChild(backLabel);
        }
    }

    if (needTopBottom) {
        osg::Quat topRot = computeFaceRotation(LabelFace::Top, xlen, ylen, zlen);
        osg::Quat bottomRot = computeFaceRotation(LabelFace::Bottom, xlen, ylen, zlen);

        // DEBUG: Log charSize before calling makeTextGeodeForFace
        fprintf(stderr, "DEBUG preTopFace: name=%s charSize=%.3f topY=%.3f centerZ=%.3f\n",
                obj.displayName.c_str(), charSize, topY, centerZ);

        // Top face (top Y surface)
        osg::Node* topLabel = makeTextGeodeForFace(obj.displayName, centerX, topY, centerZ, charSize, textColor, topRot);
        if (topLabel) {
            labelGroup->addChild(topLabel);
        }
        // Bottom face
        osg::Node* bottomLabel = makeTextGeodeForFace(obj.displayName, centerX, bottomY, centerZ, charSize, textColor, bottomRot);
        if (bottomLabel) {
            labelGroup->addChild(bottomLabel);
        }
    }

    if (needRightLeft) {
        osg::Quat rightRot = computeFaceRotation(LabelFace::Right, xlen, ylen, zlen);
        osg::Quat leftRot = computeFaceRotation(LabelFace::Left, xlen, ylen, zlen);

        // Right face (right X surface)
        osg::Node* rightLabel = makeTextGeodeForFace(obj.displayName, rightX, centerY, centerZ, charSize, textColor, rightRot);
        if (rightLabel) {
            labelGroup->addChild(rightLabel);
        }
        // Left face
        osg::Node* leftLabel = makeTextGeodeForFace(obj.displayName, leftX, centerY, centerZ, charSize, textColor, leftRot);
        if (leftLabel) {
            labelGroup->addChild(leftLabel);
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
}

osg::Quat OsgScene::computeFaceRotation(LabelFace face,
                                         float xlen,
                                         float ylen,
                                         float zlen) const {
    // Text geometry is in the XY plane at the origin, facing +Z.
    //
    // Base rotation: makes the text face the correct cube face.
    // Readability rotation: when an object is taller in one axis, rotate
    // the text so it remains readable (matches glview behavior).

    // Base rotation: which face to point at
    osg::Quat base;
    switch (face) {
        case LabelFace::Front:                                 break; // faces +Z
        case LabelFace::Back:  base = osg::Quat(osg::PI,       osg::Vec3(0,1,0)); break; // faces -Z
        case LabelFace::Right: base = osg::Quat(osg::PI_2,     osg::Vec3(0,1,0)); break; // faces +X
        case LabelFace::Left:  base = osg::Quat(-osg::PI_2,    osg::Vec3(0,1,0)); break; // faces -X
        case LabelFace::Top:   base = osg::Quat(-osg::PI_2,    osg::Vec3(1,0,0)); break; // faces +Y
        case LabelFace::Bottom:base = osg::Quat(osg::PI_2,     osg::Vec3(1,0,0)); break; // faces -Y
    }

    // Readability rotation: applied after base, rotates text in the face plane
    osg::Quat readability;
    bool isVertical = (ylen > xlen && ylen > zlen);
    bool isDeep = (zlen > ylen && zlen > xlen);

    if (isVertical) {
        // Object is vertical (Y longest) — rotate text 90° so it reads vertically
        switch (face) {
            case LabelFace::Front:
            case LabelFace::Back:
                readability = osg::Quat(osg::PI_2, osg::Vec3(0,0,1)); break;
            case LabelFace::Right:
            case LabelFace::Left:
                readability = osg::Quat(osg::PI_2, osg::Vec3(1,0,0)); break;
            default: break;
        }
    } else if (isDeep) {
        // Object is deep (Z longest) — rotate text 90° on top/bottom
        switch (face) {
            case LabelFace::Top:
                readability = osg::Quat(osg::PI_2, osg::Vec3(0,1,0)); break;
            case LabelFace::Bottom:
                readability = osg::Quat(-osg::PI_2, osg::Vec3(0,1,0)); break;
            default: break;
        }
    }

    // Order: readability * base — readability is applied in the face plane
    // so it comes after the base orientation.
    return readability * base;
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

    // Font atlas renders at FONT_SIZE=40px; scale to requested charSize
    float scale = charSize / static_cast<float>(OsgFontAtlas::FONT_SIZE);

    // DEBUG: Log transform info
    static int xformDebugCount = 0;
    if (xformDebugCount < 5) {
        fprintf(stderr, "DEBUG xform: text=%s center=(%.2f,%.2f,%.2f) charSize=%.3f scale=%.6f\n",
                text.c_str(), centerX, centerY, centerZ, charSize, scale);
        xformDebugCount++;
    }

    // OsgTextGeode creates centered text at origin, at font atlas scale
    OsgTextGeode* textGeode = new OsgTextGeode(text, fontAtlas_.get(), color);
    textGeode->setName("label:" + text);

    // Apply transform: scale, rotate, translate
    // OSG uses row vectors: v' = v * scale * rotate * translate
    osg::MatrixTransform* mt = new osg::MatrixTransform();
    mt->setName("labelXform:" + text);
    osg::Matrixd matrix = osg::Matrixd::scale(scale, scale, scale)
                          * osg::Matrixd::rotate(rotation)
                          * osg::Matrixd::translate(centerX, centerY, centerZ);
    mt->setMatrix(matrix);
    mt->addChild(textGeode);

    // DEBUG: Check first vertex after transform
    static int vertWorldDebugCount = 0;
    if (vertWorldDebugCount < 3 && text == "_484_") {
        osg::Vec3f testVert(0, 0, 0);  // Original vertex at origin
        osg::Vec3f transformed = testVert * matrix;
        fprintf(stderr, "DEBUG vertWorld: text=%s orig=(0,0,0) transformed=(%.4f,%.4f,%.4f)\n",
                text.c_str(), transformed.x(), transformed.y(), transformed.z());
        vertWorldDebugCount++;
    }

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
        case domain::ObjectType::Blockage:
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
        case domain::ObjectType::Via:
        case domain::ObjectType::Drc:
        case domain::ObjectType::Track: {
            const LocalBoxParams box = computeLocalBoxParamsFromBbox(obj, 0.05F);
            osg::Geometry* geom = createBoxGeometry(
                box.xOffset, box.yOffset, box.width, box.height, box.depth);
            addLayerColoredDrawable(geom);
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

void OsgScene::configureGeodeRenderState(osg::Geode* geode,
                                         const domain::LayerRecord& layer,
                                         domain::ObjectType objectType) {
    if (!geode) {
        return;
    }

    osg::StateSet* stateset = geode->getOrCreateStateSet();
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

    applyStipple(stateset, layer.lineStyle, layer.stippleId);

    if (effectiveTransparency > 0.0F) {
        stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
        stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    }

    stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
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
    // Core-profile contexts used by QOpenGLWidget do not reliably support fixed-function line stipple.
    return;

    // Stipple patterns: solid, dash, dot, dashdot, cross
    if (stippleId == "solid" || lineStyle == domain::LineStyle::Solid) {
        // No stipple needed for solid
        return;
    }
    
    GLint factor = 1;
    GLushort pattern = 0xFFFF;
    
    if (stippleId == "dash" || lineStyle == domain::LineStyle::Dashed) {
        factor = 3;
        pattern = 0x00FF;  // 0000000011111111
    } else if (stippleId == "dot" || lineStyle == domain::LineStyle::Dotted) {
        factor = 1;
        pattern = 0x5555;  // 0101010101010101
    } else if (stippleId == "dashdot" || lineStyle == domain::LineStyle::DashDot) {
        factor = 2;
        pattern = 0x0F0F;  // 0000111100001111
    } else if (stippleId == "cross") {
        factor = 2;
        pattern = 0x3F3F;  // 0011111100111111
    }
    
    osg::LineStipple* stipple = new osg::LineStipple(factor, pattern);
    stateset->setAttributeAndModes(stipple, osg::StateAttribute::ON);
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
#include "OsgScene.h"
#include "OsgGeometry.h"
#include "OsgLight.h"
#include "OsgMaterial.h"
#include "OsgTexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
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
}

OsgScene::~OsgScene() {
    clear();
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
        auto byId = objectsById_.find(object->getId());
        if (byId != objectsById_.end()) {
            delete byId->second;
            objectsById_.erase(byId);
        }
        objectLayerById_.erase(object->getId());
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
    highlightNodes_.clear();
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
        fprintf(stderr, "DEBUG getBoundingBox: node=%s bound_center=(%f,%f,%f) radius=%f\n",
                node->getName().c_str(), bs.center().x(), bs.center().y(), bs.center().z(), bs.radius());
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
    
    // Debug output for transform (show first 5 wire objects)
    static int wireDebugCount = 0;
    if (obj.objectId.find("wire_") == 0 && wireDebugCount < 5) {
        fprintf(stderr, "DEBUG createSceneObject: %s hasBbox=%d layerId=%s transform=(%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f)\n",
                obj.objectId.c_str(), obj.hasBbox, obj.layerId.c_str(),
                obj.transform[0], obj.transform[1], obj.transform[2], obj.transform[3],
                obj.transform[4], obj.transform[5], obj.transform[6], obj.transform[7],
                obj.transform[8], obj.transform[9], obj.transform[10], obj.transform[11],
                obj.transform[12], obj.transform[13], obj.transform[14], obj.transform[15]);
        wireDebugCount++;
    }
    if (obj.objectId.find("debug_") == 0) {
        fprintf(stderr, "DEBUG createSceneObject: %s hasBbox=%d transform=(%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f)\n",
                obj.objectId.c_str(), obj.hasBbox,
                obj.transform[0], obj.transform[1], obj.transform[2], obj.transform[3],
                obj.transform[4], obj.transform[5], obj.transform[6], obj.transform[7],
                obj.transform[8], obj.transform[9], obj.transform[10], obj.transform[11],
                obj.transform[12], obj.transform[13], obj.transform[14], obj.transform[15]);
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
    if (obj.objectId.find("debug_") == 0) {
        osg::Vec3d t = mat.getTrans();
        fprintf(stderr, "DEBUG createSceneObject: %s matrix translation=(%f,%f,%f)\n",
                obj.objectId.c_str(), t.x(), t.y(), t.z());
    }
    
    OsgSceneObject* sceneObj = new OsgSceneObject(obj.objectId, transform);
    objectLayerById_[obj.objectId] = obj.layerId;
    
    // English comment.
    osg::Geode* geode = new osg::Geode();
    geode->setName(obj.objectId + ":geode");
    

    sceneObj->setVisible(!layer || layer->visible);
    sceneObj->setSelectable(!layer || layer->selectable);
       switch (obj.type) {
        case domain::ObjectType::Inst: {
            float width = 1.0F, height = 1.0F, depth = 0.3F;
            float xOffset = 0.0F, yOffset = 0.0F;
            if (obj.hasBbox) {
                width = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
                height = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
                depth = std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2]);
                xOffset = obj.bboxMin[0] - obj.transform[12];
                yOffset = obj.bboxMin[1] - obj.transform[13];
            }
            osg::Geometry* geom = createBoxGeometry(xOffset, yOffset, width, height, depth);
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
            // Create box from real bbox dimensions when available.
            float width = 1.0F, height = 1.0F, depth = 0.3F;
            float xOffset = 0.0F, yOffset = 0.0F;
            if (obj.hasBbox) {
                width = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
                height = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
                depth = std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2]);
                xOffset = obj.bboxMin[0] - obj.transform[12];
                yOffset = obj.bboxMin[1] - obj.transform[13];
            } else if (layer) {
                depth = std::max(layer->lineWidth * 0.5F, 0.05F);
            }
            osg::Geometry* geom = createBoxGeometry(xOffset, yOffset, width, height, depth);
            if (geom) {
                if (layer) {
                    applyFlatColor(geom, osg::Vec4(layer->color.r, layer->color.g, layer->color.b,
                                                   1.0F - layer->transparency));
                }
                geode->addDrawable(geom);
            }
            break;
        }
        case domain::ObjectType::Wire: {
            // English comment.
            // Transform is center, so geometry needs offset so that center + offset = bboxMin
            // We pass the offset to createPolylineGeometry via xOffset/yOffset params
            float xOffset = 0.0F, yOffset = 0.0F;
            if (obj.hasBbox) {
                float centerX = obj.transform[12];
                float centerY = obj.transform[13];
                xOffset = obj.bboxMin[0] - centerX;  // negative = geometry left of center
                yOffset = obj.bboxMin[1] - centerY;  // negative = geometry below center
            }
            osg::Geometry* geom = createPolylineGeometry(obj, layer, xOffset, yOffset);
            if (geom) {
                if (layer) {
                    applyFlatColor(geom, osg::Vec4(layer->color.r, layer->color.g, layer->color.b,
                                                   1.0F - layer->transparency));
                }
                geode->addDrawable(geom);
            }
            break;
        }
        case domain::ObjectType::Via:
        case domain::ObjectType::Drc:
        case domain::ObjectType::Track: {
            // English comment.
            // Transform uses center, so geometry must be offset to match bboxMin
            float width = 1.0F, height = 1.0F;
            float depth = 0.05F;
            float xOffset = 0.0F, yOffset = 0.0F;
            if (obj.hasBbox) {
                width = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
                height = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
                depth = std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2]);
                // Calculate center from transform and offset geometry to match bboxMin
                float centerX = obj.transform[12];
                float centerY = obj.transform[13];
                xOffset = obj.bboxMin[0] - centerX;
                yOffset = obj.bboxMin[1] - centerY;
            }
            osg::Geometry* geom = createBoxGeometry(xOffset, yOffset, width, height, depth);
            if (geom) {
                if (layer) {
                    applyFlatColor(geom, osg::Vec4(layer->color.r, layer->color.g, layer->color.b,
                                                   1.0F - layer->transparency));
                }
                geode->addDrawable(geom);
            }
            break;
        }
        default: {
            // English comment.
            // English comment.
            float width = 1.0F, height = 1.0F;
            float depth = 0.2F;
            float xOffset = 0.0F;
            float yOffset = 0.0F;
            if (obj.hasBbox) {
                width = std::max(0.01F, obj.bboxMax[0] - obj.bboxMin[0]);
                height = std::max(0.01F, obj.bboxMax[1] - obj.bboxMin[1]);
                depth = std::max(0.01F, obj.bboxMax[2] - obj.bboxMin[2]);
                xOffset = obj.bboxMin[0];
                yOffset = obj.bboxMin[1];
            }
            osg::Geometry* geom = createBoxGeometry(xOffset, yOffset, width, height, depth);
            if (geom) {
                if (layer) {
                    applyFlatColor(geom, osg::Vec4(layer->color.r, layer->color.g, layer->color.b,
                                                   1.0F - layer->transparency));
                }
                geode->addDrawable(geom);
            }
            break;
        }
    }
    
    // English comment.
    if (layer) {
        osg::StateSet* stateset = geode->getOrCreateStateSet();
        stateset->setMode(GL_LIGHTING, osg::StateAttribute::ON);

        osg::Material* material = new osg::Material();
        material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
        const float baseR = std::clamp(layer->color.r, 0.0F, 1.0F);
        const float baseG = std::clamp(layer->color.g, 0.0F, 1.0F);
        const float baseB = std::clamp(layer->color.b, 0.0F, 1.0F);
          const float legoR = std::clamp(baseR * 1.08F, 0.0F, 1.0F);
          const float legoG = std::clamp(baseG * 1.08F, 0.0F, 1.0F);
          const float legoB = std::clamp(baseB * 1.08F, 0.0F, 1.0F);
          const float effectiveTransparency = (obj.type == domain::ObjectType::Inst)
            ? std::max(layer->transparency, 0.55F)
            : layer->transparency;
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

        // English comment.
        osg::LineWidth* lw = new osg::LineWidth(layer->lineWidth);
        stateset->setAttributeAndModes(lw, osg::StateAttribute::ON);
        
        // English comment.
        osg::PolygonMode* pm = new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::FILL);
        stateset->setAttributeAndModes(pm, osg::StateAttribute::ON);
        
        // English comment.
        applyStipple(stateset, layer->lineStyle, layer->stippleId);
        
        // English comment.
        if (effectiveTransparency > 0.0F) {
            stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
            stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
        }
        
        // English comment.
        stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    }
    
    transform->addChild(geode);
    return sceneObj;
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
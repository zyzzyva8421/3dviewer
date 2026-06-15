#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <osg/ref_ptr>
#include <osg/Object>
#include <osg/Quat>
#include <osg/Group>
#include <osg/Switch>
#include <osg/Geometry>
#include <osg/Material>

#include "render/api/IScene.h"
#include "render/api/IGeometry.h"
#include "render/api/IMaterial.h"
#include "render/api/ILight.h"
#include "render/api/ITexture.h"
#include "core/domain/SceneTypes.h"

namespace osg {
class Node;
class Geode;
class MatrixTransform;
class Geometry;
class Material;
class Light;
class Texture2D;
class LOD;
}

namespace viewer3d {
namespace render {
namespace backend_osg {

class OsgGeometry;
class OsgMaterial;
class OsgLightSystem;
class OsgTexture;
class OsgTextureFactory;
class OsgFontAtlas;

/**
 * @brief English description.
 */
class OsgSceneObject : public ISceneObject {
public:
    OsgSceneObject(const std::string& id, osg::MatrixTransform* node);
    virtual ~OsgSceneObject();
    
    const std::string& getId() const override { return id_; }
    void setGeometry(IGeometry* geometry) override;
    IGeometry* getGeometry() const override;
    void setMaterial(IMaterial* material) override;
    IMaterial* getMaterial() const override;
    void setTransform(const std::array<float, 16>& transform) override;
    const std::array<float, 16>& getTransform() const override;
    void setVisible(bool visible) override;
    bool isVisible() const override;
    void setSelectable(bool selectable) override;
    bool isSelectable() const override;
    void setSelected(bool selected) override;
    bool isSelected() const override;
    void setHighlighted(bool highlighted) override;
    bool isHighlighted() const override;
    
    osg::MatrixTransform* getOsgNode() const { return node_; }
    
private:
    std::string id_;
    osg::ref_ptr<osg::MatrixTransform> node_;
    osg::ref_ptr<osg::Geode> geometryNode_;
    IGeometry* geometry_ = nullptr;
    IMaterial* material_ = nullptr;
    std::array<float, 16> transform_;
    bool visible_ = true;
    bool selectable_ = true;
    bool selected_ = false;
    bool highlighted_ = false;
};

/**
 * @brief English description.
 */
class OsgScene : public osg::Object,
                 public IScene,
                 public IGeometryFactory,
                 public IMaterialFactory,
                 public ILightSystem,
                 public ITextureFactory {
public:
    OsgScene();
    virtual ~OsgScene();

    osg::Object* cloneType() const override;
    osg::Object* clone(const osg::CopyOp& copyop) const override;
    bool isSameKindAs(const osg::Object* obj) const override;
    const char* libraryName() const override;
    const char* className() const override;
    
    // ========== IScene ==========
    void addObject(ISceneObject* object) override;
    void removeObject(ISceneObject* object) override;
    void clear() override;
    size_t getObjectCount() const override;
    std::vector<ISceneObject*> getObjectsByLayer(const std::string& layerId) const override;
    void setLayerVisible(const std::string& layerId, bool visible) override;
    bool isLayerVisible(const std::string& layerId) const override;
    void updateLayer(const domain::LayerRecord& layer) override;
    const domain::LayerRecord* getLayer(const std::string& layerId) const override;
    ISceneObject* raycast(const Vec3& origin, const Vec3& direction, float maxDistance) override;
    void getBoundingBox(Vec3& min, Vec3& max) const override;
    
    // ========== IGeometryFactory ==========
    IGeometry* createCube(float width, float height, float depth) override;
    IGeometry* createBox(float x, float y, float width, float height, float depth) override;
    IGeometry* createPolygon(const std::vector<Vec2>& vertices, float height) override;
    IGeometry* createPolyline(const std::vector<Vec3>& points, float width) override;
    IGeometry* createPoint(float size) override;
    IGeometry* createSphere(float radius, int segments = 16) override;
    IGeometry* createCylinder(float radius, float height, int segments = 16) override;
    IGeometry* createFromData(const GeometryParams& params) override;
    
    // ========== IMaterialFactory ==========
    IMaterial* createMaterial() override;
    IMaterial* createMaterial(const MaterialDesc& desc) override;
    IMaterial* createHighlightMaterial(float r, float g, float b) override;
    IMaterial* createSelectionMaterial() override;
    IMaterial* createGlowMaterial(float intensity = 1.0F) override;
    
    // ========== ILightSystem ==========
    ILight* createLight(LightType type) override;
    void destroyLight(ILight* light) override;
    size_t getLightCount() const override;
    void setMainLight(ILight* light) override;
    ILight* getMainLight() override;
    void update() override;
    
    // ========== ITextureFactory ==========
    ITexture* loadTexture(const TextureLoadParams& params) override;
    ITexture* createTexture(int width, int height, TextureFormat format, const uint8_t* data) override;
    ITexture* createTexture(int width, int height, TextureFormat format) override;
    void destroyTexture(ITexture* texture) override;
    void setDefaultTexture(ITexture* texture) override;
    ITexture* getDefaultTexture() override;
    void clearCache() override;
    
    // English comment.
    void loadSnapshot(const domain::SceneSnapshot& snapshot);
    void upsertObjects(const std::vector<domain::ObjectRecord>& objects);
    void updateObjectVisibility(const std::vector<std::pair<std::string, bool>>& visibilityUpdates);
    void removeObjectsById(const std::vector<std::string>& objectIds);
    void removeLayersById(const std::vector<std::string>& layerIds);
    void setSelectedObject(const std::string& objectId);
    osg::Node* getObjectNode(const std::string& objectId) const;
    
    osg::Group* getObjectRoot() const { return objectRoot_.get(); }
    osg::Switch* getHighlightRoot() const { return highlightRoot_.get(); }
    
    // English comment.
    OsgSceneObject* createSceneObject(const domain::ObjectRecord& obj, const domain::LayerRecord* layer);
    
    // Name label display control
    void setDisplayNamesVisible(bool visible);
    bool isDisplayNamesVisible() const { return displayNamesVisible_; }

    // Stipple surface pattern control
    void setStippleVisible(bool visible);
    bool isStippleVisible() const { return stippleVisible_; }

    // Object hide/show
    void setObjectVisible(const std::string& objectId, bool visible);
    bool isObjectVisible(const std::string& objectId) const;

    // Find object info from screen coords (for tooltips)
    std::string findObjectIdAtScreen(float nx, float ny) const;

private:
    osg::ref_ptr<osg::Group> objectRoot_;
    osg::ref_ptr<osg::Switch> highlightRoot_;
    std::vector<osg::ref_ptr<osg::Node>> objects_;
    std::unordered_map<std::string, OsgSceneObject*> objectsById_;
    std::unordered_map<std::string, std::string> objectLayerById_;
    std::unordered_map<std::string, domain::LayerRecord> layers_;
    std::unordered_map<std::string, osg::ref_ptr<osg::Node>> highlightNodes_;
    std::string previousSelectedId_;
    
    // Name label display state
    bool displayNamesVisible_ = false;
    bool stippleVisible_ = false;
    std::unordered_map<std::string, osg::ref_ptr<osg::Group>> textLabelNodes_;
    // Stipple LOD nodes (tracked separately for efficient toggle)
    std::unordered_map<std::string, osg::ref_ptr<osg::LOD>> stippleNodes_;
    // Pointer to ObjectRecord (avoids copying the 64-byte transform array)
    std::unordered_map<std::string, const domain::ObjectRecord*> objectRecordPtrs_;
    
    // Font atlas for text rendering (stb_truetype approach)
    std::unique_ptr<OsgFontAtlas> fontAtlas_;

    // Stipple texture cache: pattern name → RGBA texture
    std::unordered_map<std::string, osg::ref_ptr<osg::Texture2D>> stippleTextures_;

    // Hidden objects (excluded from selection + visibility)
    std::unordered_set<std::string> hiddenObjects_;

    std::vector<osg::ref_ptr<osg::Light>> lights_;
    osg::ref_ptr<osg::Light> mainLight_;

    osg::ref_ptr<osg::Texture2D> defaultTexture_;

    void applyStipple(osg::StateSet* stateset, domain::LineStyle lineStyle, const std::string& stippleId);
    osg::Texture2D* getOrCreateStippleTexture(const std::string& name);
    void attachObjectGeometry(osg::Geode* geode,
                              const domain::ObjectRecord& obj,
                              const domain::LayerRecord* layer);
    void configureGeodeRenderState(osg::Geode* geode,
                                   const domain::LayerRecord& layer,
                                   domain::ObjectType objectType);
    osg::Geometry* createBoxGeometry(float x, float y, float width, float height, float depth);
    osg::Geometry* createPolylineGeometry(const domain::ObjectRecord& obj, const domain::LayerRecord* layer, float xOffset = 0.0f, float yOffset = 0.0f);
    osg::Geometry* createPolygonGeometry(const std::vector<osg::Vec2>& points, float thickness);
    osg::Geometry* createPointGeometry(float size);
    void applyGlowMaterial(osg::Node* node);
    void restoreObjectMaterial(osg::Node* node);
    osg::Geometry* createSelectionGeometry(osg::Node* node);
    
    // Initialize font atlas (tries common system paths)
    bool initFontAtlas();
    
    // Debug: create XYZ coordinate axes at origin
    void createCoordinateAxes();

    // Label face orientation (glview-style)
    enum class LabelFace { Front, Back, Right, Left, Top, Bottom };

    // Name label helpers (glview-style text on faces)
    void attachObjectLabels(osg::MatrixTransform* transform, const domain::ObjectRecord& obj, const domain::LayerRecord* layer);
    void removeObjectLabels(const std::string& objectId);
    osg::Node* makeStippleFaceQuad(float cx, float cy, float cz,
                                    float halfW, float halfH,
                                    int dim1, int dim2,
                                    const osg::Vec4& color,
                                    const std::string& stippleId);
    osg::Node* makeTextGeodeForFace(const std::string& text,
                                    float centerX,
                                    float centerY,
                                    float centerZ,
                                    float charSize,
                                    const osg::Vec4& color,
                                    const osg::Quat& rotation);

    // DEBUG: colored marker box at text position for visual verification
    osg::Node* makeMarkerBox(float cx, float cy, float cz,
                             const osg::Vec4& color,
                             const osg::Quat& rotation);
    osg::Quat computeFaceRotation(LabelFace face,
                                  bool textAlongX,
                                  float xlen,
                                  float ylen,
                                  float zlen) const;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
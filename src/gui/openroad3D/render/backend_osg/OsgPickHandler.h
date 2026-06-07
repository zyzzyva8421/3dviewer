#pragma once

#include <osgViewer/ViewerEventHandlers>
#include <osgGA/GUIEventHandler>

namespace viewer3d {
namespace render {
namespace backend_osg {

class OsgBackend;

/**
 * @brief English description.
 */
class OsgPickHandler : public osgGA::GUIEventHandler {
public:
    explicit OsgPickHandler(OsgBackend* backend);
    virtual ~OsgPickHandler() {}
    
    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;
    
private:
    OsgBackend* backend_;
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
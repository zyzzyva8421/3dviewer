#include "OsgPickHandler.h"

#include <osgViewer/Viewer>
#include <osgUtil/LineSegmentIntersector>
#include <osgGA/GUIEventAdapter>

#include "OsgBackend.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

OsgPickHandler::OsgPickHandler(OsgBackend* backend)
    : backend_(backend) {
}

bool OsgPickHandler::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
    (void)aa;
    switch (ea.getEventType()) {
        case osgGA::GUIEventAdapter::PUSH: {
            // English comment.
            return false;  // English comment.
        }
        
        case osgGA::GUIEventAdapter::RELEASE: {
            // English comment.
            return false;
        }
        
        case osgGA::GUIEventAdapter::MOVE: {
            // English comment.
            return false;
        }
        
        case osgGA::GUIEventAdapter::SCROLL: {
            // English comment.
            return false;
        }
        
        case osgGA::GUIEventAdapter::KEYDOWN: {
            // English comment.
            return false;
        }
        
        default:
            return false;
    }
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d
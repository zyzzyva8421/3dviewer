#include "render/api/BackendFactory.h"

#include "render/api/IRenderBackend.h"
#include "render/backend_osg/OsgBackend.h"

namespace viewer3d {
namespace render {

std::unique_ptr<IRenderBackend> BackendFactory::create(BackendType type) {
  switch (type) {
    case BackendType::Osg:
      return std::make_unique<backend_osg::OsgBackend>();
    default:
      return nullptr;
  }
}

}  // namespace render
}  // namespace viewer3d

#pragma once

#include <memory>

#include "render/api/BackendType.h"

namespace viewer3d {
namespace render {

class IRenderBackend;

class BackendFactory {
 public:
  static std::unique_ptr<IRenderBackend> create(BackendType type);
};

}  // namespace render
}  // namespace viewer3d

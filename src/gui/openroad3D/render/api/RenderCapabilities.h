#pragma once

#include <string>

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 * 
 * English documentation.
 */
enum class RenderProfile {
    /** English comment. */
    Modern,
    
    /** English comment. */
    Legacy,
    
    /** English comment. */
    Software
};

/**
 * @brief English description.
 * 
 * English documentation.
 */
class RenderCapabilities {
public:
    /**
     * @brief English description.
     * @return Return value description.
     */
    static RenderProfile detectProfile();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static std::string getOpenGLVersion();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static bool isSoftwareRendering();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static std::string getRendererName();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static bool supportsInstancing();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static bool supportsShadowMapping();
    
    /**
     * @brief English description.
     * @return Return value description.
     */
    static bool supportsFloatTextures();
};

}  // namespace render
}  // namespace viewer3d
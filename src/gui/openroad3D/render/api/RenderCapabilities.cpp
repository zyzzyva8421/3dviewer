#include "RenderCapabilities.h"

#include <string>
#include <cstring>

// English comment.
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace viewer3d {
namespace render {

RenderProfile RenderCapabilities::detectProfile() {
    // English comment.
    if (isSoftwareRendering()) {
        return RenderProfile::Software;
    }
    
    // English comment.
    const std::string version = getOpenGLVersion();
    
    if (version.empty()) {
        return RenderProfile::Software;  // English comment.
    }
    
    // English comment.
    float versionNum = 0.0F;
    try {
        size_t dotPos = version.find('.');
        if (dotPos != std::string::npos) {
            float major = std::stof(version.substr(0, dotPos));
            float minor = std::stof(version.substr(dotPos + 1));
            versionNum = major + minor * 0.1F;
        }
    } catch (...) {
        return RenderProfile::Software;
    }
    
    if (versionNum >= 3.3F) {
        return RenderProfile::Modern;
    } else if (versionNum >= 2.0F) {
        return RenderProfile::Legacy;
    } else if (versionNum >= 1.5F) {
        return RenderProfile::Legacy;
    }
    
    return RenderProfile::Software;
}

std::string RenderCapabilities::getOpenGLVersion() {
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version) {
        return std::string(version);
    }
    return {};
}

bool RenderCapabilities::isSoftwareRendering() {
    // English comment.
    const char* envSoftware = getenv("LIBGL_ALWAYS_SOFTWARE");
    if (envSoftware && strcmp(envSoftware, "1") == 0) {
        return true;
    }
    
    const char* envSoftwareAlt = getenv("MESA_GL_VERSION_OVERRIDE");
    if (envSoftwareAlt && strstr(envSoftwareAlt, "llvmpipe")) {
        return true;
    }
    
    // English comment.
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer) {
        std::string rendererStr(renderer);
        // English comment.
        if (rendererStr.find("llvmpipe") != std::string::npos ||
            rendererStr.find("softpipe") != std::string::npos ||
            rendererStr.find("Software") != std::string::npos ||
            rendererStr.find("swrast") != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string RenderCapabilities::getRendererName() {
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer) {
        return std::string(renderer);
    }
    return {};
}

bool RenderCapabilities::supportsInstancing() {
    // English comment.
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions) {
        std::string extStr(extensions);
        if (extStr.find("GL_ARB_draw_instanced") != std::string::npos) {
            return true;
        }
    }
    
    // English comment.
    const std::string version = getOpenGLVersion();
    if (!version.empty()) {
        try {
            size_t dotPos = version.find('.');
            if (dotPos != std::string::npos) {
                float major = std::stof(version.substr(0, dotPos));
                if (major >= 3.1F) {
                    return true;
                }
            }
        } catch (...) {}
    }
    
    return false;
}

bool RenderCapabilities::supportsShadowMapping() {
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions) {
        std::string extStr(extensions);
        if (extStr.find("GL_ARB_depth_texture") != std::string::npos ||
            extStr.find("GL_EXT_depth_texture") != std::string::npos) {
            return true;
        }
    }
    
    // English comment.
    const std::string version = getOpenGLVersion();
    if (!version.empty()) {
        try {
            size_t dotPos = version.find('.');
            if (dotPos != std::string::npos) {
                float major = std::stof(version.substr(0, dotPos));
                float minor = std::stof(version.substr(dotPos + 1));
                if (major > 1 || (major == 1 && minor >= 4)) {
                    return true;
                }
            }
        } catch (...) {}
    }
    
    return false;
}

bool RenderCapabilities::supportsFloatTextures() {
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions) {
        std::string extStr(extensions);
        if (extStr.find("GL_ARB_texture_float") != std::string::npos ||
            extStr.find("GL_EXT_texture_float") != std::string::npos) {
            return true;
        }
    }
    
    // English comment.
    const std::string version = getOpenGLVersion();
    if (!version.empty()) {
        try {
            size_t dotPos = version.find('.');
            if (dotPos != std::string::npos) {
                float major = std::stof(version.substr(0, dotPos));
                float minor = std::stof(version.substr(dotPos + 1));
                if (major > 3 || (major == 3 && minor >= 3)) {
                    return true;
                }
            }
        } catch (...) {}
    }
    
    return false;
}

}  // namespace render
}  // namespace viewer3d
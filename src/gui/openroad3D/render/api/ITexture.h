#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace viewer3d {
namespace render {

/**
 * @brief English description.
 */
enum class TextureType {
    Texture2D,
    TextureCubeMap,
    Texture3D
};

/**
 * @brief English description.
 */
enum class TextureFormat {
    RGB,
    RGBA,
    Float,
    Depth
};

/**
 * @brief English description.
 */
class ITexture {
public:
    virtual ~ITexture() = default;
    
    /**
     * @brief English description.
     */
    virtual unsigned int getTextureId() const = 0;
    
    /**
     * @brief English description.
     */
    virtual TextureType getType() const = 0;
    
    /**
     * @brief English description.
     */
    virtual int getWidth() const = 0;
    
    /**
     * @brief English description.
     */
    virtual int getHeight() const = 0;
    
    /**
     * @brief English description.
     */
    virtual bool isValid() const = 0;
    
    /**
     * @brief English description.
     */
    virtual void bind(int unit = 0) = 0;
    
    /**
     * @brief English description.
     */
    virtual void unbind() = 0;
};

/**
 * @brief English description.
 */
struct TextureLoadParams {
    std::string filePath;
    bool generateMipmaps = true;
    bool sRGB = false;
    TextureFormat format = TextureFormat::RGBA;
    int Anisotropy = 4;  // English comment.
};

/**
 * @brief English description.
 */
class ITextureFactory {
public:
    virtual ~ITextureFactory() = default;
    
    /**
     * @brief English description.
     */
    virtual ITexture* loadTexture(const TextureLoadParams& params) = 0;
    
    /**
     * @brief English description.
     */
    virtual ITexture* createTexture(int width, int height, TextureFormat format, const uint8_t* data) = 0;
    
    /**
     * @brief English description.
     */
    virtual ITexture* createTexture(int width, int height, TextureFormat format) = 0;
    
    /**
     * @brief English description.
     */
    virtual void destroyTexture(ITexture* texture) = 0;
    
    /**
     * @brief English description.
     */
    virtual void setDefaultTexture(ITexture* texture) = 0;
    
    /**
     * @brief English description.
     */
    virtual ITexture* getDefaultTexture() = 0;
    
    /**
     * @brief English description.
     */
    virtual void clearCache() = 0;
};

}  // namespace render
}  // namespace viewer3d
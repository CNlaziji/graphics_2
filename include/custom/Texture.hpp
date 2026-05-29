#pragma once

/**
 * @file Texture.hpp
 * @brief 纹理与材质系统
 * 
 * 成员 C 负责模块，提供：
 *  - LoadTexture()   通用纹理加载函数（PNG / JPG → OpenGL 纹理对象）
 *  - Texture 类      封装单张 OpenGL 纹理的生命周期管理
 *  - Material 结构体 漫反射贴图 + 高光贴图 + 光泽度，支持多种预设材质类型
 * 
 * 与成员 B（Mesh / Model）的对接约定：
 *  - Mesh 在构造时调用 LoadTexture() 或直接使用 Texture 对象获取纹理 ID
 *  - 渲染前调用 Material::Bind(shader) 激活并绑定所有纹理单元
 * 
 * @author 成员 C
 */

#include "glad/glad.h"

// ── 补充 glad.h 中未包含的纹理相关 OpenGL 常量 ──────────────────────────────
// 本项目的 glad.h 是精简版，只含阴影管线所需常量。
// 以下常量值来自 OpenGL 4.6 Core Profile 规范，数值固定不变。
#ifndef GL_RED
#  define GL_RED                    0x1903
#endif
#ifndef GL_RGB
#  define GL_RGB                    0x1907
#endif
#ifndef GL_RGBA
#  define GL_RGBA                   0x1908
#endif
#ifndef GL_SRGB
#  define GL_SRGB                   0x8C40
#endif
#ifndef GL_SRGB_ALPHA
#  define GL_SRGB_ALPHA             0x8C42
#endif
#ifndef GL_REPEAT
#  define GL_REPEAT                 0x2901
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#  define GL_LINEAR_MIPMAP_LINEAR   0x2703
#endif
#ifndef GL_LINEAR_MIPMAP_NEAREST
#  define GL_LINEAR_MIPMAP_NEAREST  0x2701
#endif
#ifndef GL_TEXTURE3
#  define GL_TEXTURE3               0x84C3
#endif
// ─────────────────────────────────────────────────────────────────────────────

// stb_image 函数声明（实现在 src/stb_image_impl.cpp）
// 不直接 include stb_image.h，避免其拉入 <windows.h> 造成宏污染。
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char* stbi_load(const char* filename, int* x, int* y,
                                int* channels_in_file, int desired_channels);
extern void           stbi_image_free(void* retval_from_stbi_load);
extern const char*    stbi_failure_reason(void);
extern void           stbi_set_flip_vertically_on_load(int flag);
#ifdef __cplusplus
}
#endif

#include <string>
#include <unordered_map>
#include <iostream>
#include <stdexcept>

// ============================================================================
// 纹理类型枚举
// ============================================================================

/**
 * @enum TextureType
 * @brief 纹理在材质中的语义角色
 */
enum class TextureType {
    Diffuse,   ///< 漫反射贴图（基础颜色）
    Specular,  ///< 高光贴图（反射强度）
    Normal,    ///< 法线贴图（表面细节，可选）
    Emissive   ///< 自发光贴图（发光体材质）
};

// ============================================================================
// 核心纹理加载函数
// ============================================================================

/**
 * @brief 从文件路径加载图片并生成 OpenGL 纹理对象
 * 
 * 功能：
 *  - 使用 stb_image 读取 PNG / JPG / BMP / TGA 等常见格式
 *  - 自动翻转图像（OpenGL 纹理坐标原点在左下角）
 *  - 配置 GL_REPEAT 环绕 + GL_LINEAR_MIPMAP_LINEAR 过滤
 *  - 自动生成 Mipmap 链
 *  - 内置全局缓存，相同路径不重复上传显存
 * 
 * @param path       图片文件路径（相对于工作目录，如 "textures/wood.png"）
 * @param gammaCorrect 是否使用 sRGB 格式（漫反射贴图建议 true，法线/高光建议 false）
 * @return 成功返回 OpenGL 纹理 ID（> 0），失败返回 0
 */
inline unsigned int LoadTexture(const char* path, bool gammaCorrect = false) {
    // ---- 全局纹理缓存（避免重复加载相同文件）----
    static std::unordered_map<std::string, unsigned int> s_cache;

    std::string key = std::string(path) + (gammaCorrect ? "_srgb" : "_linear");
    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
        return it->second;  // 命中缓存，直接返回已有 ID
    }

    // ---- 翻转图像（OpenGL UV 原点在左下角，图片文件原点在左上角）----
    stbi_set_flip_vertically_on_load(true);

    // ---- 读取图片数据 ----
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0);

    if (!data) {
        std::cerr << "[Texture] Failed to load: " << path
                  << " | Reason: " << stbi_failure_reason() << std::endl;
        // 返回 0 表示加载失败，调用方应做容错处理
        return 0;
    }

    // ---- 根据通道数选择 OpenGL 内部格式 ----
    GLenum internalFormat = GL_RGB;
    GLenum dataFormat     = GL_RGB;

    if (channels == 1) {
        internalFormat = GL_RED;
        dataFormat     = GL_RED;
    } else if (channels == 3) {
        internalFormat = gammaCorrect ? GL_SRGB : GL_RGB;
        dataFormat     = GL_RGB;
    } else if (channels == 4) {
        internalFormat = gammaCorrect ? GL_SRGB_ALPHA : GL_RGBA;
        dataFormat     = GL_RGBA;
    }

    // ---- 生成并配置 OpenGL 纹理对象 ----
    unsigned int textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // 上传像素数据到显存
    glTexImage2D(
        GL_TEXTURE_2D,
        0,               // Mipmap 层级 0（基础层）
        internalFormat,  // 显存中的存储格式
        width,
        height,
        0,               // border（必须为 0）
        dataFormat,      // 内存中的像素格式
        GL_UNSIGNED_BYTE,
        data
    );

    // 自动生成完整 Mipmap 链（提升远距离渲染质量，减少摩尔纹）
    glGenerateMipmap(GL_TEXTURE_2D);

    // ---- 纹理环绕方式：GL_REPEAT（平铺重复，适合大多数材质）----
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // ---- 纹理过滤方式 ----
    // 缩小过滤：GL_LINEAR_MIPMAP_LINEAR（三线性过滤，最高质量）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // 放大过滤：GL_LINEAR（双线性插值，平滑放大）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // ---- 释放 CPU 端图片内存 ----
    stbi_image_free(data);

    // ---- 写入缓存 ----
    s_cache[key] = textureID;

    std::cout << "[Texture] Loaded: " << path
              << " (" << width << "x" << height << ", " << channels << "ch)"
              << " ID=" << textureID << std::endl;

    return textureID;
}

// ============================================================================
// Texture 类：RAII 封装单张纹理
// ============================================================================

/**
 * @class Texture
 * @brief 封装一个 OpenGL 2D 纹理对象，管理其生命周期
 * 
 * 使用示例：
 * @code
 *   Texture woodTex("textures/wood.png", TextureType::Diffuse);
 *   woodTex.Bind(GL_TEXTURE0);  // 绑定到纹理单元 0
 * @endcode
 */
class Texture {
public:
    /**
     * @brief 从文件加载纹理
     * @param path         图片路径
     * @param type         纹理语义类型（Diffuse / Specular / Normal / Emissive）
     * @param gammaCorrect 漫反射贴图建议开启 sRGB 校正
     */
    Texture(const std::string& path, TextureType type = TextureType::Diffuse,
            bool gammaCorrect = false)
        : m_path(path)
        , m_type(type)
        , m_id(LoadTexture(path.c_str(), gammaCorrect))
    {}

    /**
     * @brief 从已有 OpenGL 纹理 ID 构造（用于程序生成纹理）
     */
    explicit Texture(unsigned int id, TextureType type = TextureType::Diffuse)
        : m_path("<generated>")
        , m_type(type)
        , m_id(id)
    {}

    // 禁止拷贝（纹理 ID 由 LoadTexture 缓存管理，不做引用计数）
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // 允许移动
    Texture(Texture&& other) noexcept
        : m_path(std::move(other.m_path))
        , m_type(other.m_type)
        , m_id(other.m_id)
    {
        other.m_id = 0;
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            m_path = std::move(other.m_path);
            m_type = other.m_type;
            m_id   = other.m_id;
            other.m_id = 0;
        }
        return *this;
    }

    /**
     * @brief 激活指定纹理单元并绑定本纹理
     * @param unit 纹理单元，如 GL_TEXTURE0、GL_TEXTURE1 等
     */
    void Bind(GLenum unit = GL_TEXTURE0) const {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, m_id);
    }

    /** @brief 解绑当前纹理单元 */
    static void Unbind(GLenum unit = GL_TEXTURE0) {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    unsigned int  GetID()   const { return m_id;   }
    TextureType   GetType() const { return m_type;  }
    const std::string& GetPath() const { return m_path; }

    /** @brief 纹理是否加载成功 */
    bool IsValid() const { return m_id != 0; }

private:
    std::string  m_path;
    TextureType  m_type;
    unsigned int m_id = 0;
};

// ============================================================================
// Material 结构体：漫反射 + 高光 + 光泽度
// ============================================================================

/**
 * @struct Material
 * @brief 描述一个表面的光照响应属性
 * 
 * 与着色器约定的 Uniform 名称：
 *  - material.diffuse   → sampler2D，纹理单元 0
 *  - material.specular  → sampler2D，纹理单元 1
 *  - material.emissive  → sampler2D，纹理单元 2（可选）
 *  - material.shininess → float
 * 
 * 使用示例：
 * @code
 *   Material mat = Material::CreateWood();
 *   mat.Bind(shader);
 *   // 渲染调用...
 * @endcode
 */
struct Material {
    // ---- 纹理贴图 ----
    unsigned int diffuseMap  = 0;  ///< 漫反射贴图 ID（纹理单元 0）
    unsigned int specularMap = 0;  ///< 高光贴图 ID（纹理单元 1）
    unsigned int emissiveMap = 0;  ///< 自发光贴图 ID（纹理单元 2，可选）

    // ---- 标量参数 ----
    float shininess = 32.0f;       ///< 高光光泽度（越大高光越集中）

    // ---- 颜色回退（无贴图时使用）----
    float diffuseColor[3]  = {1.0f, 1.0f, 1.0f};  ///< 漫反射颜色（白色）
    float specularColor[3] = {0.5f, 0.5f, 0.5f};  ///< 高光颜色（灰色）

    /**
     * @brief 将材质参数绑定到着色器 Uniform
     * 
     * 调用前需确保着色器已通过 shader.use() 激活。
     * 
     * @tparam ShaderT 任意提供 setInt / setFloat 接口的着色器类
     * @param shader 当前激活的着色器对象
     */
    template<typename ShaderT>
    void Bind(const ShaderT& shader) const {
        // 漫反射贴图 → 纹理单元 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseMap);
        shader.setInt("material.diffuse", 0);

        // 高光贴图 → 纹理单元 1
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, specularMap != 0 ? specularMap : diffuseMap);
        shader.setInt("material.specular", 1);

        // 自发光贴图 → 纹理单元 2（若有）
        if (emissiveMap != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, emissiveMap);
            shader.setInt("material.emissive", 2);
        }

        // 光泽度
        shader.setFloat("material.shininess", shininess);
    }

    // =========================================================================
    // 预设材质工厂方法
    // =========================================================================

    /**
     * @brief 木头材质（漫反射为主，低光泽）
     * 
     * 特征：温暖的棕色调，粗糙表面，几乎无镜面高光
     */
    static Material CreateWood() {
        Material mat;
        mat.diffuseMap  = LoadTexture("textures/wood_diffuse.png",  true);
        mat.specularMap = LoadTexture("textures/wood_specular.png", false);
        mat.shininess   = 8.0f;   // 低光泽，木头表面粗糙
        return mat;
    }

    /**
     * @brief 石料材质（粗糙，几乎无高光）
     * 
     * 特征：灰色调，高粗糙度，极低镜面反射
     */
    static Material CreateStone() {
        Material mat;
        mat.diffuseMap  = LoadTexture("textures/stone_diffuse.png",  true);
        mat.specularMap = LoadTexture("textures/stone_specular.png", false);
        mat.shininess   = 4.0f;   // 极低光泽，石料表面粗糙
        return mat;
    }

    /**
     * @brief 金属材质（高反光，强镜面高光）
     * 
     * 特征：冷色调，光滑表面，强烈镜面反射
     */
    static Material CreateMetal() {
        Material mat;
        mat.diffuseMap  = LoadTexture("textures/metal_diffuse.png",  true);
        mat.specularMap = LoadTexture("textures/metal_specular.png", false);
        mat.shininess   = 128.0f; // 高光泽，金属表面光滑
        return mat;
    }

    /**
     * @brief 发光体材质（自发光，不受光照影响）
     * 
     * 特征：自身发光，常用于灯具、魔法阵、能量核心等
     */
    static Material CreateEmissive() {
        Material mat;
        mat.diffuseMap   = LoadTexture("textures/emissive_diffuse.png",  true);
        mat.specularMap  = LoadTexture("textures/emissive_specular.png", false);
        mat.emissiveMap  = LoadTexture("textures/emissive_glow.png",     true);
        mat.shininess    = 64.0f;
        return mat;
    }

    /**
     * @brief 地板材质（石砖/混凝土，适合室内地面）
     */
    static Material CreateFloor() {
        Material mat;
        mat.diffuseMap  = LoadTexture("textures/floor_diffuse.png",  true);
        mat.specularMap = LoadTexture("textures/floor_specular.png", false);
        mat.shininess   = 16.0f;
        return mat;
    }

    /**
     * @brief 从自定义路径创建材质（通用工厂）
     * 
     * @param diffusePath  漫反射贴图路径
     * @param specularPath 高光贴图路径（传空字符串则复用漫反射贴图）
     * @param shininess    光泽度
     * @param gammaCorrect 漫反射贴图是否做 sRGB 校正
     */
    static Material CreateCustom(
        const char* diffusePath,
        const char* specularPath = "",
        float       shininess    = 32.0f,
        bool        gammaCorrect = true)
    {
        Material mat;
        mat.diffuseMap  = LoadTexture(diffusePath, gammaCorrect);
        mat.specularMap = (specularPath && specularPath[0] != '\0')
                          ? LoadTexture(specularPath, false)
                          : mat.diffuseMap;
        mat.shininess   = shininess;
        return mat;
    }
};

// ============================================================================
// 纹理工具函数
// ============================================================================

/**
 * @brief 生成一张纯色 1×1 纹理（用于材质无贴图时的占位符）
 * 
 * @param r, g, b, a  RGBA 颜色分量（0–255）
 * @return OpenGL 纹理 ID
 */
inline unsigned int CreateSolidColorTexture(
    unsigned char r, unsigned char g,
    unsigned char b, unsigned char a = 255)
{
    unsigned int texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    unsigned char pixel[4] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return texID;
}

/**
 * @brief 获取白色占位纹理（漫反射默认值）
 * 
 * 单例模式，整个程序生命周期只创建一次。
 */
inline unsigned int GetWhiteTexture() {
    static unsigned int s_white = 0;
    if (s_white == 0) {
        s_white = CreateSolidColorTexture(255, 255, 255, 255);
    }
    return s_white;
}

/**
 * @brief 获取黑色占位纹理（高光/自发光默认值）
 */
inline unsigned int GetBlackTexture() {
    static unsigned int s_black = 0;
    if (s_black == 0) {
        s_black = CreateSolidColorTexture(0, 0, 0, 255);
    }
    return s_black;
}

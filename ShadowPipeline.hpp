#pragma once

#include "glad/glad.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "stdexcept"
#include "string"
#include "iostream"

// 阴影深度图分辨率配置
#define SHADOW_MAP_WIDTH 2048
#define SHADOW_MAP_HEIGHT 2048

// 光源视锥体配置（正交投影参数）
#define LIGHT_ORTHO_SIZE 10.0f
#define LIGHT_NEAR_PLANE 1.0f
#define LIGHT_FAR_PLANE 50.0f

/**
 * @class ShadowPipeline
 * @brief 阴影映射渲染管线封装类
 * 
 * 该类负责管理阴影深度图的生成，包括：
 * - 创建专用的帧缓冲对象（FBO）
 * - 创建深度纹理贴图
 * - 配置深度纹理参数（过滤、环绕方式）
 * - 提供光源空间变换矩阵计算
 * - 管理双通路渲染中的阴影通路状态切换
 * 
 * 使用 RAII 机制管理 OpenGL 资源生命周期
 */
class ShadowPipeline {
public:
    /**
     * @brief 构造函数：初始化阴影渲染管线
     * 
     * 创建 FBO 和深度纹理，配置所有必要的 OpenGL 状态
     * @throws std::runtime_error 当 FBO 创建失败时抛出异常
     */
    ShadowPipeline() {
        // 生成帧缓冲对象（FBO）
        glGenFramebuffers(1, &m_fbo);
        
        // 生成深度纹理贴图
        glGenTextures(1, &m_depthTexture);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        
        // 分配深度纹理存储空间（仅深度，无颜色）
        glTexImage2D(
            GL_TEXTURE_2D, 
            0, 
            GL_DEPTH_COMPONENT, 
            SHADOW_MAP_WIDTH, 
            SHADOW_MAP_HEIGHT, 
            0, 
            GL_DEPTH_COMPONENT, 
            GL_FLOAT, 
            nullptr
        );
        
        // 配置纹理过滤参数：使用最近邻采样，避免插值导致的深度精度问题
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        
        // 配置纹理环绕方式：GL_CLAMP_TO_BORDER 防止阴影贴图外的区域产生错误阴影
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        
        // 设置边界颜色为全白（1.0f），表示超出阴影贴图范围的区域无阴影
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        
        // 绑定 FBO 并附加深度纹理
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, 
            GL_DEPTH_ATTACHMENT, 
            GL_TEXTURE_2D, 
            m_depthTexture, 
            0
        );
        
        // 显式关闭颜色缓冲区的读写操作（阴影贴图只需要深度信息）
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        
        // 检查 FBO 完整性
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Cleanup();
            throw std::runtime_error("ShadowPipeline Error: Framebuffer is not complete!");
        }
        
        // 恢复默认帧缓冲
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        std::cout << "[ShadowPipeline] Initialized successfully. "
                  << "Depth texture: " << SHADOW_MAP_WIDTH << "x" << SHADOW_MAP_HEIGHT << std::endl;
    }
    
    /**
     * @brief 析构函数：清理 OpenGL 资源
     * 
     * 遵循 RAII 原则，自动释放 FBO 和深度纹理
     */
    ~ShadowPipeline() {
        Cleanup();
    }
    
    // 禁止拷贝（OpenGL 资源不可拷贝）
    ShadowPipeline(const ShadowPipeline&) = delete;
    ShadowPipeline& operator=(const ShadowPipeline&) = delete;
    
    // 允许移动语义
    ShadowPipeline(ShadowPipeline&& other) noexcept
        : m_fbo(other.m_fbo)
        , m_depthTexture(other.m_depthTexture)
        , m_viewportWidth(other.m_viewportWidth)
        , m_viewportHeight(other.m_viewportHeight) {
        other.m_fbo = 0;
        other.m_depthTexture = 0;
    }
    
    ShadowPipeline& operator=(ShadowPipeline&& other) noexcept {
        if (this != &other) {
            Cleanup();
            m_fbo = other.m_fbo;
            m_depthTexture = other.m_depthTexture;
            m_viewportWidth = other.m_viewportWidth;
            m_viewportHeight = other.m_viewportHeight;
            other.m_fbo = 0;
            other.m_depthTexture = 0;
        }
        return *this;
    }
    
    /**
     * @brief 绑定阴影 FBO 用于写入深度信息（第一通路：Shadow Pass）
     * 
     * 此函数会：
     * 1. 保存当前视口尺寸
     * 2. 设置视口为阴影贴图分辨率
     * 3. 绑定阴影 FBO
     * 4. 清空深度缓冲
     */
    void BindForWriting() {
        // 保存当前视口尺寸以便后续恢复
        glGetIntegerv(GL_VIEWPORT, m_savedViewport);
        
        // 绑定阴影帧缓冲
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        
        // 设置视口为阴影贴图分辨率
        glViewport(0, 0, SHADOW_MAP_WIDTH, SHADOW_MAP_HEIGHT);
        
        // 清空深度缓冲（准备写入新的深度信息）
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    
    /**
     * @brief 解绑阴影 FBO 并恢复默认帧缓冲（切换到主场景渲染）
     * 
     * 此函数会：
     * 1. 恢复默认帧缓冲
     * 2. 恢复之前保存的视口尺寸
     */
    void UnbindForReading() {
        // 恢复默认帧缓冲
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // 恢复之前的视口尺寸
        glViewport(
            m_savedViewport[0], 
            m_savedViewport[1], 
            m_savedViewport[2], 
            m_savedViewport[3]
        );
    }
    
    /**
     * @brief 获取深度纹理 ID，用于传递给主场景着色器
     * @return 深度纹理的 OpenGL ID
     */
    unsigned int GetDepthTextureID() const {
        return m_depthTexture;
    }
    
    /**
     * @brief 计算光源空间变换矩阵（Light Space Matrix）
     * 
     * 该矩阵将世界坐标变换到光源视角的裁剪空间，用于：
     * - 第一通路：从光源视角渲染深度图
     * - 第二通路：将片元坐标变换到光源空间进行阴影检测
     * 
     * @param lightDir 光源方向（用于方向光）
     * @param lightPos 光源位置（可选，用于计算观察点）
     * @param target 光源观察目标点（默认为原点）
     * @return glm::mat4 光源空间变换矩阵（投影矩阵 * 观察矩阵）
     */
    glm::mat4 GetLightSpaceMatrix(
        const glm::vec3& lightDir,
        const glm::vec3& lightPos = glm::vec3(0.0f),
        const glm::vec3& target = glm::vec3(0.0f)
    ) const {
        // 如果未指定光源位置，根据方向计算一个合适的位置
        glm::vec3 actualLightPos = lightPos;
        if (glm::length(lightPos) < 0.001f) {
            // 默认将光源放置在距离原点一定距离的位置，沿光照方向的反方向
            actualLightPos = -lightDir * 20.0f;
        }
        
        // 计算光源的观察矩阵（LookAt）
        // 光源看向目标点，上向量默认为世界 Y 轴
        glm::mat4 lightView = glm::lookAt(
            actualLightPos,           // 光源位置
            target,                   // 观察目标
            glm::vec3(0.0f, 1.0f, 0.0f)  // 上向量
        );
        
        // 使用正交投影矩阵模拟方向光（平行光）
        // 正交投影适合方向光，因为方向光没有透视效果
        glm::mat4 lightProjection = glm::ortho(
            -LIGHT_ORTHO_SIZE,   // 左
            LIGHT_ORTHO_SIZE,    // 右
            -LIGHT_ORTHO_SIZE,   // 下
            LIGHT_ORTHO_SIZE,    // 上
            LIGHT_NEAR_PLANE,    // 近裁剪面
            LIGHT_FAR_PLANE      // 远裁剪面
        );
        
        // 光源空间矩阵 = 投影矩阵 * 观察矩阵
        return lightProjection * lightView;
    }
    
    /**
     * @brief 获取阴影贴图宽度
     */
    static constexpr int GetShadowMapWidth() { return SHADOW_MAP_WIDTH; }
    
    /**
     * @brief 获取阴影贴图高度
     */
    static constexpr int GetShadowMapHeight() { return SHADOW_MAP_HEIGHT; }

private:
    /**
     * @brief 清理 OpenGL 资源
     */
    void Cleanup() {
        if (m_depthTexture != 0) {
            glDeleteTextures(1, &m_depthTexture);
            m_depthTexture = 0;
        }
        if (m_fbo != 0) {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
    }

private:
    unsigned int m_fbo = 0;              // 帧缓冲对象 ID
    unsigned int m_depthTexture = 0;     // 深度纹理 ID
    
    // 用于保存/恢复视口尺寸
    GLint m_savedViewport[4] = {0, 0, 1280, 720};
    
    // 视口尺寸（用于 Unbind 时恢复）
    int m_viewportWidth = 1280;
    int m_viewportHeight = 720;
};

/**
 * @file main.cpp
 * @brief 计算机图形学实验 - 核心双通路渲染管线与实时阴影映射
 * 
 * 本文件实现了基于 OpenGL 3.3 Core Profile 的双通路阴影渲染系统：
 * - 第一通路（Shadow Pass）：从光源视角渲染深度图
 * - 第二通路（Lighting Pass）：主场景渲染，包含光照计算和阴影检测
 * 
 * @author 项目组长
 * @note 本文件作为核心骨架，预留了与组员代码集成的接口
 */

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <iostream>
#include <stdexcept>
#include <memory>

// 核心阴影管线封装
#include "ShadowPipeline.hpp"

// 模型加载系统（成员B）
#include "Model.hpp"

// ============================================================================
// 窗口配置
// ============================================================================
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr const char* WINDOW_TITLE = "Computer Graphics Lab - Shadow Mapping";

// ============================================================================
// 前向声明：着色器类占位（由组员实现或集成）
// ============================================================================

// ============================================================================
// 内嵌着色器源码（无需外部 shader 文件即可运行）
// ============================================================================

// 阴影深度顶点着色器
const char* kShadowVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)glsl";

// 阴影深度片元着色器（仅写深度，无颜色输出）
const char* kShadowFragSrc = R"glsl(
#version 330 core
void main() {
    // 深度自动写入，无需手动输出颜色
}
)glsl";

// 主场景顶点着色器
const char* kSceneVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position = projection * view * worldPos;
}
)glsl";

// 主场景片元着色器：Blinn-Phong 光照 + PCF 阴影
const char* kSceneFragSrc = R"glsl(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;

uniform sampler2D shadowMap;
uniform vec3 cameraPos;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight dirLight;

struct PointLight {
    vec3 position;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
};
uniform PointLight pointLight;

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
};
uniform SpotLight spotLight;

uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float shininess;
uniform vec3 objectColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = 0.005;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir) {
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    return light.color * light.intensity * (diff + spec);
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir) {
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return light.color * attenuation * (diff + spec);
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir) {
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    return light.color * attenuation * intensity * (diff + spec);
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    vec3 result = ambientColor * ambientIntensity;
    float shadow = ShadowCalculation(FragPosLightSpace);
    result += (1.0 - shadow) * CalcDirLight(dirLight, norm, viewDir);
    result += CalcPointLight(pointLight, norm, FragPos, viewDir);
    result += CalcSpotLight(spotLight, norm, FragPos, viewDir);
    result *= objectColor;
    FragColor = vec4(result, 1.0);
}
)glsl";

/**
 * @class Shader
 * @brief 着色器程序封装
 *
 * 优先从文件加载着色器，若文件不存在则使用内嵌默认着色器。
 * 内嵌着色器支持：shadow_depth(.vert/.frag), scene(.vert/.frag)
 */
class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath) : m_id(0) {
        std::cout << "[Shader] 加载: " << vertexPath << ", " << fragmentPath << std::endl;

        std::string vSrc, fSrc;
        std::string vPath(vertexPath), fPath(fragmentPath);

        // 尝试从文件读取，失败则使用内嵌默认着色器
        vSrc = ReadFileOrEmbed(vPath);
        fSrc = ReadFileOrEmbed(fPath);
        if (vSrc.empty() || fSrc.empty()) {
            std::cerr << "[Shader] 错误：无法获取着色器源码" << std::endl;
            m_id = glCreateProgram();
            return;
        }

        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vSrc);
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fSrc);

        if (vert == 0 || frag == 0) {
            if (vert) glDeleteShader(vert);
            if (frag) glDeleteShader(frag);
            m_id = glCreateProgram();
            return;
        }

        m_id = glCreateProgram();
        glAttachShader(m_id, vert);
        glAttachShader(m_id, frag);
        glLinkProgram(m_id);

        GLint success;
        glGetProgramiv(m_id, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(m_id, 512, nullptr, infoLog);
            std::cerr << "[Shader] 链接失败:\n" << infoLog << std::endl;
            glDeleteProgram(m_id);
            m_id = glCreateProgram();
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    ~Shader() {
        if (m_id != 0) glDeleteProgram(m_id);
    }

    void use() const { glUseProgram(m_id); }

    bool IsValid() const {
        if (m_id == 0) return false;
        GLint linked = 0;
        glGetProgramiv(m_id, GL_LINK_STATUS, &linked);
        return linked == GL_TRUE;
    }

    unsigned int GetID() const { return m_id; }

    void setMat4(const std::string& name, const glm::mat4& value) const {
        GLint loc = glGetUniformLocation(m_id, name.c_str());
        if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    }

    void setVec3(const std::string& name, const glm::vec3& value) const {
        GLint loc = glGetUniformLocation(m_id, name.c_str());
        if (loc != -1) glUniform3fv(loc, 1, glm::value_ptr(value));
    }

    void setVec3(const std::string& name, float x, float y, float z) const {
        setVec3(name, glm::vec3(x, y, z));
    }

    void setFloat(const std::string& name, float value) const {
        GLint loc = glGetUniformLocation(m_id, name.c_str());
        if (loc != -1) glUniform1f(loc, value);
    }

    void setInt(const std::string& name, int value) const {
        GLint loc = glGetUniformLocation(m_id, name.c_str());
        if (loc != -1) glUniform1i(loc, value);
    }

private:
    unsigned int m_id;

    unsigned int CompileShader(GLenum type, const std::string& source) {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "[Shader] 编译失败 ("
                      << (type == GL_VERTEX_SHADER ? "顶点" : "片元") << "):\n"
                      << infoLog << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    std::string ReadFileOrEmbed(const std::string& path) {
        // 尝试读取外部文件
        std::ifstream file(path);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            std::cout << "[Shader] 从文件读取: " << path << std::endl;
            return ss.str();
        }

        // 文件不存在，使用内嵌默认着色器
        std::cout << "[Shader] 使用内嵌默认着色器: " << path << std::endl;

        // 提取文件名（不含路径）
        size_t slash = path.find_last_of("/\\");
        std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;

        if (filename.find("shadow_depth.vert") != std::string::npos) return kShadowVertSrc;
        if (filename.find("shadow_depth.frag") != std::string::npos) return kShadowFragSrc;
        if (filename.find("scene.vert") != std::string::npos) return kSceneVertSrc;
        if (filename.find("scene.frag") != std::string::npos) return kSceneFragSrc;

        std::cerr << "[Shader] 未知着色器文件: " << path << std::endl;
        return "";
    }
};

// ============================================================================
// 模型类：已由 Model.hpp 提供完整实现（成员B）
// 详见 graphics_2/Model.hpp
// ============================================================================

// ============================================================================
// 前向声明：相机类占位（由组员实现或集成）
// ============================================================================

/**
 * @class Camera
 * @brief 相机控制封装（占位符，实际由组员提供完整实现）
 * 
 * 预期接口：
 * - glm::mat4 GetViewMatrix()  // 获取观察矩阵
 * - glm::vec3 GetPosition()    // 获取相机位置
 * - void ProcessInput(GLFWwindow* window, float deltaTime)  // 处理输入
 */
class Camera {
public:
    glm::mat4 GetViewMatrix() const {
        // TODO: 组员实现相机观察矩阵计算
        // 临时返回一个默认观察矩阵
        return glm::lookAt(
            glm::vec3(0.0f, 5.0f, 10.0f),  // 相机位置
            glm::vec3(0.0f, 0.0f, 0.0f),   // 观察目标
            glm::vec3(0.0f, 1.0f, 0.0f)    // 上向量
        );
    }
    
    glm::vec3 GetPosition() const {
        // TODO: 组员返回实际相机位置
        return glm::vec3(0.0f, 5.0f, 10.0f);
    }
    
    void ProcessInput(GLFWwindow* window, float deltaTime) {
        // TODO: 组员实现相机移动控制（WASD、鼠标视角等）
        (void)window;
        (void)deltaTime;
    }
};

// ============================================================================
// 全局变量
// ============================================================================

// GLFW 窗口句柄
GLFWwindow* g_window = nullptr;

// 核心渲染组件
std::unique_ptr<ShadowPipeline> g_shadowPipeline;
std::unique_ptr<Shader> g_shadowShader;  // 第一通路：深度着色器
std::unique_ptr<Shader> g_sceneShader;   // 第二通路：主场景着色器
std::unique_ptr<Camera> g_camera;

// 场景模型（示例）
// 注：已替换为 g_sceneObjects 容器（见下方成员B场景物体管理）

// 光源参数
struct DirectionalLight {
    glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 0.95f);  // 略微偏暖的白光
    float intensity = 1.0f;
} g_dirLight;

struct PointLight {
    glm::vec3 position = glm::vec3(2.0f, 4.0f, 2.0f);
    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.6f);   // 暖色点光源
    float constant = 1.0f;    // 常数衰减
    float linear = 0.09f;     // 线性衰减
    float quadratic = 0.032f; // 二次衰减
} g_pointLight;

struct SpotLight {
    glm::vec3 position = glm::vec3(0.0f, 8.0f, 0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 color = glm::vec3(0.9f, 0.9f, 1.0f);   // 略微偏冷的聚光灯
    float cutOff = glm::cos(glm::radians(12.5f));        // 内切角
    float outerCutOff = glm::cos(glm::radians(17.5f));   // 外切角
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
} g_spotLight;

// ============================================================================
// 场景模型系统（成员B）
// ============================================================================

/// 场景物体：模型 + 世界变换 + 材质颜色
struct SceneObject {
    std::unique_ptr<Model> model;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // 欧拉角（度数）
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec3 color = glm::vec3(0.7f);     // 物体颜色

    glm::mat4 GetModelMatrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        return m;
    }
};

std::vector<SceneObject> g_sceneObjects;   ///< 场景所有物体

// 时间控制
float g_deltaTime = 0.0f;
float g_lastFrame = 0.0f;

// ============================================================================
// 函数声明
// ============================================================================

bool InitializeGLFW();
bool InitializeOpenGL();
bool InitializeResources();
void Shutdown();
void ProcessInput();
void UpdateDeltaTime();
void RenderScene(Shader& shader);
void ShadowPass();
void LightingPass();
void RenderLoop();
void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

// ============================================================================
// 主函数
// ============================================================================

int main() {
    // 设置控制台输出为 UTF-8（解决 PowerShell/CMD 中文乱码）
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    try {
        // 1. 初始化 GLFW
        if (!InitializeGLFW()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        
        // 2. 初始化 OpenGL 上下文
        if (!InitializeOpenGL()) {
            throw std::runtime_error("Failed to initialize OpenGL");
        }
        
        // 3. 初始化资源（着色器、模型、阴影管线等）
        if (!InitializeResources()) {
            throw std::runtime_error("Failed to initialize resources");
        }
        
        // 4. 进入渲染主循环
        RenderLoop();
        
    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        Shutdown();
        return -1;
    }
    
    // 5. 清理并退出
    Shutdown();
    return 0;
}

// ============================================================================
// 初始化函数实现
// ============================================================================

bool InitializeGLFW() {
    // 初始化 GLFW 库
    if (!glfwInit()) {
        std::cerr << "[Error] Failed to initialize GLFW" << std::endl;
        return false;
    }
    
    // 配置 GLFW：OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // 创建窗口
    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    if (!g_window) {
        std::cerr << "[Error] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    // 设置当前上下文
    glfwMakeContextCurrent(g_window);
    
    // 注册窗口大小变化回调
    glfwSetFramebufferSizeCallback(g_window, FramebufferSizeCallback);
    
    // 启用垂直同步
    glfwSwapInterval(1);
    
    std::cout << "[GLFW] Initialized successfully" << std::endl;
    return true;
}

bool InitializeOpenGL() {
    // 初始化 GLAD：加载 OpenGL 函数指针
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[Error] Failed to initialize GLAD" << std::endl;
        return false;
    }
    
    // 配置 OpenGL 全局状态
    
    // 启用深度测试（必须，用于正确的遮挡关系）
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);  // 深度值小于当前值时通过测试
    
    // 设置视口
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // 设置清除颜色（天空色）
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);  // 天蓝色
    
    std::cout << "[OpenGL] Context initialized successfully" << std::endl;
    std::cout << "[OpenGL] Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "[OpenGL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    
    return true;
}

bool InitializeResources() {
    try {
        // 1. 初始化阴影渲染管线（核心组件 - 组长已完成）
        g_shadowPipeline = std::make_unique<ShadowPipeline>();
        
        // 2. 加载着色器（路径需根据实际项目结构调整）
        // 第一通路：阴影深度着色器（仅需顶点着色器）
        g_shadowShader = std::make_unique<Shader>(
            "shaders/shadow_depth.vert",  // 顶点着色器路径
            "shaders/shadow_depth.frag"   // 片元着色器路径（可为空）
        );
        
        // 第二通路：主场景着色器（完整光照+阴影）
        g_sceneShader = std::make_unique<Shader>(
            "shaders/scene.vert",   // 顶点着色器路径
            "shaders/scene.frag"    // 片元着色器路径
        );
        
        // 3. 验证着色器
        if (!g_shadowShader->IsValid()) {
            std::cerr << "[Error] 阴影着色器编译/链接失败!" << std::endl;
        }
        if (!g_sceneShader->IsValid()) {
            std::cerr << "[Error] 场景着色器编译/链接失败! 程序ID="
                      << g_sceneShader->GetID() << std::endl;
        } else {
            std::cout << "[Shader] 场景着色器验证通过 (ID="
                      << g_sceneShader->GetID() << ")" << std::endl;
        }

        // 4. 初始化相机
        g_camera = std::make_unique<Camera>();
        
        // ============================================================================
        // [插槽 0] 成员 B - 模型资源初始化
        // ============================================================================
        // 使用程序化几何体创建 5+ 个场景模型
        // 坐标约定：Y轴朝上，XZ为地面平面

        std::cout << "\n[成员B] 初始化场景模型..." << std::endl;

        // 1. 地面 - 大型平面（接受阴影）
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreatePlane()),
            glm::vec3(0.0f, -1.0f, 0.0f),   // 位置：原点下方
            glm::vec3(0.0f),
            glm::vec3(20.0f, 1.0f, 20.0f),  // 20x20大平面
            glm::vec3(0.5f, 0.5f, 0.5f)     // 灰色地面
        });
        std::cout << "  [1/7] 地面平面已创建" << std::endl;

        // 2. 主体建筑 - 大型立方体
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCube()),
            glm::vec3(0.0f, 0.0f, 0.0f),    // 原点位置
            glm::vec3(0.0f),
            glm::vec3(2.0f, 3.0f, 2.0f),    // 宽2 高3 深2
            glm::vec3(0.8f, 0.6f, 0.4f)     // 沙色建筑
        });
        std::cout << "  [2/7] 主体建筑已创建" << std::endl;

        // 3. 建筑顶部 - 四棱锥屋顶
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreatePyramid()),
            glm::vec3(0.0f, 2.0f, 0.0f),    // 建筑顶部
            glm::vec3(0.0f),
            glm::vec3(1.5f, 1.5f, 1.5f),
            glm::vec3(0.7f, 0.2f, 0.1f)     // 红棕色屋顶
        });
        std::cout << "  [3/7] 金字塔屋顶已创建" << std::endl;

        // 4. 柱子（圆柱体）x2 - 建筑前方两侧
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCylinder(24)),
            glm::vec3(-2.5f, -0.2f, 2.0f),
            glm::vec3(0.0f),
            glm::vec3(0.3f, 2.0f, 0.3f),
            glm::vec3(0.9f, 0.9f, 0.8f)     // 浅色石柱
        });
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCylinder(24)),
            glm::vec3(2.5f, -0.2f, 2.0f),
            glm::vec3(0.0f),
            glm::vec3(0.3f, 2.0f, 0.3f),
            glm::vec3(0.9f, 0.9f, 0.8f)
        });
        std::cout << "  [4/7] 石柱×2 已创建" << std::endl;

        // 5. 装饰球体 - 建筑前方
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateSphere(36, 18)),
            glm::vec3(0.0f, 0.5f, 3.0f),
            glm::vec3(0.0f),
            glm::vec3(0.6f, 0.6f, 0.6f),
            glm::vec3(0.3f, 0.5f, 0.8f)     // 蓝紫色球体
        });
        std::cout << "  [5/7] 装饰球体已创建" << std::endl;

        // 6. 道具箱子 - 小立方体（建筑侧面）
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCube()),
            glm::vec3(3.5f, -0.3f, 0.0f),
            glm::vec3(0.0f, 30.0f, 0.0f),   // 略微旋转
            glm::vec3(0.8f, 0.6f, 0.8f),
            glm::vec3(0.6f, 0.3f, 0.2f)     // 棕色箱子
        });
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCube()),
            glm::vec3(3.5f, 0.3f, 0.0f),
            glm::vec3(0.0f, 15.0f, 0.0f),
            glm::vec3(0.7f, 0.5f, 0.7f),
            glm::vec3(0.6f, 0.3f, 0.2f)
        });
        std::cout << "  [6/7] 道具箱子×2 已创建" << std::endl;

        // 7. 后方装饰 - 圆锥（树/尖塔）
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCone(24)),
            glm::vec3(-3.0f, -0.5f, -2.5f),
            glm::vec3(0.0f),
            glm::vec3(0.5f, 1.5f, 0.5f),
            glm::vec3(0.2f, 0.7f, 0.3f)     // 绿色圆锥
        });
        g_sceneObjects.push_back({
            std::make_unique<Model>(Model::CreateCone(24)),
            glm::vec3(3.0f, -0.5f, -2.5f),
            glm::vec3(0.0f),
            glm::vec3(0.5f, 1.5f, 0.5f),
            glm::vec3(0.2f, 0.7f, 0.3f)
        });
        std::cout << "  [7/7] 装饰圆锥×2 已创建" << std::endl;

        std::cout << "[成员B] 场景模型初始化完成，共 "
                  << g_sceneObjects.size() << " 个物体\n" << std::endl;

        // 注：若存在 .obj 模型文件，可通过以下方式加载：
        // g_sceneObjects.push_back({
        //     std::make_unique<Model>(Model("models/building.obj")),
        //     glm::vec3(0.0f, 0.0f, 0.0f),
        //     glm::vec3(0.0f),
        //     glm::vec3(1.0f),
        //     glm::vec3(0.7f)
        // });
        
        std::cout << "[Resources] All resources initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Error] Resource initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Shutdown() {
    // 清理场景物体
    g_sceneObjects.clear();

    // 智能指针会自动清理资源
    g_camera.reset();
    g_sceneShader.reset();
    g_shadowShader.reset();
    g_shadowPipeline.reset();
    
    // 销毁 GLFW 窗口
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    
    // 终止 GLFW
    glfwTerminate();
    
    std::cout << "[Shutdown] Resources cleaned up successfully" << std::endl;
}

// ============================================================================
// 渲染循环相关函数
// ============================================================================

void RenderLoop() {
    std::cout << "[RenderLoop] Starting main render loop..." << std::endl;

    // 帧率统计变量
    int   frameCount = 0;
    double lastFpsTime = glfwGetTime();

    while (!glfwWindowShouldClose(g_window)) {
        // 1. 计算帧时间
        UpdateDeltaTime();

        // 2. 处理输入
        ProcessInput();

        // ================================================================
        // 双通路渲染流程 (Dual-Pass Rendering)
        // ================================================================

        // 第一通路：生成阴影深度图
        ShadowPass();

        // 第二通路：主场景光照渲染（包含阴影计算）
        LightingPass();

        // ================================================================
        // FPS 诊断
        // ================================================================
        frameCount++;
        double now = glfwGetTime();
        if (now - lastFpsTime >= 1.0) {
            double fps = frameCount / (now - lastFpsTime);
            std::string title = "CG Lab2 - Shadow Mapping | FPS: "
                              + std::to_string(static_cast<int>(fps));
            glfwSetWindowTitle(g_window, title.c_str());
            std::cerr << "[FPS] " << static_cast<int>(fps) << std::endl;
            frameCount = 0;
            lastFpsTime = now;

            // 检查 OpenGL 错误
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                std::cerr << "[GL Error] 0x" << std::hex << err << std::dec << std::endl;
            }
        }
        
        // ============================================================================
        // [插槽 4] 成员 E - ImGui GUI 系统
        // ============================================================================
        // TODO: 在此渲染 ImGui 控制面板
        //
        // 任务清单：
        // 1. 集成 ImGui 库（下载并添加到项目）
        // 2. 在 CMakeLists.txt 中添加 ImGui 源文件
        // 3. 初始化 ImGui（在 InitializeResources 中）
        // 4. 每帧渲染 GUI 面板：
        //    - 环境光强度滑块 (0.0 - 1.0)
        //    - 环境光颜色选择器 (RGB)
        //    - 光源位置 XYZ 输入框/滑块
        //    - 光源颜色选择器
        //    - 阴影偏移参数调节
        //
        // 实现步骤：
        // ImGui_ImplOpenGL3_NewFrame();
        // ImGui_ImplGlfw_NewFrame();
        // ImGui::NewFrame();
        //
        // // 绘制控制面板
        // ImGui::Begin("Lighting Control");
        // ImGui::SliderFloat("Ambient Intensity", &ambientIntensity, 0.0f, 1.0f);
        // ImGui::ColorEdit3("Ambient Color", &ambientColor[0]);
        // ... 其他控件
        // ImGui::End();
        //
        // ImGui::Render();
        // ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        //
        // 参考：ImGui 官方示例 (examples/example_glfw_opengl3)
        // ============================================================================
        
        // 3. 交换缓冲区并轮询事件
        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }
    
    std::cout << "[RenderLoop] Render loop ended" << std::endl;
}

void UpdateDeltaTime() {
    float currentFrame = static_cast<float>(glfwGetTime());
    g_deltaTime = currentFrame - g_lastFrame;
    g_lastFrame = currentFrame;
}

void ProcessInput() {
    // 处理 ESC 键退出
    if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(g_window, true);
    }
    
    // ============================================================================
    // [插槽 3] 成员 D - 相机与交互系统
    // ============================================================================
    // TODO: 实现 FPS 漫游相机的完整输入处理
    //
    // 任务清单：
    // 1. WASD 键盘移动：
    //    - W: 前进，S: 后退，A: 左移，D: 右移
    //    - 移动速度 = baseSpeed * g_deltaTime
    //
    // 2. 鼠标视角控制：
    //    - 鼠标移动控制相机俯仰角(pitch)和偏航角(yaw)
    //    - 限制俯仰角范围 [-89°, 89°]，防止万向锁
    //    - 鼠标灵敏度可调（建议 0.1f）
    //
    // 3. 鼠标滚轮（可选）：
    //    - 调整视野角度(FOV)，实现缩放效果
    //
    // 实现建议：
    // - 在 Camera 类中实现 ProcessKeyboard() 和 ProcessMouseMovement()
    // - 使用 glfwGetKey() 检测键盘状态
    // - 使用 glfwSetCursorPosCallback() 设置鼠标回调
    // - 使用 glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED) 隐藏鼠标
    //
    // 参考：LearnOpenGL 的 Camera 章节
    // ============================================================================
    
    // 委托相机处理 WASD 和鼠标输入（当前为占位实现）
    if (g_camera) {
        g_camera->ProcessInput(g_window, g_deltaTime);
    }
}

// ============================================================================
// 双通路渲染核心实现
// ============================================================================

/**
 * @brief 第一通路：阴影深度图生成（Shadow Pass）
 * 
 * 此通路从光源视角渲染场景，仅记录深度信息到深度纹理。
 * 这是阴影映射算法的第一步。
 */
void ShadowPass() {
    // ------------------------------------------------------------------------
    // Step 1: 绑定阴影 FBO，准备写入深度信息
    // ------------------------------------------------------------------------
    g_shadowPipeline->BindForWriting();
    
    // ------------------------------------------------------------------------
    // Step 2: 激活阴影专用着色器
    // ------------------------------------------------------------------------
    g_shadowShader->use();
    
    // ------------------------------------------------------------------------
    // Step 3: 计算并传入光源空间变换矩阵
    // ------------------------------------------------------------------------
    glm::mat4 lightSpaceMatrix = g_shadowPipeline->GetLightSpaceMatrix(
        g_dirLight.direction,
        glm::vec3(0.0f),  // 光源位置（自动计算）
        glm::vec3(0.0f)   // 观察目标（原点）
    );
    g_shadowShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    
    // ------------------------------------------------------------------------
    // Step 4: 渲染场景（仅深度）
    // 
    // 注意：在此通路中，我们只关心片元的深度值，不需要颜色输出。
    // 阴影着色器的片元着色器可以留空或仅包含：
    // void main() { /* 不写颜色，仅更新深度缓冲 */ }
    // ------------------------------------------------------------------------
    RenderScene(*g_shadowShader);
    
    // ------------------------------------------------------------------------
    // Step 5: 解绑 FBO（恢复默认帧缓冲）
    // ------------------------------------------------------------------------
    g_shadowPipeline->UnbindForReading();
}

/**
 * @brief 第二通路：主场景光照渲染（Lighting Pass）
 * 
 * 此通路在正常相机视角下渲染场景，使用第一通路生成的深度纹理
 * 进行阴影检测，实现动态阴影效果。
 */
void LightingPass() {

    // ------------------------------------------------------------------------
    // Step 1: 恢复默认帧缓冲并设置视口
    // ------------------------------------------------------------------------
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // ------------------------------------------------------------------------
    // Step 2: 清除颜色和深度缓冲
    // ------------------------------------------------------------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ------------------------------------------------------------------------
    // Step 3: 激活主场景着色器
    // ------------------------------------------------------------------------
    g_sceneShader->use();
    
    // ------------------------------------------------------------------------
    // Step 4: 设置变换矩阵
    // ------------------------------------------------------------------------
    
    // 投影矩阵（透视投影）
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),                                    // FOV
        static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT,       // 宽高比
        0.1f,                                                   // 近裁剪面
        100.0f                                                  // 远裁剪面
    );
    
    // 观察矩阵（从相机视角）
    glm::mat4 view = g_camera->GetViewMatrix();
    
    // 模型矩阵（单位矩阵，可根据需要修改）
    glm::mat4 model = glm::mat4(1.0f);
    
    // 光源空间矩阵（用于片元着色器中的阴影坐标变换）
    glm::mat4 lightSpaceMatrix = g_shadowPipeline->GetLightSpaceMatrix(
        g_dirLight.direction
    );
    
    g_sceneShader->setMat4("projection", projection);
    g_sceneShader->setMat4("view", view);
    g_sceneShader->setMat4("model", model);
    g_sceneShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    
    // 传入相机位置（用于光照计算）
    g_sceneShader->setVec3("cameraPos", g_camera->GetPosition());
    
    // ------------------------------------------------------------------------
    // Step 5: 绑定阴影深度纹理
    // ------------------------------------------------------------------------
    
    // 激活纹理单元 1（纹理单元 0 通常用于漫反射贴图）
    glActiveTexture(GL_TEXTURE1);
    
    // 绑定深度纹理
    glBindTexture(GL_TEXTURE_2D, g_shadowPipeline->GetDepthTextureID());
    
    // 告知着色器阴影贴图位于纹理单元 1
    g_sceneShader->setInt("shadowMap", 1);

    // ------------------------------------------------------------------------
    // Step 6: 设置多光源参数
    // ------------------------------------------------------------------------

    // 方向光（产生阴影的光源）
    g_sceneShader->setVec3("dirLight.direction", g_dirLight.direction);
    g_sceneShader->setVec3("dirLight.color", g_dirLight.color);
    g_sceneShader->setFloat("dirLight.intensity", g_dirLight.intensity);
    
    // 点光源
    g_sceneShader->setVec3("pointLight.position", g_pointLight.position);
    g_sceneShader->setVec3("pointLight.color", g_pointLight.color);
    g_sceneShader->setFloat("pointLight.constant", g_pointLight.constant);
    g_sceneShader->setFloat("pointLight.linear", g_pointLight.linear);
    g_sceneShader->setFloat("pointLight.quadratic", g_pointLight.quadratic);
    
    // 聚光灯
    g_sceneShader->setVec3("spotLight.position", g_spotLight.position);
    g_sceneShader->setVec3("spotLight.direction", g_spotLight.direction);
    g_sceneShader->setVec3("spotLight.color", g_spotLight.color);
    g_sceneShader->setFloat("spotLight.cutOff", g_spotLight.cutOff);
    g_sceneShader->setFloat("spotLight.outerCutOff", g_spotLight.outerCutOff);
    g_sceneShader->setFloat("spotLight.constant", g_spotLight.constant);
    g_sceneShader->setFloat("spotLight.linear", g_spotLight.linear);
    g_sceneShader->setFloat("spotLight.quadratic", g_spotLight.quadratic);

    // ------------------------------------------------------------------------
    // Step 6.5: 设置环境光与材质参数
    // ------------------------------------------------------------------------
    g_sceneShader->setVec3("ambientColor", glm::vec3(1.0f, 1.0f, 1.0f));
    g_sceneShader->setFloat("ambientIntensity", 0.35f);  // 提高环境光，避免场景过暗
    g_sceneShader->setFloat("shininess", 32.0f);

    // ------------------------------------------------------------------------
    // Step 7: 渲染完整场景（带光照和阴影）
    // ------------------------------------------------------------------------
    RenderScene(*g_sceneShader);
}

/**
 * @brief 场景渲染占位函数
 * 
 * 此函数作为与组员代码集成的接口。
 * 组员应在此函数中实现实际的场景绘制逻辑，包括：
 * - 绘制地面/平面
 * - 绘制模型
 * - 绘制其他场景物体
 * 
 * @param shader 当前激活的着色器程序
 */
void RenderScene(Shader& shader) {
    // ============================================================================
    // [插槽 1 & 2] 成员 B - 模型渲染系统 + 成员C - 材质系统
    // ============================================================================

    for (const auto& obj : g_sceneObjects) {
        // 设置物体世界变换矩阵
        shader.setMat4("model", obj.GetModelMatrix());

        // 设置物体颜色（基础材质参数）
        shader.setVec3("objectColor", obj.color);

        // 绘制模型
        if (obj.model) {
            obj.model->Draw(shader);
        }
    }
}

// ============================================================================
// 回调函数
// ============================================================================

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    // 窗口大小变化时更新视口
    glViewport(0, 0, width, height);
    std::cout << "[Window] Resized to " << width << "x" << height << std::endl;
}

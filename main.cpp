/**
 * @file main.cpp
 * @brief 计算机图形学实验 - 核心双通路渲染管线与实时阴影映射
 * 
 * 本文件实现了基于 OpenGL 3.3 Core Profile 的双通路阴影渲染系统：
 * - 第一通路（Shadow Pass）：从光源视角渲染深度图
 * - 第二通路（Lighting Pass）：主场景渲染，包含光照计算和阴影检测
 * 
 * 室内场景：包含多类型光源（方向光、点光源、聚光灯）和阴影映射。
 */

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

// 成员 E：ImGui 库
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

// 核心阴影管线封装
#include "ShadowPipeline.hpp"

// 模型加载系统（成员B）
#include "custom/Model.hpp"

// ============================================================================
// 窗口配置
// ============================================================================
int g_winWidth = 1280;
int g_winHeight = 720;
constexpr const char* WINDOW_TITLE = "Computer Graphics Lab - Shadow Mapping";

// ============================================================================
// 内嵌着色器源码
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
out vec4 FragPosSpotLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform mat4 spotLightSpaceMatrix;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    FragPosSpotLightSpace = spotLightSpaceMatrix * worldPos;
    gl_Position = projection * view * worldPos;
}
)glsl";

// 主场景片元着色器：完整 Blinn-Phong + 背面遮蔽 + radius + 材质参数
const char* kSceneFragSrc = R"glsl(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;
in vec4 FragPosSpotLightSpace;

uniform sampler2D shadowMap;
uniform sampler2D spotShadowMap;
uniform sampler2D texture_diffuse;
uniform sampler2D texture_emissive;
uniform sampler2D sceneDepth;
uniform bool useTexture;
uniform bool hasEmissive;
uniform bool isLit;
uniform vec3 cameraPos;

uniform mat4 projection;
uniform mat4 view;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight dirLight;

#define MAX_POINT_LIGHTS 64

struct PointLight {
    vec3 position;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
    float radius;
};

uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;
uniform float pointLightMaster;

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    float specStrength;
};
uniform SpotLight spotLight;
uniform int spotLightEnabled;

uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform vec3 objectColor;
uniform float matShininess;
uniform float matSpecStrength;
uniform float shadowBias;
uniform int pcfRadius;

float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(depthMap, 0);
    int count = 0;
    for (int x = -pcfRadius; x <= pcfRadius; x++) {
        for (int y = -pcfRadius; y <= pcfRadius; y++) {
            float pcfDepth = texture(depthMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - shadowBias > pcfDepth ? 1.0 : 0.0;
            count++;
        }
    }
    return shadow / float(count);
}

float ScreenSpaceOcclusion(vec3 fragWorld, vec3 lightWorld, mat4 proj, mat4 view, sampler2D depthTex, vec2 fragCoord) {
    vec3 toLight = lightWorld - fragWorld;
    float totalDist = length(toLight);
    if (totalDist < 0.01) return 1.0;
    vec3 dir = toLight / totalDist;

    const int STEPS = 8;
    float stepSize = totalDist / float(STEPS);

    float hash = fract(sin(dot(fragCoord, vec2(127.1, 311.7))) * 43758.5453);

    int occludedSteps = 0;
    float rayT = stepSize * 0.2 + hash * stepSize * 0.6;

    for (int i = 0; i < STEPS; i++) {
        vec3 rayPos = fragWorld + dir * rayT;
        rayT += stepSize;
        if (rayT > totalDist) break;

        vec4 clip = proj * view * vec4(rayPos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sampledDepth = texture(depthTex, uv).r;
        float rayDepth = ndc.z * 0.5 + 0.5;
        if (sampledDepth < rayDepth - 0.003) {
            occludedSteps++;
        }
    }
    if (occludedSteps == 0) return 1.0;
    if (occludedSteps >= STEPS) return 0.0;
    return 1.0 - float(occludedSteps) / float(STEPS);
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, float shadow, float shininess, float specStrength) {
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 diffuse = light.color * light.intensity * diff * (1.0 - shadow);
    vec3 specular = light.color * light.intensity * spec * specStrength * (1.0 - shadow);
    return diffuse + specular;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess, float specStrength) {
    vec3 lightDir = normalize(light.position - fragPos);
    float NdotL = dot(normal, lightDir);
    float diff = max(NdotL, 0.0);
    float backFace = step(0.0, NdotL);
    if (backFace < 0.5) return vec3(0.0);

    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float windowAtten = 1.0 - smoothstep(light.radius * 0.7, light.radius, dist);
    attenuation *= windowAtten;
    if (attenuation < 0.001) return vec3(0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);

    vec3 diffuse  = light.color * attenuation * diff;
    vec3 specular = light.color * attenuation * spec * specStrength;
    float ssOcclusion = ScreenSpaceOcclusion(fragPos, light.position, projection, view, sceneDepth, gl_FragCoord.xy);
    return (diffuse + specular) * ssOcclusion;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess, float spotShadow) {
    vec3 lightDir = normalize(light.position - fragPos);
    float NdotL = dot(normal, lightDir);
    float diff = max(NdotL, 0.0);
    float backFace = step(0.0, NdotL);
    if (backFace < 0.5) return vec3(0.0);

    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    attenuation *= intensity;
    if (attenuation < 0.001) return vec3(0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);

    vec3 diffuse  = light.color * attenuation * diff * (1.0 - spotShadow);
    vec3 specular = light.color * attenuation * spec * light.specStrength * (1.0 - spotShadow);
    return diffuse + specular;
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    float shadow = ShadowCalculation(FragPosLightSpace, shadowMap);
    float spotShadow = ShadowCalculation(FragPosSpotLightSpace, spotShadowMap);

    vec3 lighting = vec3(0.0);

    lighting += CalcDirLight(dirLight, norm, viewDir, shadow, matShininess, matSpecStrength);

    for (int i = 0; i < numPointLights; i++) {
        lighting += CalcPointLight(pointLights[i], norm, FragPos, viewDir, matShininess, matSpecStrength) * pointLightMaster;
    }

    lighting += CalcSpotLight(spotLight, norm, FragPos, viewDir, matShininess, spotShadow) * float(spotLightEnabled);

    vec4 texColor = useTexture ? texture(texture_diffuse, TexCoords) : vec4(1.0);
    vec3 surfaceColor = objectColor * texColor.rgb;

    vec3 result = ambientColor * ambientIntensity * surfaceColor;
    result += lighting * surfaceColor;

    if (hasEmissive && isLit) {
        vec3 emissiveColor = texture(texture_emissive, TexCoords).rgb;
        result += emissiveColor * 0.8;
    }

    float alpha = useTexture ? texColor.a : 1.0;
    if (alpha < 0.1) discard;
    FragColor = vec4(result, alpha);
}
)glsl";

const char* kDepthPreFragSrc = R"glsl(
#version 330 core
void main() {}
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

    void setBool(const std::string& name, bool value) const {
        GLint loc = glGetUniformLocation(m_id, name.c_str());
        if (loc != -1) glUniform1i(loc, value ? 1 : 0);
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
        if (filename.find("depth_pre.vert") != std::string::npos) return kSceneVertSrc;
        if (filename.find("depth_pre.frag") != std::string::npos) return kDepthPreFragSrc;

        std::cerr << "[Shader] 未知着色器文件: " << path << std::endl;
        return "";
    }
};

// ============================================================================
// 模型类：已由 Model.hpp 提供完整实现（成员B）
// 详见 graphics_2/Model.hpp
// ============================================================================

// ============================================================================
// 成员 D：相机与动态模块
#include "custom/Camera.hpp"

// ============================================================================
// 全局变量
// ============================================================================

// GLFW 窗口句柄
GLFWwindow* g_window = nullptr;

// 核心渲染组件
std::unique_ptr<ShadowPipeline> g_shadowPipeline;
std::unique_ptr<Shader> g_shadowShader;  // 第一通路：深度着色器
std::unique_ptr<Shader> g_sceneShader;   // 第二通路：主场景着色器
std::unique_ptr<Shader> g_depthPreShader; // 屏幕空间深度预通道
GLuint g_sceneDepthTex = 0;
GLuint g_sceneDepthFBO = 0;
std::unique_ptr<Camera> g_camera;

// 光源参数
struct DirectionalLight {
    glm::vec3 direction = glm::vec3(-0.3f, -0.8f, -0.3f);
    glm::vec3 color = glm::vec3(0.3f, 0.35f, 0.5f);
    float intensity = 0.4f;
} g_dirLight;

struct PointLight {
    glm::vec3 position = glm::vec3(0.0f, 3.0f, 0.0f);
    glm::vec3 color = glm::vec3(1.0f, 0.7f, 0.4f);
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
};

struct SpotLight {
    glm::vec3 position = glm::vec3(0.0f, 2.0f, 5.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 color = glm::vec3(1.0f, 0.95f, 0.8f);
    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(20.0f));
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
} g_spotLight;

float g_timeOfDay = 0.25f;
float g_dayCycleSpeed = 0.02f;
bool g_dayCycleEnabled = false;  // 室内默认关闭昼夜循环

float g_pointLightMaster = 1.0f;
bool g_allLightsEnabled = false;
bool g_flashlightOn = false;

std::vector<float> g_pointLightIntensity;
std::vector<glm::vec3> g_pointLightColorMul;

bool g_flashlightPickedUp = false;
glm::vec3 g_flashlightWorldPos = glm::vec3(-25.25f, 0.53f, -122.43f);
std::unique_ptr<Model> g_flashlightBody;
std::unique_ptr<Model> g_flashlightHead;

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

std::vector<SceneObject> g_sceneObjects;

// ============================================================================
// 门交互系统
// ============================================================================

struct DoorObject {
    std::vector<Mesh> meshes;       // 门网格（从 Model 提取）
    glm::vec3 hingeWorldPos;        // 铰链世界坐标
    glm::vec3 hingeAxis = glm::vec3(0.0f, 1.0f, 0.0f);  // 旋转轴
    float openAngle = 0.0f;         // 当前开合角度（度）
    float targetAngle = 0.0f;       // 目标角度
    float closedAngle = 0.0f;       // 关闭角度
    float openedAngle = 90.0f;      // 打开角度
    float animSpeed = 120.0f;       // 动画速度（度/秒）
    bool isOpen = false;
    bool isAnimating = false;
    bool hasDoor = false;
    glm::vec3 localMin;
    glm::vec3 localMax;

    glm::mat4 GetDoorModelMatrix() const {
        if (!hasDoor) return glm::mat4(1.0f);

        glm::mat4 m(1.0f);
        m = glm::translate(m, hingeWorldPos);
        m = glm::rotate(m, glm::radians(openAngle), hingeAxis);
        m = glm::translate(m, -hingeWorldPos);
        return m;
    }

    void Update(float deltaTime) {
        if (std::abs(openAngle - targetAngle) < 0.01f) {
            isAnimating = false;
            return;
        }
        isAnimating = true;
        float step = animSpeed * deltaTime;
        if (openAngle < targetAngle)
            openAngle = std::min(openAngle + step, targetAngle);
        else
            openAngle = std::max(openAngle - step, targetAngle);
    }

    void Toggle() {
        if (isAnimating) return;
        targetAngle = isOpen ? closedAngle : openedAngle;
        isOpen = !isOpen;
    }

    void GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const {
        if (!hasDoor) { outMin = outMax = glm::vec3(0.0f); return; }
        glm::mat4 mat = GetDoorModelMatrix();
        glm::vec3 corners[8] = {
            glm::vec3(localMin.x, localMin.y, localMin.z),
            glm::vec3(localMax.x, localMin.y, localMin.z),
            glm::vec3(localMin.x, localMax.y, localMin.z),
            glm::vec3(localMin.x, localMin.y, localMax.z),
            glm::vec3(localMax.x, localMax.y, localMin.z),
            glm::vec3(localMax.x, localMin.y, localMax.z),
            glm::vec3(localMin.x, localMax.y, localMax.z),
            glm::vec3(localMax.x, localMax.y, localMax.z)
        };
        outMin = glm::vec3(1e10f);
        outMax = glm::vec3(-1e10f);
        for (int i = 0; i < 8; i++) {
            glm::vec4 wc = mat * glm::vec4(corners[i], 1.0f);
            outMin = glm::min(outMin, glm::vec3(wc));
            outMax = glm::max(outMax, glm::vec3(wc));
        }
    }
};

DoorObject g_door;

// 时间控制
float g_deltaTime = 0.0f;
float g_lastFrame = 0.0f;

// ============================================================================
// ImGui UI 参数（成员 E）
// ============================================================================
bool g_imguiInitialized = false;
float g_ambientIntensity = 0.15f;
glm::vec3 g_ambientColor = glm::vec3(0.4f, 0.35f, 0.35f);
float g_shadowBias = 0.005f;
int g_shadowPCFRadius = 1;

// ============================================================================
// 聚光灯阴影贴图（Spotlight Shadow Map）
// ============================================================================
GLuint g_spotShadowFBO = 0;
GLuint g_spotShadowTex = 0;
constexpr int SPOT_SHADOW_SIZE = 1024;
std::unique_ptr<Shader> g_spotShadowShader;
glm::mat4 g_spotShadowLightSpace = glm::mat4(1.0f);

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
void SpotShadowPass();
void SceneDepthPrePass();
void LightingPass();
static void UpdateDayNightCycle();
void RenderLoop();
void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
void MouseCallback(GLFWwindow* window, double xpos, double ypos);
void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

// ============================================================================
// FPS 角色碰撞系统
// ============================================================================

struct PlayerCharacter {
    glm::vec3 position = glm::vec3(0.0f);
    float radius = 0.22f;
    float eyeHeight = 1.7f;
    bool active = false;
    int spawnFrames = 10;

    float velocityY = 0.0f;
    bool isGrounded = false;
    static constexpr float GRAVITY = 20.0f;
    static constexpr float JUMP_SPEED = 7.0f;

    void SyncFromCamera(const Camera& cam) {
        position = cam.position;
    }

    void ApplyToCamera(Camera& cam) const {
        cam.position = position;
    }
};

PlayerCharacter g_player;
glm::vec3 g_lastSafePos(0.0f);

// ============================================================================
// FPS 碰撞辅助类型与函数
// ============================================================================

static bool CheckAABBOverlapXZ(const glm::vec3& pos, float radius,
                                float minX, float maxX, float minZ, float maxZ) {
    float cx = std::max(minX, std::min(pos.x, maxX));
    float cz = std::max(minZ, std::min(pos.z, maxZ));
    float dx = pos.x - cx, dz = pos.z - cz;
    return (dx * dx + dz * dz) < (radius * radius);
}

static bool CheckRefOverlap(const glm::vec3& refPos, float radius,
                             float minX, float maxX, float minZ, float maxZ) {
    return CheckAABBOverlapXZ(refPos, radius, minX, maxX, minZ, maxZ);
}

static bool PushOutOfAABB(glm::vec3& pos, float radius,
                           float minX, float maxX, float minZ, float maxZ) {
    float cx = std::max(minX, std::min(pos.x, maxX));
    float cz = std::max(minZ, std::min(pos.z, maxZ));
    float dx = pos.x - cx, dz = pos.z - cz;
    float distSq = dx * dx + dz * dz;

    if (distSq >= radius * radius) return false;

    if (distSq < 0.000001f) {
        static int insideWarnCount = 0;
        if (insideWarnCount < 5) {
            insideWarnCount++;
            std::cout << "[Collision] 玩家中心在AABB内部 "
                      << "AABB=[" << minX << "," << maxX << "]x[" << minZ << "," << maxZ << "] "
                      << "pos=(" << pos.x << "," << pos.z << ")" << std::endl;
        }
        float edges[4] = { pos.x - minX, maxX - pos.x,
                           pos.z - minZ, maxZ - pos.z };
        int bestIdx = 0;
        for (int i = 1; i < 4; i++) {
            if (edges[i] < edges[bestIdx]) bestIdx = i;
        }
        if (bestIdx == 0) pos.x = minX - radius;
        else if (bestIdx == 1) pos.x = maxX + radius;
        else if (bestIdx == 2) pos.z = minZ - radius;
        else pos.z = maxZ + radius;
    } else {
        float dist = std::sqrt(distSq);
        float pen = radius - dist;
        pos.x += (dx / dist) * pen;
        pos.z += (dz / dist) * pen;
    }
    return true;
}

static void ResolveVerticalCollision(PlayerCharacter& player) {
    player.isGrounded = false;
    float playerBottom = player.position.y - player.radius;
    float playerTop = player.position.y + player.radius;
    float groundY = -1e10f;
    float ceilingY = 1e10f;
    const float gndThresh = player.radius * 0.45f;

    auto scanAABB = [&](float minX, float maxX, float minZ, float maxZ,
                        float aabbMinY, float aabbMaxY) {
        if (!CheckAABBOverlapXZ(player.position, player.radius, minX, maxX, minZ, maxZ))
            return;
        if (aabbMaxY <= player.position.y + gndThresh && aabbMaxY > groundY)
            groundY = aabbMaxY;
        if (aabbMinY >= player.position.y - gndThresh && aabbMinY < ceilingY)
            ceilingY = aabbMinY;
    };

    for (const auto& obj : g_sceneObjects) {
        if (!obj.model) continue;
        for (const auto& fc : obj.model->GetFloorColliders()) {
            scanAABB(fc.min.x, fc.max.x, fc.min.z, fc.max.z, fc.min.y, fc.max.y);
        }
    }

    if (g_door.hasDoor) {
        glm::vec3 dMin, dMax;
        g_door.GetWorldAABB(dMin, dMax);
        scanAABB(dMin.x, dMax.x, dMin.z, dMax.z, dMin.y, dMax.y);
    }

    if (groundY > -1e9f && player.velocityY <= 0.0f) {
        if (playerBottom <= groundY + gndThresh) {
            player.position.y = groundY + player.radius;
            player.velocityY = 0.0f;
            player.isGrounded = true;
        }
    }

    // Ceiling bump
    if (ceilingY < 1e9f && player.velocityY > 0.0f) {
        if (playerTop >= ceilingY) {
            player.position.y = ceilingY - player.radius;
            player.velocityY = 0.0f;
        }
    }

    // World floor safety net
    if (player.position.y - player.radius < -20.0f) {
        player.position.y = -20.0f + player.radius;
        player.velocityY = 0.0f;
        player.isGrounded = true;
    }
}

static void ResolvePlayerCollision(glm::vec3& pos, float radius) {
    const float worldMinX = -100.0f, worldMaxX = 100.0f;
    const float worldMinZ = -200.0f, worldMaxZ = 50.0f;

    pos.x = glm::clamp(pos.x, worldMinX + radius, worldMaxX - radius);
    pos.z = glm::clamp(pos.z, worldMinZ + radius, worldMaxZ - radius);

    for (int iter = 0; iter < 5; iter++) {
        bool anyOverlap = false;

        for (const auto& obj : g_sceneObjects) {
            if (!obj.model) continue;
            for (const auto& wc : obj.model->GetWallColliders()) {
                if (pos.y - radius > wc.max.y || pos.y + radius < wc.min.y)
                    continue;
                if (PushOutOfAABB(pos, radius, wc.min.x, wc.max.x, wc.min.z, wc.max.z))
                    anyOverlap = true;
            }
        }

        if (g_door.hasDoor) {
            glm::vec3 doorMin, doorMax;
            g_door.GetWorldAABB(doorMin, doorMax);
            if (pos.y - radius <= doorMax.y && pos.y + radius >= doorMin.y) {
                if (PushOutOfAABB(pos, radius, doorMin.x, doorMax.x, doorMin.z, doorMax.z))
                    anyOverlap = true;
            }
        }

        if (!anyOverlap) break;
    }
}

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
    g_window = glfwCreateWindow(g_winWidth, g_winHeight, WINDOW_TITLE, nullptr, nullptr);
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
    
    // 获取实际帧缓冲大小并设置视口
    glfwGetFramebufferSize(g_window, &g_winWidth, &g_winHeight);
    glViewport(0, 0, g_winWidth, g_winHeight);
    
    // 设置清除颜色（天空色）
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);  // 室内暗色
    
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
            "../shaders/shadow_depth.vert",
            "../shaders/shadow_depth.frag"
        );
        
        // 第二通路：主场景着色器（完整光照+阴影）
        g_sceneShader = std::make_unique<Shader>(
            "../shaders/scene.vert",
            "../shaders/scene.frag"
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

        g_depthPreShader = std::make_unique<Shader>(
            "../shaders/depth_pre.vert",
            "../shaders/depth_pre.frag"
        );

        glGenTextures(1, &g_sceneDepthTex);
        glBindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, g_winWidth, g_winHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &g_sceneDepthFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_sceneDepthFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_sceneDepthTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 4. 初始化相机（成员 D）— 出生在主房屋正门前
        g_camera = std::make_unique<Camera>(glm::vec3(-28.5f, 1.2f, -100.0f));
        g_camera->fpsEyeHeight = 1.2f;
        g_camera->InitInputMode(g_window);
        g_player.position = g_camera->position;
        g_lastSafePos = g_camera->position;
        glfwSetCursorPosCallback(g_window, MouseCallback);
        glfwSetScrollCallback(g_window, ScrollCallback);
        std::cout << "[成员D] FPS 相机已初始化 (WASD移动, 鼠标视角, ESC切换鼠标)" << std::endl;
        
        // 5. 加载废弃房屋场景
        std::cout << "[Scene] 加载 Abandoned House..." << std::endl;
        try {
            auto houseModel = std::make_unique<Model>("../asset/model/Abandoned_House.obj");
            if (houseModel->meshes.empty()) {
                houseModel = std::make_unique<Model>("asset/model/Abandoned_House.obj");
            }
            if (!houseModel->meshes.empty() || houseModel->HasDoor()) {
                // 提取门网格
                if (houseModel->HasDoor()) {
                    g_door.meshes = houseModel->ExtractDoorMeshes();
                    g_door.hasDoor = true;

                    glm::vec3 doorCenter = houseModel->GetDoorCenter();
                    glm::vec3 doorSize = houseModel->GetDoorSize();
                    glm::vec3 doorMin = houseModel->doorBMin;
                    glm::vec3 doorMax = houseModel->doorBMax;

                    glm::vec3 hingeLocal;
                    hingeLocal.y = doorCenter.y;
                    if (doorSize.x > doorSize.z) {
                        hingeLocal.x = doorMin.x + 0.01f;
                        hingeLocal.z = doorCenter.z;
                    } else {
                        hingeLocal.x = doorCenter.x;
                        hingeLocal.z = doorMin.z + 0.01f;
                    }

                    g_door.hingeWorldPos = hingeLocal;
                    g_door.localMin = houseModel->doorBMin;
                    g_door.localMax = houseModel->doorBMax;

                    std::cout << "[Door] 铰链位置: [" << hingeLocal.x << "," << hingeLocal.y << "," << hingeLocal.z << "]"
                              << " 门大小: [" << doorSize.x << "," << doorSize.y << "," << doorSize.z << "]" << std::endl;
                }

                SceneObject house;
                house.model = std::move(houseModel);
                house.position = glm::vec3(0.0f, 0.0f, 0.0f);
                house.scale = glm::vec3(1.0f);
                house.color = glm::vec3(0.9f);
                g_sceneObjects.push_back(std::move(house));
                std::cout << "[Scene] 场景加载完成: " << g_sceneObjects.size() << " 个物体" << std::endl;

                for (auto& obj : g_sceneObjects) {
                    if (!obj.model) continue;
                    auto& fgs = obj.model->GetFanGroups();
                    for (auto& fg : fgs) fg.spinning = true;
                    std::cout << "[Fan] 初始化: " << fgs.size() << " 个吊扇全开" << std::endl;
                }

                g_flashlightBody = std::make_unique<Model>(Model::CreateCylinder(16));
                g_flashlightHead = std::make_unique<Model>(Model::CreateCylinder(16));
            } else {
                std::cerr << "[Scene] 模型无网格数据, 场景为空" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Scene] 加载模型异常: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Scene] 加载模型未知异常" << std::endl;
        }

        // 如果模型加载失败，放一个测试立方体
        if (g_sceneObjects.empty()) {
            std::cout << "[Scene] 回退: 创建测试立方体" << std::endl;
            SceneObject testObj;
            testObj.model = std::make_unique<Model>(Model::CreateCube());
            testObj.position = glm::vec3(0.0f, 2.0f, 0.0f);
            testObj.scale = glm::vec3(3.0f);
            testObj.color = glm::vec3(0.8f, 0.4f, 0.2f);
            g_sceneObjects.push_back(std::move(testObj));
        }
        std::cout << "[Scene] 最终场景物体数: " << g_sceneObjects.size() << std::endl;

        int totalLights = 0;
        for (const auto& obj : g_sceneObjects) {
            if (obj.model) totalLights += (int)obj.model->GetLightSources().size();
        }
        g_pointLightIntensity.assign(totalLights, 1.0f);
        g_pointLightColorMul.assign(totalLights, glm::vec3(1.0f));
        std::cout << "[Scene] 点光源数量: " << totalLights << std::endl;

        std::cout.flush();

        // ============================================================================
        // [插槽 0] 成员 E - ImGui 初始化
        // ============================================================================
        std::cout << "[成员E] 初始化 ImGui..." << std::endl;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;

        ImFontConfig fontCfg;
        fontCfg.OversampleH = 2;
        fontCfg.OversampleV = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/simhei.ttf", 16.0f, &fontCfg,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (!font) {
            font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16.0f, &fontCfg,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
        if (!font) {
            std::cout << "[成员E] 未找到中文字体, 使用默认字体 (中文将显示为?)" << std::endl;
            io.Fonts->AddFontDefault();
        }

        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForOpenGL(g_window, true)) {
            std::cerr << "[Error] ImGui_ImplGlfw_InitForOpenGL failed!" << std::endl;
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
            std::cerr << "[Error] ImGui_ImplOpenGL3_Init failed!" << std::endl;
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        g_imguiInitialized = true;
        std::cout << "[成员E] ImGui 初始化成功" << std::endl;
        
        std::cout << "[Resources] All resources initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Error] Resource initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Shutdown() {
    // 清理 ImGui
    if (g_imguiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        g_imguiInitialized = false;
        std::cout << "[成员E] ImGui 已清理" << std::endl;
    }

    // 清理场景物体
    g_sceneObjects.clear();

    // 智能指针会自动清理资源
    g_camera.reset();
    g_sceneShader.reset();
    g_shadowShader.reset();
    g_shadowPipeline.reset();
    g_spotShadowShader.reset();

    if (g_spotShadowFBO) { glDeleteFramebuffers(1, &g_spotShadowFBO); g_spotShadowFBO = 0; }
    if (g_spotShadowTex) { glDeleteTextures(1, &g_spotShadowTex); g_spotShadowTex = 0; }

    if (g_sceneDepthTex) { glDeleteTextures(1, &g_sceneDepthTex); g_sceneDepthTex = 0; }
    if (g_sceneDepthFBO) { glDeleteFramebuffers(1, &g_sceneDepthFBO); g_sceneDepthFBO = 0; }
    
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

        // 3. 更新门动画（必须在 ShadowPass 之前，保证阴影一致）
        g_door.Update(g_deltaTime);

        for (auto& obj : g_sceneObjects) {
            if (!obj.model) continue;
            for (auto& fg : obj.model->GetFanGroups()) {
                if (fg.spinning) fg.angle += g_deltaTime * 8.0f;
            }
        }

        // ================================================================
        // 双通路渲染流程 (Dual-Pass Rendering)
        // ================================================================

        // 第一通路：生成阴影深度图
        ShadowPass();

        // 聚光灯阴影通路
        if (g_camera) {
            g_spotLight.position  = g_camera->GetPosition() + g_camera->GetFront() * 1.2f + g_camera->right * 0.5f + glm::vec3(0.0f, -0.35f, 0.0f);
            g_spotLight.direction = g_camera->GetFront();
        }
        SpotShadowPass();

        SceneDepthPrePass();

        UpdateDayNightCycle();

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
        if (g_imguiInitialized) {
            // 开始 ImGui 帧
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // 设置窗口样式
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::SetNextWindowSize(ImVec2(380.0f, 500.0f), ImGuiCond_FirstUseEver);

            // 绘制控制面板
            ImGui::Begin("光照控制面板");
            ImGui::Text("废弃房屋 - 室内光照");

            ImGui::SeparatorText("相机模式");
            const char* modeText = (g_camera && g_camera->IsFPSMode()) ? "FPS (Tab=自由)" : "自由 (Tab=FPS)";
            ImGui::Text("%s | 高度: %.1fm", modeText, g_camera ? g_camera->position.y : 0.0f);
            if (g_camera) {
                ImGui::Text("位置: (%.2f, %.2f, %.2f)",
                    g_camera->position.x, g_camera->position.y, g_camera->position.z);
            }
            ImGui::SameLine();
            if (ImGui::Button("切换")) { if (g_camera) g_camera->ToggleFPSMode(); }

            if (g_door.hasDoor) {
                ImGui::SeparatorText("门");
                const char* stateText = g_door.isOpen ? "开" : "关";
                float progress = g_door.openedAngle > 0.01f ? g_door.openAngle / g_door.openedAngle : 0.0f;
                ImGui::Text("状态: %s (%.0f\xc2\xb0)", stateText, g_door.openAngle);
                ImGui::ProgressBar(progress, ImVec2(-1, 15), stateText);
                if (g_camera && g_camera->IsFPSMode()) {
                    glm::vec3 toDoor = g_door.hingeWorldPos - g_camera->GetPosition();
                    float dist = glm::length(toDoor);
                    ImGui::Text("距离: %.1fm %s", dist, dist < 3.0f ? "(E 开关门)" : "(太远)");
                }
                ImGui::SameLine();
                if (ImGui::Button("开关门")) { g_door.Toggle(); }
            }

            ImGui::SeparatorText("方向光(太阳)");
            ImGui::SliderFloat("强度", &g_dirLight.intensity, 0.0f, 2.0f, "%.2f");
            ImGui::ColorEdit3("颜色", glm::value_ptr(g_dirLight.color));

            ImGui::SeparatorText("室内灯具");
            ImGui::Checkbox("全部开启", &g_allLightsEnabled);
            ImGui::SliderFloat("全局缩放", &g_pointLightMaster, 0.0f, 5.0f, "%.2f");
            int totalLights = 0;
            for (const auto& obj : g_sceneObjects) {
                if (obj.model) totalLights += (int)obj.model->GetLightSources().size();
            }
            ImGui::Text("灯具总数: %d", totalLights);
            if (g_camera && g_camera->IsFPSMode()) {
                ImGui::Text("E键: 开关附近灯具");
            }

            if (ImGui::TreeNode("单灯独立设置")) {
                static int selectedLight = -1;
                int flatIdx = 0;
                for (const auto& obj : g_sceneObjects) {
                    if (!obj.model) continue;
                    for (auto& ls : obj.model->GetLightSources()) {
                        std::ostringstream oss;
                        oss << "灯具#" << flatIdx << " ("
                            << std::fixed << std::setprecision(1)
                            << ls.position.x << ","
                            << ls.position.y << ","
                            << ls.position.z << ")";
                        std::string label = oss.str();
                        if (ImGui::TreeNode(label.c_str())) {
                            if (flatIdx < (int)g_pointLightIntensity.size()) {
                                ImGui::SliderFloat("亮度", &g_pointLightIntensity[flatIdx], 0.0f, 5.0f, "%.2f");
                            }
                            if (flatIdx < (int)g_pointLightColorMul.size()) {
                                ImGui::ColorEdit3("色调", glm::value_ptr(g_pointLightColorMul[flatIdx]));
                            }
                            ImGui::Text("开启: %s", ls.enabled ? "是" : "否");
                            ImGui::TreePop();
                        }
                        flatIdx++;
                    }
                }
                ImGui::TreePop();
            }

            ImGui::SeparatorText("聚光灯(手电筒)");
            if (g_flashlightPickedUp) {
                ImGui::Checkbox("手电筒开关", &g_flashlightOn);
                ImGui::SliderFloat("手电筒强度", &g_spotLight.color.r, 0.0f, 3.0f, "%.2f");
            } else {
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "尚未拾取手电筒");
            }
            g_spotLight.color.g = g_spotLight.color.r * 0.95f;
            g_spotLight.color.b = g_spotLight.color.r * 0.8f;
            if (g_camera && g_camera->IsFPSMode()) {
                if (g_flashlightPickedUp) {
                    ImGui::Text("F键: 开关手电筒");
                } else {
                    ImGui::Text("E键: 拾取附近手电筒");
                }
            }

            ImGui::SeparatorText("环境光");
            static bool blackoutTest = false;
            static float savedAmbient = 0.08f;
            static float savedDirIntensity = 0.4f;
            if (ImGui::Checkbox("断电测试", &blackoutTest)) {
                if (blackoutTest) {
                    savedAmbient = g_ambientIntensity;
                    savedDirIntensity = g_dirLight.intensity;
                    g_ambientIntensity = 0.0f;
                    g_dirLight.intensity = 0.0f;
                } else {
                    g_ambientIntensity = savedAmbient;
                    g_dirLight.intensity = savedDirIntensity;
                }
            }
            ImGui::SliderFloat("环境光强度", &g_ambientIntensity, 0.0f, 1.0f, "%.3f");
            ImGui::ColorEdit3("环境光颜色", glm::value_ptr(g_ambientColor));

            ImGui::SeparatorText("昼夜循环");
            ImGui::Checkbox("启用循环", &g_dayCycleEnabled);
            ImGui::SliderFloat("循环速度", &g_dayCycleSpeed, 0.001f, 0.1f, "%.3f");
            ImGui::SliderFloat("时刻", &g_timeOfDay, 0.0f, 1.0f, "%.3f");
            ImGui::Text("0=黎明  0.25=正午  0.5=黄昏  0.75=午夜");
            if (ImGui::Button("锁定黑夜", ImVec2(-1, 0))) {
                g_timeOfDay = 0.75f;
                g_dayCycleEnabled = false;
                UpdateDayNightCycle();
            }

            ImGui::End();

            // 渲染 ImGui
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
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
    bool fpsActive = g_camera && g_camera->IsFPSMode();
    g_player.active = fpsActive;

    if (g_camera) {
        g_camera->ProcessInput(g_window, g_deltaTime);
    }

    if (fpsActive) {
        g_player.position.x = g_camera->position.x;
        g_player.position.z = g_camera->position.z;

        if (g_player.spawnFrames > 0) {
            g_player.spawnFrames--;
            g_player.position.y = g_camera->position.y;
            g_lastSafePos = g_player.position;
            if (g_player.spawnFrames == 0) {
                int totalWC = 0, totalFC = 0;
                for (const auto& obj : g_sceneObjects) {
                    if (obj.model) {
                        totalWC += (int)obj.model->GetWallColliders().size();
                        totalFC += (int)obj.model->GetFloorColliders().size();
                    }
                }
                std::cout << "[Spawn] 出生保护结束 pos=("
                          << g_player.position.x << "," << g_player.position.y << "," << g_player.position.z
                          << ") 墙面=" << totalWC << " 地面=" << totalFC << std::endl;
                ResolveVerticalCollision(g_player);
                g_lastSafePos = g_player.position;
            }
        } else {
            // Gravity
            g_player.velocityY -= PlayerCharacter::GRAVITY * g_deltaTime;

            // Jump input
            static bool spaceHeld = false;
            if (glfwGetKey(g_window, GLFW_KEY_SPACE) == GLFW_PRESS) {
                if (!spaceHeld && g_player.isGrounded) {
                    g_player.velocityY = PlayerCharacter::JUMP_SPEED;
                    g_player.isGrounded = false;
                }
                spaceHeld = true;
            } else {
                spaceHeld = false;
            }

            // Apply vertical movement + resolve
            g_player.position.y += g_player.velocityY * g_deltaTime;
            ResolveVerticalCollision(g_player);

            // XZ collision
            glm::vec3 preCollisionPos = g_player.position;
            ResolvePlayerCollision(g_player.position, g_player.radius);

            // Re-check ground after XZ pushout (may have been pushed off a ledge)
            ResolveVerticalCollision(g_player);

            if (glm::length(g_player.position - preCollisionPos) < 0.001f) {
                g_lastSafePos = g_player.position;
            }
        }

        g_camera->position = g_player.position;
        g_camera->position.y += g_player.eyeHeight - g_player.radius;
    }

    if (fpsActive) {
        static bool ePressed = false;
        if (glfwGetKey(g_window, GLFW_KEY_E) == GLFW_PRESS) {
            if (!ePressed) {
                ePressed = true;
                glm::vec3 camPos = g_camera->GetPosition();
                glm::vec3 camFront = g_camera->GetFront();
                bool used = false;

                if (g_door.hasDoor) {
                    glm::vec3 toDoor = g_door.hingeWorldPos - camPos;
                    float dist = glm::length(toDoor);
                    if (dist < 3.0f) {
                        glm::vec3 dirToDoor = glm::normalize(toDoor);
                        float dot = glm::dot(glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z)),
                                             glm::normalize(glm::vec3(dirToDoor.x, 0.0f, dirToDoor.z)));
                        if (dot > 0.4f) {
                            g_door.Toggle();
                            std::cout << "[Door] " << (g_door.isOpen ? "打开" : "关闭")
                                      << " (距离=" << dist << ")" << std::endl;
                            used = true;
                        }
                    }
                }

                if (!used) {
                    float bestDist = 2.5f;
                    int bestIdx = -1;
                    Model* bestModel = nullptr;
                    int globalIdx = 0;
                    glm::vec3 flatFront = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
                    for (auto& obj : g_sceneObjects) {
                        if (!obj.model) continue;
                        for (auto& ls : obj.model->GetLightSources()) {
                            float dist = glm::length(ls.position - camPos);
                            if (dist >= bestDist) { globalIdx++; continue; }
                            glm::vec3 dirToLight = glm::normalize(ls.position - camPos);
                            float dot = glm::dot(flatFront, glm::normalize(glm::vec3(dirToLight.x, 0.0f, dirToLight.z)));
                            if (dot < 0.55f) { globalIdx++; continue; }
                            bestDist = dist;
                            bestIdx = globalIdx;
                            bestModel = obj.model.get();
                            globalIdx++;
                        }
                    }
                    if (bestModel && bestIdx >= 0) {
                        globalIdx = 0;
                        for (auto& ls : bestModel->GetLightSources()) {
                            if (globalIdx == bestIdx) {
                                ls.enabled = !ls.enabled;
                                std::cout << "[Light] " << (ls.enabled ? "开" : "关")
                                          << " 距离=" << bestDist << std::endl;
                                used = true;
                                break;
                            }
                            globalIdx++;
                        }
                    }

                    if (!used) {
                        float bestFanDist = 3.0f;
                        Model* bestFanModel = nullptr;
                        int bestFanIdx = -1;
                        glm::vec3 flatFront = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
                        for (auto& obj : g_sceneObjects) {
                            if (!obj.model) continue;
                            auto& fgs = obj.model->GetFanGroups();
                            for (int fi = 0; fi < (int)fgs.size(); fi++) {
                                float dist = glm::length(fgs[fi].pivot - camPos);
                                if (dist >= bestFanDist) continue;
                                glm::vec3 dirToFan = glm::normalize(fgs[fi].pivot - camPos);
                                float dot = glm::dot(flatFront, glm::normalize(glm::vec3(dirToFan.x, 0.0f, dirToFan.z)));
                                if (dot < 0.55f) continue;
                                bestFanDist = dist;
                                bestFanIdx = fi;
                                bestFanModel = obj.model.get();
                            }
                        }
                        if (bestFanModel && bestFanIdx >= 0) {
                            auto& fgs = bestFanModel->GetFanGroups();
                            fgs[bestFanIdx].spinning = !fgs[bestFanIdx].spinning;
                            std::cout << "[Fan] " << (fgs[bestFanIdx].spinning ? "旋转" : "停止")
                                      << " 距离=" << bestFanDist << std::endl;
                            used = true;
                        }
                    }

                    if (!used && !g_flashlightPickedUp) {
                        glm::vec3 flatFront2 = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
                        float dist = glm::length(g_flashlightWorldPos - camPos);
                        if (dist < 2.5f) {
                            glm::vec3 dirToItem = glm::normalize(g_flashlightWorldPos - camPos);
                            float dot = glm::dot(flatFront2, glm::normalize(glm::vec3(dirToItem.x, 0.0f, dirToItem.z)));
                            if (dot > 0.55f) {
                                g_flashlightPickedUp = true;
                                g_flashlightOn = true;
                                std::cout << "[Item] 拾取手电筒 距离=" << dist << std::endl;
                            }
                        }
                    }
                }
            }
        } else {
            ePressed = false;
        }

        static bool fPressed = false;
        if (glfwGetKey(g_window, GLFW_KEY_F) == GLFW_PRESS) {
            if (!fPressed) {
                fPressed = true;
                if (g_flashlightPickedUp) {
                    g_flashlightOn = !g_flashlightOn;
                    std::cout << "[Flashlight] " << (g_flashlightOn ? "开" : "关") << std::endl;
                } else {
                    std::cout << "[Flashlight] 未拾取手电筒" << std::endl;
                }
            }
        } else {
            fPressed = false;
        }
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
    glm::vec3 shadowTarget = g_player.active ? g_player.position : g_camera->GetPosition();
    glm::mat4 lightSpaceMatrix = g_shadowPipeline->GetLightSpaceMatrix(
        g_dirLight.direction,
        glm::vec3(0.0f),
        shadowTarget
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

void SpotShadowPass() {
    if (!g_flashlightPickedUp || !g_flashlightOn) return;

    if (g_spotShadowFBO == 0) {
        glGenTextures(1, &g_spotShadowTex);
        glBindTexture(GL_TEXTURE_2D, g_spotShadowTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SPOT_SHADOW_SIZE, SPOT_SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        glGenFramebuffers(1, &g_spotShadowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, g_spotShadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_spotShadowTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[SpotShadow] FBO incomplete! status=0x" << std::hex << fboStatus << std::dec << std::endl;
            glDeleteFramebuffers(1, &g_spotShadowFBO);
            g_spotShadowFBO = 0;
            glDeleteTextures(1, &g_spotShadowTex);
            g_spotShadowTex = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        std::cout << "[SpotShadow] FBO created successfully (" << SPOT_SHADOW_SIZE << "x" << SPOT_SHADOW_SIZE << ")" << std::endl;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_spotShadowFBO);
    glViewport(0, 0, SPOT_SHADOW_SIZE, SPOT_SHADOW_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    g_shadowShader->use();

    glm::mat4 spotProj = glm::ortho(-15.0f, 15.0f, -15.0f, 15.0f, 0.5f, 60.0f);
    glm::vec3 spotPos = g_spotLight.position;
    glm::vec3 spotTarget = spotPos + g_spotLight.direction * 20.0f;
    glm::mat4 spotView = glm::lookAt(spotPos, spotTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 spotLightSpace = spotProj * spotView;
    g_shadowShader->setMat4("lightSpaceMatrix", spotLightSpace);

    RenderScene(*g_shadowShader);

    g_spotShadowLightSpace = spotLightSpace;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneDepthPrePass() {
    glBindFramebuffer(GL_FRAMEBUFFER, g_sceneDepthFBO);
    glViewport(0, 0, g_winWidth, g_winHeight);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    g_depthPreShader->use();
    float aspect = static_cast<float>(g_winWidth) / g_winHeight;
    glm::mat4 projection = g_camera->GetProjectionMatrix(aspect);
    glm::mat4 view = g_camera->GetViewMatrix();
    glm::mat4 lightDummy(1.0f);
    g_depthPreShader->setMat4("projection", projection);
    g_depthPreShader->setMat4("view", view);
    g_depthPreShader->setMat4("model", glm::mat4(1.0f));
    g_depthPreShader->setMat4("lightSpaceMatrix", lightDummy);

    for (const auto& obj : g_sceneObjects) {
        if (!obj.model) continue;
        g_depthPreShader->setMat4("model", obj.GetModelMatrix());
        for (const auto& mesh : obj.model->meshes) {
            mesh.Draw(*g_depthPreShader);
        }
    }
    if (g_door.hasDoor) {
        g_depthPreShader->setMat4("model", g_door.GetDoorModelMatrix());
        for (const auto& mesh : g_door.meshes) {
            mesh.Draw(*g_depthPreShader);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/**
 * @brief 第二通路：主场景光照渲染（Lighting Pass）
 * 
 * 此通路在正常相机视角下渲染场景，使用第一通路生成的深度纹理
 * 进行阴影检测，实现动态阴影效果。
 */
static void UpdateDayNightCycle() {
    if (!g_dayCycleEnabled) return;

    g_timeOfDay += g_deltaTime * g_dayCycleSpeed;
    if (g_timeOfDay > 1.0f) g_timeOfDay -= 1.0f;

    const float TWO_PI = glm::pi<float>() * 2.0f;
    float sunAngle = g_timeOfDay * TWO_PI;
    float sunHeight = sin(sunAngle);

    glm::vec3 sunPos(
        cos(sunAngle) * 12.0f,
        sunHeight * 10.0f,
        sin(sunAngle * 0.7f) * 6.0f
    );
    g_dirLight.direction = glm::normalize(-sunPos);

    float daytime = glm::clamp(sunHeight * 1.2f + 0.1f, 0.0f, 1.0f);
    float duskFac = glm::clamp(1.0f - glm::abs(sunHeight) * 3.0f, 0.0f, 1.0f);

    glm::vec3 dayColor(1.0f, 0.98f, 0.92f);
    glm::vec3 duskColor(1.0f, 0.5f, 0.2f);
    glm::vec3 nightColor(0.15f, 0.15f, 0.3f);
    g_dirLight.color = glm::mix(glm::mix(nightColor, duskColor, duskFac), dayColor, daytime);
    g_dirLight.intensity = 0.1f + daytime * 0.9f;

    glm::vec3 ambDay(0.6f, 0.55f, 0.5f);
    glm::vec3 ambDusk(0.35f, 0.25f, 0.2f);
    glm::vec3 ambNight(0.08f, 0.08f, 0.15f);
    g_ambientColor = glm::mix(glm::mix(ambNight, ambDusk, duskFac), ambDay, daytime);
    g_ambientIntensity = 0.08f + daytime * 0.55f;

    glm::vec3 skyDay(0.53f, 0.81f, 0.92f);
    glm::vec3 skyDusk(0.6f, 0.35f, 0.2f);
    glm::vec3 skyNight(0.05f, 0.05f, 0.15f);
    glm::vec3 sky = glm::mix(glm::mix(skyNight, skyDusk, duskFac), skyDay, daytime);
    glClearColor(sky.r, sky.g, sky.b, 1.0f);
}

void LightingPass() {

    // ------------------------------------------------------------------------
    // Step 1: 恢复默认帧缓冲并设置视口
    // ------------------------------------------------------------------------
    glViewport(0, 0, g_winWidth, g_winHeight);

    // ------------------------------------------------------------------------
    // Step 2: 清除颜色和深度缓冲
    // ------------------------------------------------------------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ------------------------------------------------------------------------
    // Step 3: 激活主场景着色器
    // ------------------------------------------------------------------------
    g_sceneShader->use();
    
    // ------------------------------------------------------------------------
    // Step 4: 设置变换矩阵（成员 D - 相机控制）
    // ------------------------------------------------------------------------
    
    // 投影矩阵（透视投影，使用相机的 FOV）
    float aspect = static_cast<float>(g_winWidth) / g_winHeight;
    glm::mat4 projection = g_camera->GetProjectionMatrix(aspect);
    
    // 观察矩阵（从相机视角）
    glm::mat4 view = g_camera->GetViewMatrix();
    
    // 模型矩阵（单位矩阵，可根据需要修改）
    glm::mat4 model = glm::mat4(1.0f);
    
    // 光源空间矩阵（用于片元着色器中的阴影坐标变换）
    glm::vec3 shadowTarget = g_player.active ? g_player.position : g_camera->GetPosition();
    glm::mat4 lightSpaceMatrix = g_shadowPipeline->GetLightSpaceMatrix(
        g_dirLight.direction,
        glm::vec3(0.0f),
        shadowTarget
    );
    
    g_sceneShader->setMat4("projection", projection);
    g_sceneShader->setMat4("view", view);
    g_sceneShader->setMat4("model", model);
    g_sceneShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    g_sceneShader->setMat4("spotLightSpaceMatrix", g_spotShadowLightSpace);
    
    // 传入相机位置（用于光照计算）
    g_sceneShader->setVec3("cameraPos", g_camera->GetPosition());
    
    // ------------------------------------------------------------------------
    // Step 5: 绑定阴影深度纹理
    // ------------------------------------------------------------------------
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_shadowPipeline->GetDepthTextureID());
    g_sceneShader->setInt("shadowMap", 1);

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, g_spotShadowTex);
    g_sceneShader->setInt("spotShadowMap", 4);

    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
    g_sceneShader->setInt("sceneDepth", 3);

    // ------------------------------------------------------------------------
    // Step 6: 设置多光源参数
    // ------------------------------------------------------------------------

    // 方向光（产生阴影的光源）
    g_sceneShader->setVec3("dirLight.direction", g_dirLight.direction);
    g_sceneShader->setVec3("dirLight.color", g_dirLight.color);
    g_sceneShader->setFloat("dirLight.intensity", g_dirLight.intensity);
    
    // 点光源（从模型自动检测）
    g_sceneShader->setFloat("pointLightMaster", g_pointLightMaster);
    int numLights = 0;
    for (const auto& obj : g_sceneObjects) {
        if (!obj.model) continue;
        for (auto& ls : obj.model->GetLightSources()) {
            if (!ls.enabled || numLights >= 64) continue;
            std::string base = "pointLights[" + std::to_string(numLights) + "]";
            g_sceneShader->setVec3(base + ".position", ls.position);
            float mul = (numLights < (int)g_pointLightIntensity.size()) ? g_pointLightIntensity[numLights] : 1.0f;
            glm::vec3 colMul = (numLights < (int)g_pointLightColorMul.size()) ? g_pointLightColorMul[numLights] : glm::vec3(1.0f);
            g_sceneShader->setVec3(base + ".color", ls.color * colMul * mul);
            g_sceneShader->setFloat(base + ".constant", ls.constant);
            g_sceneShader->setFloat(base + ".linear", ls.linear);
            g_sceneShader->setFloat(base + ".quadratic", ls.quadratic);
            g_sceneShader->setFloat(base + ".radius", ls.radius);
            numLights++;
        }
    }
    g_sceneShader->setInt("numPointLights", numLights);

    if (g_camera) {
        g_spotLight.position  = g_camera->GetPosition() + g_camera->GetFront() * 1.2f + g_camera->right * 0.5f + glm::vec3(0.0f, -0.35f, 0.0f);
        g_spotLight.direction = g_camera->GetFront();
    }

    g_sceneShader->setVec3("spotLight.position",  g_spotLight.position);
    g_sceneShader->setVec3("spotLight.direction", g_spotLight.direction);
    g_sceneShader->setVec3("spotLight.color", g_spotLight.color);
    g_sceneShader->setFloat("spotLight.cutOff", g_spotLight.cutOff);
    g_sceneShader->setFloat("spotLight.outerCutOff", g_spotLight.outerCutOff);
    g_sceneShader->setFloat("spotLight.constant", g_spotLight.constant);
    g_sceneShader->setFloat("spotLight.linear", g_spotLight.linear);
    g_sceneShader->setFloat("spotLight.quadratic", g_spotLight.quadratic);
    g_sceneShader->setInt("spotLightEnabled", (g_flashlightPickedUp && g_flashlightOn) ? 1 : 0);
    g_sceneShader->setFloat("spotLight.specStrength", 0.9f);

    // ------------------------------------------------------------------------
    // Step 6.5: 设置环境光与材质参数（成员 E - ImGui 控制）
    // ------------------------------------------------------------------------
    g_sceneShader->setVec3("ambientColor", g_ambientColor);
    g_sceneShader->setFloat("ambientIntensity", g_ambientIntensity);
    
    // 阴影参数（成员 E - ImGui 控制）
    g_sceneShader->setFloat("shadowBias", g_shadowBias);
    g_sceneShader->setInt("pcfRadius", g_shadowPCFRadius);

    // ------------------------------------------------------------------------
    // Step 7: 渲染完整场景（带光照和阴影）
    // ------------------------------------------------------------------------
    g_sceneShader->setInt("texture_diffuse", 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    RenderScene(*g_sceneShader);
    glDisable(GL_BLEND);
}

/**
 * @brief 场景渲染函数
 * 
 * 遍历所有场景物体，设置模型矩阵和颜色后绘制。
 * 通过 Model 类的 Draw() 绑定纹理并提交绘制。
 * 
 * @param shader 当前激活的着色器程序
 */
void RenderScene(Shader& shader) {
    shader.setBool("isLit", g_allLightsEnabled);

    auto applyMaterial = [&shader](const std::string& matName) {
        std::string lower;
        for (char c : matName) lower += (char)tolower((unsigned char)c);
        float shininess = 32.0f;
        float specStrength = 0.25f;
        if (lower.find("metal") != std::string::npos || lower.find("iron") != std::string::npos || lower.find("steel") != std::string::npos) {
            shininess = 128.0f; specStrength = 0.8f;
        } else if (lower.find("wood") != std::string::npos || lower.find("madera") != std::string::npos || lower.find("floor") != std::string::npos || lower.find("plank") != std::string::npos || lower.find("piso") != std::string::npos) {
            shininess = 16.0f; specStrength = 0.1f;
        } else if (lower.find("tile") != std::string::npos || lower.find("marble") != std::string::npos || lower.find("glass") != std::string::npos || lower.find("vidrio") != std::string::npos || lower.find("stone") != std::string::npos || lower.find("piedra") != std::string::npos || lower.find("ladrillo") != std::string::npos || lower.find("muro") != std::string::npos || lower.find("pared") != std::string::npos) {
            shininess = 64.0f; specStrength = 0.6f;
        } else if (lower.find("fabric") != std::string::npos || lower.find("cloth") != std::string::npos || lower.find("rug") != std::string::npos || lower.find("tela") != std::string::npos || lower.find("telara") != std::string::npos || lower.find("carpet") != std::string::npos || lower.find("cuero") != std::string::npos || lower.find("colchon") != std::string::npos) {
            shininess = 4.0f; specStrength = 0.05f;
        } else if (lower.find("plastico") != std::string::npos || lower.find("carton") != std::string::npos || lower.find("plastic") != std::string::npos) {
            shininess = 8.0f; specStrength = 0.08f;
        }
        shader.setFloat("matShininess", shininess);
        shader.setFloat("matSpecStrength", specStrength);
    };

    for (const auto& obj : g_sceneObjects) {
        shader.setMat4("model", obj.GetModelMatrix());
        shader.setVec3("objectColor", obj.color);
        if (obj.model) {
            const auto& bladeMap = obj.model->bladeToFanIdx;
            auto& fanGroups = obj.model->GetFanGroups();
            for (int mi = 0; mi < (int)obj.model->meshes.size(); mi++) {
                const auto& mesh = obj.model->meshes[mi];
                bool hasTex = !mesh.textures.empty();
                shader.setBool("useTexture", hasTex);
                shader.setBool("hasEmissive", mesh.hasEmissive);
                applyMaterial(mesh.matName);
                if (hasTex) shader.setInt("texture_diffuse", 0);
                if (mesh.hasEmissive) {
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, mesh.emissiveTexID);
                    shader.setInt("texture_emissive", 2);
                }
                auto bit = bladeMap.find(mi);
                if (bit != bladeMap.end()) {
                    const auto& fg = fanGroups[bit->second];
                    glm::mat4 fanModel = obj.GetModelMatrix();
                    fanModel = glm::translate(fanModel, fg.pivot);
                    fanModel = glm::rotate(fanModel, fg.angle, glm::vec3(0, 1, 0));
                    fanModel = glm::translate(fanModel, -fg.pivot);
                    shader.setMat4("model", fanModel);
                    mesh.Draw(shader);
                    shader.setMat4("model", obj.GetModelMatrix());
                } else {
                    mesh.Draw(shader);
                }
            }
        }
    }

    if (g_door.hasDoor) {
        glm::mat4 doorModel = g_door.GetDoorModelMatrix();
        shader.setMat4("model", doorModel);
        shader.setVec3("objectColor", glm::vec3(0.85f));
        shader.setBool("hasEmissive", false);
        shader.setFloat("matShininess", 32.0f);
        shader.setFloat("matSpecStrength", 0.25f);
        for (const auto& mesh : g_door.meshes) {
            bool hasTex = !mesh.textures.empty();
            shader.setBool("useTexture", hasTex);
            if (hasTex) shader.setInt("texture_diffuse", 0);
            mesh.Draw(shader);
        }
    }

    if (!g_flashlightPickedUp && g_flashlightBody && g_flashlightHead) {
        glm::mat4 fm(1.0f);
        fm = glm::translate(fm, g_flashlightWorldPos);
        fm = glm::rotate(fm, glm::radians(90.0f), glm::vec3(1, 0, 0));
        fm = glm::scale(fm, glm::vec3(0.07f, 0.35f, 0.07f));
        shader.setMat4("model", fm);
        shader.setVec3("objectColor", glm::vec3(0.12f, 0.12f, 0.15f));
        shader.setBool("useTexture", false);
        shader.setBool("hasEmissive", false);
        shader.setFloat("matShininess", 48.0f);
        shader.setFloat("matSpecStrength", 0.4f);
        for (const auto& mesh : g_flashlightBody->meshes) {
            mesh.Draw(shader);
        }

        glm::mat4 hm(1.0f);
        hm = glm::translate(hm, g_flashlightWorldPos + glm::vec3(0, 0, 0.22f));
        hm = glm::rotate(hm, glm::radians(90.0f), glm::vec3(1, 0, 0));
        hm = glm::scale(hm, glm::vec3(0.13f, 0.10f, 0.13f));
        shader.setMat4("model", hm);
        shader.setVec3("objectColor", glm::vec3(0.18f, 0.18f, 0.20f));
        for (const auto& mesh : g_flashlightHead->meshes) {
            mesh.Draw(shader);
        }
    }
}

// ============================================================================
// 回调函数
// ============================================================================

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    g_winWidth = width;
    g_winHeight = height;
    glViewport(0, 0, width, height);

    if (g_sceneDepthTex != 0) {
        glBindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }

    std::cout << "[Window] Resized to " << width << "x" << height << std::endl;
}

/**
 * @brief 鼠标移动回调（成员 D）
 */
void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    if (g_camera && g_camera->cursorDisabled) {
        g_camera->ProcessMouseMovement(static_cast<float>(xpos), static_cast<float>(ypos));
    }
}

/**
 * @brief 鼠标滚轮回调（成员 D）
 */
void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;
    (void)xoffset;
    if (g_camera) {
        g_camera->ProcessMouseScroll(static_cast<float>(yoffset));
    }
}

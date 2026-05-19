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

// ============================================================================
// 窗口配置
// ============================================================================
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr const char* WINDOW_TITLE = "Computer Graphics Lab - Shadow Mapping";

// ============================================================================
// 前向声明：着色器类占位（由组员实现或集成）
// ============================================================================

/**
 * @class Shader
 * @brief 着色器程序封装（占位符，实际由组员提供完整实现）
 * 
 * 预期接口：
 * - Shader(const char* vertexPath, const char* fragmentPath)
 * - void use()
 * - void setMat4(const std::string& name, const glm::mat4& value)
 * - void setVec3(const std::string& name, const glm::vec3& value)
 * - void setVec3(const std::string& name, float x, float y, float z)
 * - void setFloat(const std::string& name, float value)
 * - void setInt(const std::string& name, int value)
 */
class Shader {
public:
    // 占位符构造函数
    Shader(const char* vertexPath, const char* fragmentPath) {
        // TODO: 组员实现着色器编译和链接
        std::cout << "[Shader] Loading: " << vertexPath << ", " << fragmentPath << std::endl;
        // 临时创建空程序
        m_id = glCreateProgram();
    }
    
    ~Shader() {
        if (m_id != 0) {
            glDeleteProgram(m_id);
        }
    }
    
    void use() const {
        glUseProgram(m_id);
    }
    
    void setMat4(const std::string& name, const glm::mat4& value) const {
        GLint location = glGetUniformLocation(m_id, name.c_str());
        if (location != -1) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
        }
    }
    
    void setVec3(const std::string& name, const glm::vec3& value) const {
        GLint location = glGetUniformLocation(m_id, name.c_str());
        if (location != -1) {
            glUniform3fv(location, 1, glm::value_ptr(value));
        }
    }
    
    void setVec3(const std::string& name, float x, float y, float z) const {
        setVec3(name, glm::vec3(x, y, z));
    }
    
    void setFloat(const std::string& name, float value) const {
        GLint location = glGetUniformLocation(m_id, name.c_str());
        if (location != -1) {
            glUniform1f(location, value);
        }
    }
    
    void setInt(const std::string& name, int value) const {
        GLint location = glGetUniformLocation(m_id, name.c_str());
        if (location != -1) {
            glUniform1i(location, value);
        }
    }

private:
    unsigned int m_id = 0;
};

// ============================================================================
// 前向声明：模型类占位（由组员实现或集成）
// ============================================================================

/**
 * @class Model
 * @brief 3D 模型加载与渲染封装（占位符，实际由组员提供完整实现）
 * 
 * 预期接口：
 * - Model(const char* path)  // 从文件加载模型
 * - void Draw(Shader& shader)  // 使用指定着色器绘制模型
 */
class Model {
public:
    // 占位符构造函数
    Model(const char* path) {
        // TODO: 组员实现模型加载（如使用 Assimp 库）
        std::cout << "[Model] Loading: " << path << std::endl;
    }
    
    void Draw(Shader& shader) {
        // TODO: 组员实现模型绘制
        // 包括绑定 VAO、设置顶点属性、调用 glDrawElements 等
        (void)shader; // 避免未使用参数警告
    }
};

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
std::unique_ptr<Model> g_sceneModel;

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
        
        // 3. 初始化相机
        g_camera = std::make_unique<Camera>();
        
        // ============================================================================
        // [插槽 0] 成员 B & C - 资源初始化
        // ============================================================================
        // TODO: 在此加载所有模型和纹理资源
        //
        // 成员 B 任务：
        // 1. 创建场景模型容器（如 std::vector<std::unique_ptr<Model>>）
        // 2. 加载所有模型文件（≥5个）：
        //    - g_sceneModels.push_back(std::make_unique<Model>("models/building.obj"));
        //    - g_sceneModels.push_back(std::make_unique<Model>("models/props/box.obj"));
        //    - ... 其他模型
        // 3. 设置每个模型的初始变换（位置、旋转、缩放）
        //
        // 成员 C 任务：
        // 1. 加载所有纹理资源：
        //    - 金属纹理: "textures/metal.png"
        //    - 木头纹理: "textures/wood.png"
        //    - 石料纹理: "textures/stone.png"
        //    - 发光体纹理: "textures/glow.png"
        // 2. 创建材质对象，关联纹理ID
        // 3. 将材质分配给对应模型
        //
        // 注意：所有路径都是相对于项目根目录的相对路径！
        // ============================================================================
        
        // 4. 加载场景模型（示例占位）
        // g_sceneModel = std::make_unique<Model>("models/scene.obj");
        
        std::cout << "[Resources] All resources initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Error] Resource initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Shutdown() {
    // 智能指针会自动清理资源
    g_sceneModel.reset();
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
    
    // 传入所有矩阵到着色器
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
    // [插槽 1] 成员 B - 模型渲染系统
    // ============================================================================
    // TODO: 在此渲染所有场景模型
    // 
    // 任务清单：
    // 1. 遍历场景中的所有模型对象（建议存储在 std::vector<Model> 中）
    // 2. 为每个模型设置 model 矩阵（位置、旋转、缩放）
    // 3. 调用 model.Draw(shader) 进行渲染
    //
    // 示例代码：
    // for (auto& model : g_sceneModels) {
    //     shader.setMat4("model", model.GetTransform());
    //     model.Draw(shader);
    // }
    //
    // 注意：确保已加载至少 5 个独立模型（建筑、道具、设备等）
    // ============================================================================
    
    
    // ============================================================================
    // [插槽 2] 成员 C - 材质与纹理系统
    // ============================================================================
    // TODO: 在此绑定材质和纹理
    //
    // 任务清单：
    // 1. 根据模型类型绑定对应的纹理（金属、木头、石料、发光体）
    // 2. 设置材质相关的 uniform 参数：
    //    - material.diffuse  (漫反射贴图)
    //    - material.specular (高光贴图，可选)
    //    - material.shininess (光泽度)
    // 3. 激活并绑定纹理单元
    //
    // 示例代码：
    // glActiveTexture(GL_TEXTURE0);
    // glBindTexture(GL_TEXTURE_2D, material.diffuseMap);
    // shader.setInt("material.diffuse", 0);
    // shader.setFloat("material.shininess", 32.0f);
    //
    // 注意：纹理路径使用相对路径 "textures/wood.png"
    // ============================================================================
    
    (void)shader; // 避免未使用参数警告（实现后删除）
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

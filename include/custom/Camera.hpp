/**
 * @file Camera.hpp
 * @brief FPS 漫游相机类（成员 D - 基础实现）
 * 
 * 功能：
 * - WASD 键盘移动
 * - 鼠标视角控制（偏航角/俯仰角）
 * - 鼠标滚轮调整 FOV
 * - 实时计算观察矩阵和投影矩阵
 */

#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

#include <vector>

// 相机移动方向枚举
enum class CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

// 默认相机参数
constexpr float CAM_DEFAULT_YAW = -90.0f;
constexpr float CAM_DEFAULT_PITCH = 0.0f;
constexpr float CAM_DEFAULT_SPEED = 2.5f;
constexpr float CAM_DEFAULT_SENSITIVITY = 0.1f;
constexpr float CAM_DEFAULT_FOV = 45.0f;
constexpr float CAM_DEFAULT_NEAR_PLANE = 0.1f;
constexpr float CAM_DEFAULT_FAR_PLANE = 100.0f;

/**
 * @class Camera
 * @brief FPS 漫游相机封装
 * 
 * 使用欧拉角（偏航角/俯仰角）控制相机方向
 * 支持键盘移动和鼠标视角控制
 */
class Camera {
public:
    // 相机属性
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    
    // 欧拉角
    float yaw;
    float pitch;
    
    // 相机选项
    float movementSpeed;
    float mouseSensitivity;
    float fov;
    float nearPlane;
    float farPlane;
    
    // 鼠标状态
    bool firstMouse;
    float lastX;
    float lastY;
    bool cursorDisabled;

    bool fpsMode;
    float fpsEyeHeight;

public:
    /**
     * @brief 构造函数
     * @param pos 初始位置
     * @param up 世界坐标系上方向
     * @param yaw 初始偏航角
     * @param pitch 初始俯仰角
     */
    Camera(glm::vec3 pos = glm::vec3(0.0f, 5.0f, 10.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = CAM_DEFAULT_YAW,
           float pitch = CAM_DEFAULT_PITCH)
        : position(pos)
        , worldUp(up)
        , yaw(yaw)
        , pitch(pitch)
        , front(glm::vec3(0.0f, 0.0f, -1.0f))
        , movementSpeed(CAM_DEFAULT_SPEED)
        , mouseSensitivity(CAM_DEFAULT_SENSITIVITY)
        , fov(CAM_DEFAULT_FOV)
        , nearPlane(CAM_DEFAULT_NEAR_PLANE)
        , farPlane(CAM_DEFAULT_FAR_PLANE)
        , firstMouse(true)
        , lastX(400.0f)
        , lastY(300.0f)
        , cursorDisabled(true)
        , fpsMode(true)
        , fpsEyeHeight(1.7f)
    {
        UpdateCameraVectors();
    }

    /**
     * @brief 获取观察矩阵
     * @return glm::mat4 观察矩阵
     */
    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    /**
     * @brief 获取投影矩阵
     * @param aspect 宽高比
     * @return glm::mat4 投影矩阵
     */
    glm::mat4 GetProjectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    }

    /**
     * @brief 获取相机位置
     * @return glm::vec3 相机位置
     */
    glm::vec3 GetPosition() const {
        return position;
    }

    /**
     * @brief 获取相机朝向
     * @return glm::vec3 相机前方向量
     */
    glm::vec3 GetFront() const {
        return front;
    }

    /**
     * @brief 获取 FOV
     * @return float 视野角度
     */
    float GetFov() const {
        return fov;
    }

    bool IsFPSMode() const { return fpsMode; }

    void ToggleFPSMode() {
        fpsMode = !fpsMode;
        if (fpsMode) position.y = fpsEyeHeight;
    }

    /**
     * @brief 处理键盘输入
     * @param direction 移动方向
     * @param deltaTime 帧时间
     */
    void ProcessKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = movementSpeed * deltaTime;

        if (fpsMode) {
            glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
            glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
            switch (direction) {
                case CameraMovement::FORWARD:  position += flatFront * velocity; break;
                case CameraMovement::BACKWARD: position -= flatFront * velocity; break;
                case CameraMovement::LEFT:     position -= flatRight * velocity; break;
                case CameraMovement::RIGHT:    position += flatRight * velocity; break;
                default: break;
            }
            position.y = fpsEyeHeight;
            return;
        }

        switch (direction) {
            case CameraMovement::FORWARD:
                position += front * velocity;
                break;
            case CameraMovement::BACKWARD:
                position -= front * velocity;
                break;
            case CameraMovement::LEFT:
                position -= right * velocity;
                break;
            case CameraMovement::RIGHT:
                position += right * velocity;
                break;
            case CameraMovement::UP:
                position += worldUp * velocity;
                break;
            case CameraMovement::DOWN:
                position -= worldUp * velocity;
                break;
        }
    }

    /**
     * @brief 处理鼠标移动
     * @param xpos 鼠标 X 坐标
     * @param ypos 鼠标 Y 坐标
     * @param constrainPitch 是否限制俯仰角
     */
    void ProcessMouseMovement(float xpos, float ypos, bool constrainPitch = true) {
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // 注意：Y 坐标是反的
        
        lastX = xpos;
        lastY = ypos;

        xoffset *= mouseSensitivity;
        yoffset *= mouseSensitivity;

        yaw += xoffset;
        pitch += yoffset;

        // 限制俯仰角，防止万向锁
        if (constrainPitch) {
            if (pitch > 89.0f)
                pitch = 89.0f;
            if (pitch < -89.0f)
                pitch = -89.0f;
        }

        UpdateCameraVectors();
    }

    /**
     * @brief 处理鼠标滚轮
     * @param yoffset 滚轮偏移量
     */
    void ProcessMouseScroll(float yoffset) {
        fov -= yoffset;
        if (fov < 1.0f)
            fov = 1.0f;
        if (fov > 90.0f)
            fov = 90.0f;
    }

    /**
     * @brief 处理 GLFW 窗口输入（整合版）
     * @param window GLFW 窗口句柄
     * @param deltaTime 帧时间
     */
    void ProcessInput(GLFWwindow* window, float deltaTime) {
        // ESC 键切换鼠标捕获状态
        static bool escPressed = false;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            if (!escPressed) {
                escPressed = true;
                cursorDisabled = !cursorDisabled;
                if (cursorDisabled) {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    firstMouse = true; // 重置鼠标状态
                } else {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        } else {
            escPressed = false;
        }

        static bool tabPressed = false;
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
            if (!tabPressed) {
                tabPressed = true;
                ToggleFPSMode();
            }
        } else {
            tabPressed = false;
        }

        // WASD 移动
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::RIGHT, deltaTime);
        
        // 垂直移动（空格上升，左 Shift 下降）
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            ProcessKeyboard(CameraMovement::DOWN, deltaTime);

        // 加速移动（左 Ctrl）
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            movementSpeed = CAM_DEFAULT_SPEED * 3.0f;
        else
            movementSpeed = CAM_DEFAULT_SPEED;
    }

    /**
     * @brief 设置鼠标位置（用于初始化）
     * @param x 鼠标 X 坐标
     * @param y 鼠标 Y 坐标
     */
    void SetMousePosition(float x, float y) {
        lastX = x;
        lastY = y;
    }

    /**
     * @brief 重置鼠标首帧标志
     */
    void ResetFirstMouse() {
        firstMouse = true;
    }

    /**
     * @brief 初始化 GLFW 输入模式
     * @param window GLFW 窗口句柄
     */
    void InitInputMode(GLFWwindow* window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

private:
    /**
     * @brief 根据欧拉角更新相机向量
     */
    void UpdateCameraVectors() {
        // 根据偏航角和俯仰角计算新的前方向量
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        
        // 重新计算右方向和上方向向量
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};

#endif // CAMERA_HPP

//
// Created by 29722 on 2026/5/30.
//

#ifndef GRAPHICSLAB_SHADOWMAPPING_CAMERA_H
#define GRAPHICSLAB_SHADOWMAPPING_CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 定义移动方向的枚举
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

// 默认相机参数
const float YAW         = -90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  2.5f;
const float SENSITIVITY =  0.1f;
const float ZOOM        =  45.0f;

class Camera {
public:
    // 相机属性
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    // 欧拉角
    float Yaw;
    float Pitch;

    // 相机选项
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    // 构造函数
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = YAW, float pitch = PITCH)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM) {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVectors();
    }

    // 需求 2：获取观察矩阵 (View Matrix)
    glm::mat4 GetViewMatrix() {
        return glm::lookAt(Position, Position + Front, Up);
    }

    // 需求 2：获取透视投影矩阵 (Projection Matrix)
    glm::mat4 GetProjectionMatrix(float aspectRatio) {
        return glm::perspective(glm::radians(Zoom), aspectRatio, 0.1f, 100.0f);
    }

    // 需求 1：处理键盘输入 (W/A/S/D)
    void ProcessKeyboard(Camera_Movement direction, float deltaTime) {
        float velocity = MovementSpeed * deltaTime;
        if (direction == FORWARD)
            Position += Front * velocity;
        if (direction == BACKWARD)
            Position -= Front * velocity;
        if (direction == LEFT)
            Position -= Right * velocity;
        if (direction == RIGHT)
            Position += Right * velocity;

        // 提示：如果需要真正的“平面移动”（不随俯仰角飞天遁地），可以将 Position.y 强制设为定值
        // Position.y = 0.0f;
    }

    // 需求 1：处理鼠标位置回调 (更新偏航角与俯仰角)
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        // 限制俯仰角，防止万向节死锁和屏幕翻转
        if (constrainPitch) {
            if (Pitch > 89.0f)  Pitch = 89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;
        }

        // 实时更新相机方向向量
        updateCameraVectors();
    }

private:
    // 计算 Front, Right, Up 向量
    void updateCameraVectors() {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);

        // 重新计算 Right 和 Up 向量
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};
#endif
# 计算机图形学大实验 - 实时阴影映射

**技术栈**: C++17 + OpenGL 3.3 Core Profile

## 项目简介

基于前向渲染管线实现的多通路实时阴影映射系统。场景为废弃房屋室内环境，包含方向光（太阳）、点光源（室内灯具）、聚光灯（手电筒）三类光源的 Blinn-Phong 混合光照，方向光与聚光灯各自持有独立 Shadow Map 并通过 PCF 实现软阴影，点光源通过屏幕空间遮挡实现局部遮蔽。

## 编译环境要求

- Windows 11（开发环境）
- CMake 3.15+
- 支持 C++17 的编译器（MSVC 或 MinGW-w64）

## 第三方依赖库

以下库已全部内嵌至工程目录，无需额外安装：

| 库 | 用途 | 路径 |
|---|------|------|
| GLFW 3 | 窗口管理 | `lib/` + `include/GLFW/` |
| GLAD | OpenGL 函数加载 | `src/glad.c` + `include/glad/` |
| GLM | 数学库（向量/矩阵） | `glm/` |
| Dear ImGui | UI 控制面板 | `third_party/` |
| stb_image | 纹理图片加载 | `include/stb_image.h` + `src/stb_image_impl.cpp` |

## 编译与运行

```
mkdir build
cd build
cmake ..
cmake --build .
.\GraphicsLab_ShadowMapping.exe
```

> 程序在 build 目录生成可执行文件，运行时需从项目根目录或 build 目录启动。

## 操作按键说明

| 按键 | 功能 |
|------|------|
| W/A/S/D | 前后左右移动 |
| 鼠标移动 | 视角俯仰与左右环视 |
| ESC | 释放 / 捕获鼠标（用于操作 UI 面板） |
| Tab | 切换 FPS 模式与自由漫游模式 |
| 空格 | FPS 模式下跳跃；自由模式下上升 |
| 左 Shift | 自由模式下下降 |
| 左 Ctrl | 加速移动 |
| E | 开关门 / 开关附近灯具 / 拾取手电筒 |
| F | 开关手电筒 |
| 鼠标滚轮 | 调节视场角 |

## 项目结构

```
├── main.cpp                          # 主程序（渲染管线、碰撞、UI）
├── CMakeLists.txt                    # 构建配置
├── asset/
│   ├── model/                        # 3D 模型文件
│   │   ├── Abandoned_House.obj
│   │   └── Abandoned_House.mtl
│   └── texture/                      # 纹理贴图
├── shaders/                          # GLSL 着色器
│   ├── scene.vert / scene.frag       # 主场景着色器
│   └── shadow_depth.vert / .frag     # 阴影深度着色器
├── include/
│   ├── custom/
│   │   ├── ShadowPipeline.hpp        # 方向光 Shadow Map 管线封装
│   │   ├── Camera.hpp                # FPS/自由双模式相机
│   │   └── Model.hpp                 # 模型加载、Mesh、纹理
│   ├── glad/glad.h                   # OpenGL 加载器
│   ├── GLFW/                         # GLFW 头文件
│   └── stb_image.h                   # 图片加载
├── src/
│   ├── glad.c                        # GLAD 实现
│   └── stb_image_impl.cpp            # stb_image 实现
├── glm/                              # GLM 数学库
├── third_party/                      # Dear ImGui（仅保留 glfw + opengl3 后端）
└── lib/                              # GLFW 库文件
```

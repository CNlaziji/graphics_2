# 成员B - 通用3D模型加载与渲染模块

> **负责人**：成员B  
> **模块名称**：3D模型加载与渲染系统  
> **开发占比**：20%  
> **提交日期**：2026年6月

#补充
其他成员的本地运行操作步骤
git clone <仓库地址>
cd graphics_2

# MinGW 用户：
cmake -B build -S . -G "MinGW Makefiles"
cd build && mingw32-make

# MSVC 用户：
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cd build/Release
./GraphicsLab_ShadowMapping.exe    # 或用VS打开build/GraphicsLab_ShadowMapping.sln
注意：MSVC 用户运行时工作目录必须在项目根目录（graphics_2/），否则找不到 shaders/。可以在 VS 项目属性 → 调试 → 工作目录中设为 $(ProjectDir)..\。


## 一、设计思路

本模块负责场景中所有三维模型的加载、存储与渲染，是连接外部美术资产与GPU渲染管线的桥梁。整体设计遵循以下原则：

### 1.1 架构分层

```
┌─────────────────────────────────────────────┐
│               Model（模型容器）               │
│  - 管理多个 Mesh（子网格）                    │
│  - 场景图递归遍历（Assimp aiNode 树）         │
│  - 世界空间变换矩阵（位置/旋转/缩放）          │
│  - 文件路径 → GPU 数据的完整加载流程           │
└────────────────────┬────────────────────────┘
                     │ 1 : N
┌────────────────────▼────────────────────────┐
│               Mesh（网格单元）                │
│  - 单次 Draw Call 对应的几何数据              │
│  - VAO / VBO / EBO 生命周期管理              │
│  - 顶点属性布局（location 0/1/2）            │
│  - 关联纹理列表                              │
└────────────────────┬────────────────────────┘
                     │
┌────────────────────▼────────────────────────┐
│           Vertex（顶点数据结构）              │
│  Position (vec3) + Normal (vec3)             │
│  + TexCoords (vec2)                         │
└─────────────────────────────────────────────┘
```

### 1.2 三种加载策略（按优先级降级）

| 优先级 | 方式 | 适用场景 | 依赖 |
|--------|------|----------|------|
| **1** | Assimp 跨平台加载 | 生产环境，支持 OBJ/glTF/FBX/Collada 等 40+ 格式 | `#define USE_ASSIMP` + libassimp |
| **2** | 自建 OBJ 解析器 | 无外部依赖的轻量场景，支持 v/vt/vn/f 关键字 | 无（纯 C++ 标准库） |
| **3** | 程序化几何工厂 | 无模型文件时的备选方案，开发调试阶段 | 无（纯数学计算） |

### 1.3 关键设计决策

- **Assimp 作为主加载器**：借助 `aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs` 后处理标志，确保所有输入的几何数据统一为三角形网格、具备平滑法线、UV坐标与OpenGL约定一致。
- **自建OBJ解析器**：支持 `v`（顶点）、`vt`（纹理坐标）、`vn`（法线）、`f`（面）四种关键字，面的格式兼容 `v`、`v/vt`、`v//vn`、`v/vt/vn` 四种变体。采用顶点去重缓存（`std::map` key-value）避免冗余顶点。
- **程序化几何体**：提供 `CreateCube()`、`CreatePlane()`、`CreateSphere()`、`CreateCylinder()`、`CreatePyramid()`、`CreateCone()` 六个工厂方法，基于数学公式直接生成顶点与索引数组，确保在没有任何外部模型文件时程序仍能正常运行并展示场景。
- **RAII 资源管理**：Mesh 类在构造时生成 VAO/VBO/EBO，析构时自动释放，禁止拷贝但允许移动语义，避免 OpenGL 资源泄漏。

---

## 二、核心代码片段

### 2.1 Assimp 场景图递归遍历（核心加载逻辑）

```cpp
/**
 * @brief 递归遍历 Assimp 场景图节点
 * @param node 当前 aiNode 节点
 * @param scene Assimp 场景对象
 *
 * 从根节点开始深度优先遍历 aiNode 树，
 * 提取每个节点的网格索引并转换为本项目 Mesh 格式。
 */
void Model::ProcessNode(aiNode* node, const aiScene* scene) {
    // 处理当前节点关联的所有网格
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(ProcessMesh(ai_mesh, scene));
    }

    // 递归处理所有子节点
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene);
    }
}

/**
 * @brief 将 Assimp aiMesh 转换为本项目的 Mesh 格式
 *
 * 提取顶点位置、法线、纹理坐标、面索引和材质纹理引用。
 */
Mesh Model::ProcessMesh(aiMesh* ai_mesh, const aiScene* scene) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    // 1. 提取顶点数据（位置 + 法线 + UV）
    for (unsigned int i = 0; i < ai_mesh->mNumVertices; i++) {
        Vertex vertex;
        vertex.Position = glm::vec3(
            ai_mesh->mVertices[i].x,
            ai_mesh->mVertices[i].y,
            ai_mesh->mVertices[i].z);

        if (ai_mesh->HasNormals()) {
            vertex.Normal = glm::vec3(
                ai_mesh->mNormals[i].x,
                ai_mesh->mNormals[i].y,
                ai_mesh->mNormals[i].z);
        }

        if (ai_mesh->mTextureCoords[0]) {
            vertex.TexCoords = glm::vec2(
                ai_mesh->mTextureCoords[0][i].x,
                ai_mesh->mTextureCoords[0][i].y);
        } else {
            vertex.TexCoords = glm::vec2(0.0f);
        }
        vertices.push_back(vertex);
    }

    // 2. 提取索引数据（所有三角形面）
    for (unsigned int i = 0; i < ai_mesh->mNumFaces; i++) {
        aiFace& face = ai_mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    // 3. 提取材质纹理引用
    if (ai_mesh->mMaterialIndex >= 0) {
        aiMaterial* material = scene->mMaterials[ai_mesh->mMaterialIndex];
        std::vector<Texture> diffuseMaps =
            LoadMaterialTextures(material, aiTextureType_DIFFUSE, "diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
    }

    return Mesh(vertices, indices, textures);
}
```

### 2.2 自建 OBJ 格式解析器

```cpp
/**
 * @brief 自建 Wavefront OBJ 格式解析器（无需外部依赖）
 * @param path .obj 文件路径
 * @return 是否解析成功
 *
 * 支持的关键字：v(顶点)、vt(纹理坐标)、vn(法线)、f(面)
 * 面格式兼容：f v1 v2 v3 | f v1/vt1 v2/vt2 | f v1//vn1 v2//vn2 | f v1/vt1/vn1
 */
bool Model::LoadOBJ(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[OBJ] 无法打开文件: " << path << std::endl;
        return false;
    }

    std::vector<glm::vec3> tempPositions;
    std::vector<glm::vec2> tempTexCoords;
    std::vector<glm::vec3> tempNormals;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::map<std::string, unsigned int> vertexCache; // 顶点去重

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (prefix == "vt") {
            glm::vec2 tex;
            iss >> tex.x >> tex.y;
            tempTexCoords.push_back(tex);
        }
        else if (prefix == "vn") {
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        }
        else if (prefix == "f") {
            // 扇形三角剖分——读取面顶点并生成三角形扇
            std::vector<std::string> faceVerts;
            std::string token;
            while (iss >> token) faceVerts.push_back(token);

            // 使用第一个顶点作为扇形的公共顶点
            unsigned int first = ParseOBJVertex(faceVerts[0], ...);
            for (size_t i = 1; i + 1 < faceVerts.size(); i++) {
                indices.push_back(first);
                indices.push_back(ParseOBJVertex(faceVerts[i], ...));
                indices.push_back(ParseOBJVertex(faceVerts[i + 1], ...));
            }
        }
    }

    meshes.emplace_back(vertices, indices);
    std::cout << "[OBJ] 成功加载: " << path
              << " (顶点: " << vertices.size()
              << ", 三角面: " << indices.size() / 3 << ")" << std::endl;
    return true;
}
```

### 2.3 Mesh GPU 数据上传

```cpp
void Mesh::SetupMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // VBO：上传顶点数组
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        vertices.size() * sizeof(Vertex),
        vertices.data(), GL_STATIC_DRAW);

    // EBO：上传索引数组
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int),
        indices.data(), GL_STATIC_DRAW);

    // 顶点属性布局（与着色器 layout location 对应）
    // location = 0: Position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)offsetof(Vertex, Position));

    // location = 1: Normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    // location = 2: TexCoords (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glBindVertexArray(0);
}
```

### 2.4 场景物体管理与渲染集成

```cpp
// 场景物体结构体
struct SceneObject {
    std::unique_ptr<Model> model;  // 模型数据（含网格）
    glm::vec3 position;            // 世界空间位置
    glm::vec3 rotation;            // 欧拉角（度数）
    glm::vec3 scale;               // 缩放因子
    glm::vec3 color;               // 物体颜色（基础材质）

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

// 渲染循环（在 RenderScene 插槽中）
void RenderScene(Shader& shader) {
    for (const auto& obj : g_sceneObjects) {
        shader.setMat4("model", obj.GetModelMatrix());
        shader.setVec3("objectColor", obj.color);
        obj.model->Draw(shader);
    }
}
```

---

## 三、场景模型清单（≥5个独立模型）

| 编号 | 模型名称 | 几何类型 | 位置 (x, y, z) | 缩放 (x, y, z) | 颜色 | 用途说明 |
|------|----------|----------|----------------|----------------|------|----------|
| 1 | 地面 | Plane | (0, -1, 0) | (20, 1, 20) | 灰色 (0.5, 0.5, 0.5) | 大面积地面，接受阴影 |
| 2 | 主体建筑 | Cube | (0, 0, 0) | (2, 3, 2) | 沙色 (0.8, 0.6, 0.4) | 场景核心建筑 |
| 3 | 屋顶 | Pyramid | (0, 2, 0) | (1.5, 1.5, 1.5) | 红棕 (0.7, 0.2, 0.1) | 建筑顶部四棱锥屋顶 |
| 4 | 左石柱 | Cylinder | (-2.5, -0.2, 2) | (0.3, 2, 0.3) | 浅灰 (0.9, 0.9, 0.8) | 建筑前方左侧装饰柱 |
| 5 | 右石柱 | Cylinder | (2.5, -0.2, 2) | (0.3, 2, 0.3) | 浅灰 (0.9, 0.9, 0.8) | 建筑前方右侧装饰柱 |
| 6 | 装饰球 | Sphere | (0, 0.5, 3) | (0.6, 0.6, 0.6) | 蓝紫 (0.3, 0.5, 0.8) | 建筑前方装饰球体 |
| 7 | 道具箱A | Cube | (3.5, -0.3, 0) | (0.8, 0.6, 0.8) | 棕色 (0.6, 0.3, 0.2) | 建筑侧面堆叠箱子 |
| 8 | 道具箱B | Cube | (3.5, 0.3, 0) | (0.7, 0.5, 0.7) | 棕色 (0.6, 0.3, 0.2) | 建筑侧面堆叠箱子 |
| 9 | 左圆锥 | Cone | (-3, -0.5, -2.5) | (0.5, 1.5, 0.5) | 绿色 (0.2, 0.7, 0.3) | 建筑后方装饰（树/尖塔） |
| 10 | 右圆锥 | Cone | (3, -0.5, -2.5) | (0.5, 1.5, 0.5) | 绿色 (0.2, 0.7, 0.3) | 建筑后方装饰（树/尖塔） |

> **总计 10 个场景物体，覆盖 6 种几何类型**（Plane、Cube、Pyramid、Cylinder、Sphere、Cone），满足 "≥5个独立模型" 的要求。

---

## 四、实现效果截图

> *（请在此处粘贴程序运行截图，建议包含以下视角）*

### 4.1 场景全景图（白模/线框模式）

```
[截图1：从前方45°角观察完整场景，展示所有10个模型的布局关系]
```

### 4.2 阴影效果验证

```
[截图2：展示地面接收建筑物和柱子的投影效果]
```

### 4.3 多视角对比

```
[截图3：从侧面、顶部等不同角度观察模型的空间关系]
```

### 4.4 线框模式验证几何拓扑

```
[截图4：glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) 模式下的网格结构]
```

---

## 五、遇到的问题与解决方案

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| **程序在 GLAD 初始化时崩溃**（只有 `[GLFW] Initialized successfully`，无后续输出） | `glad.h` 将 `glGetString` 宏定义为 `glad_glGetString`（函数指针），但 `find_coreGL()` 在函数指针赋值之前调用它 → 调用 NULL 指针 → 段错误 | 修改 `src/glad.c`：在 `find_coreGL` 前 `#undef glGetString` 恢复原生系统函数调用，调用后 `#define` 恢复宏定义 |
| **MSVC C4819 警告 / 中文注释导致语法错误** | MSVC 默认使用 GBK(936) 代码页，UTF-8 中文注释被误解析 | 在 `CMakeLists.txt` 添加 `add_compile_options(/utf-8)` |
| **GL_RGB / GL_REPEAT / GL_STATIC_DRAW 等常量未定义** | 该 glad 版本为精简版，缺少部分常用 GL 常量 | 在 `Model.hpp` 中添加 `#ifndef` 保护的常量补充定义（`GL_RGB`, `GL_REPEAT`, `GL_STATIC_DRAW`, `GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`） |
| **Mesh 拷贝构造被删除导致 vector push_back 失败** | Mesh 管理 VAO/VBO/EBO，禁止拷贝；但 `Model(Mesh)` 构造通过 `push_back(const Mesh&)` 尝试拷贝 | 改为 `Model(Mesh mesh)` 按值传参 + `std::move` |
| **Assimp 链接错误** | 未安装 Assimp 库或 CMake 未找到 | 提供备选方案：`#define USE_ASSIMP` 条件编译 + 自建 OBJ 解析器 + 程序化几何体，三层降级策略确保程序始终可编译运行 |
| **OBJ 面索引从1开始** | Wavefront OBJ 规范中顶点/法线/UV的索引以1为起始 | 解析时统一执行 `index - 1` 转换为C++的0起始索引 |
| **OBJ 面格式多变** | 不同建模软件导出的面格式不同（v, v/vt, v//vn, v/vt/vn） | 使用 `std::getline(stream, part, '/')` 按分隔符解析，空字符串表示该分量不存在 |
| **程序化圆柱/圆锥侧面法线** | 需要为每个侧面顶点计算精确法线以获得正确光照 | 使用微分几何公式——圆柱侧面法线 = `normalize(x, 0, z)`，圆锥侧面法线 = `cross(tangent, generator)` 叉积计算 |

### GLAD Crash 详细诊断（核心 Bug 修复）

此 Bug 影响所有使用该版本 glad 生成的 Windows 项目，**不是成员B引入的问题**，但成员B在集成测试中发现并修复。

**问题代码链路**：
```
gladLoadGLLoader() → find_coreGL() → glGetString(GL_VERSION)  [宏展开]
→ glad_glGetString(GL_VERSION)  [此时 = NULL，尚未赋值！]
→ 调用NULL函数指针 → SEGFAULT
```

**修复**（`src/glad.c` 第202行附近）：
```c
#undef glGetString  // 取消宏，使用 opengl32.dll 中的原生函数
const GLubyte* APIENTRY glGetString(GLenum name);

static int find_coreGL(void) {
    // ... 现在调用的是真正的 opengl32.dll 中的 glGetString
}

#define glGetString glad_glGetString  // 恢复宏定义
```

**修复后验证输出**（用户机器实测）：
```
[GLFW] Initialized successfully
[OpenGL] Context initialized successfully
[OpenGL] Version: 3.3.0 NVIDIA 596.21
[OpenGL] Renderer: NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2
[ShadowPipeline] Initialized successfully. Depth texture: 2048x2048
[成员B] 初始化场景模型...
  [1/7] 地面平面已创建
  ...
[成员B] 场景模型初始化完成，共 10 个物体
[RenderLoop] Starting main render loop...
```

---

## 六、个人总结

> *（约200字，请根据实际开发感受调整）*

在本次实验中，我负责开发三维模型加载与渲染系统。通过深入理解 Assimp 的场景图（Scene Graph）数据结构，我掌握了从 `aiNode` 根节点递归遍历、提取 `aiMesh` 几何数据、构建 GPU 端 VAO/VBO/EBO 缓冲的完整流程。

在实现过程中，我设计了三层加载策略：优先使用 Assimp 支持丰富的模型格式；当 Assimp 不可用时，回退到自建的 OBJ 文本解析器，该解析器支持 `v/vt/vn/f` 全部关键字和四种面索引格式；最后提供六个程序化几何体工厂方法作为无外部文件时的备选方案。这种分层降级的设计保证了项目在不同环境下的可编译性和可运行性。

同时，我与组长协作确认了顶点属性布局（location 0/1/2），与成员C约定纹理绑定接口（通过 `Texture` 类传递 OpenGL 纹理 ID），确保各模块在集成时接口一致。通过本次实验，我不仅掌握了现代 OpenGL 的网格渲染管线，也对跨平台资产加载库的使用和容错设计有了更深入的理解。

---

## 七、外部模型加载说明

若需要使用 `.obj` / `.gltf` / `.fbx` 等外部模型文件替代程序化几何体，请按以下步骤操作：

1. **安装 Assimp**：
   ```bash
   # Windows (vcpkg)
   vcpkg install assimp:x64-windows

   # 或从官网下载预编译包
   # https://github.com/assimp/assimp/releases
   ```

2. **启用 Assimp 支持**：在 [Model.hpp](Model.hpp) 第21行取消注释：
   ```cpp
   #define USE_ASSIMP
   ```

3. **更新 CMakeLists.txt** 添加 Assimp 链接。

4. **放置模型文件**到 `models/` 目录，并在 [main.cpp](main.cpp) 的 `InitializeResources()` 中使用：
   ```cpp
   g_sceneObjects.push_back({
       std::make_unique<Model>(Model("models/building.obj")),
       glm::vec3(0.0f, 0.0f, 0.0f),
       glm::vec3(0.0f),
       glm::vec3(1.0f),
       glm::vec3(0.7f)
   });
   ```

---

---

## 八、编译验证与本地运行指南

### 8.1 编译环境

| 项目 | 详情 |
|------|------|
| **编译器** | MSVC 19.44.35225.0 (Visual Studio 2022 Community) |
| **CMake** | 3.31 (VS2022 内置) |
| **GLFW** | 3.4 (lib-vc2022) |
| **C++标准** | C++17 |
| **编译配置** | Release x64 |
| **编译结果** | **0 错误 0 警告** |

### 8.2 编译步骤（已验证通过）

```bash
# 1. 确保 lib/ 目录包含 GLFW 库文件
#    - glfw3.lib (导入库，对应 lib-vc2022/glfw3dll.lib)
#    - glfw3.dll (运行时动态库)
#    - glfw3_mt.lib (静态库，可选)

# 2. CMake 配置
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 3. 编译
cmake --build build --config Release

# 4. 运行（必须在项目根目录 graphics_2/ 下执行）
cd build/Release
./GraphicsLab_ShadowMapping.exe
```

### 8.3 运行预期效果

程序启动后将显示：
- 灰色大面积地面（接受实时阴影）
- 中心沙色建筑 + 红色棱锥屋顶
- 建筑前方：2根石柱、1个蓝紫色装饰球
- 建筑侧面：2个堆叠的棕色箱子
- 建筑后方：2个绿色圆锥（装饰树/尖塔）
- 方向光产生的实时阴影投射在地面上
- 控制台输出：
  ```
  [成员B] 初始化场景模型...
    [1/7] 地面平面已创建
    [2/7] 主体建筑已创建
    ...
  [成员B] 场景模型初始化完成，共 10 个物体
  ```

### 8.4 场景空间布局（俯视图）

```
          Z轴 (深度方向)
          ↑
          │  绿锥(-3, -2.5)     绿锥(3, -2.5)
          │      ●                   ●
          │
          │           ┌──────────┐
          │   石柱    │  建筑    │    石柱  箱子   箱子
          │ (-2.5,2) │ (0,0)    │  (2.5,2) (3.5,0) (3.5,0.6)
          │    │     │  ▲屋顶   │     │     ■      ■
          │    │     └──────────┘     │
          │          装饰球(0, 3)
          │              ○
          │
          │  ═══════════ 地面(0, -1) ═══════════
          │         (20×20 大平面)
          │
    ──────┼──────────────────────────────→ X轴 (右侧方向)
          │
```

---

> **文件清单**（成员B负责的代码文件）：
> - [Model.hpp](../Model.hpp) — 完整的模型加载与渲染系统（~480行）
> - [main.cpp](../main.cpp) — Shader 类重写 + 场景模型初始化（`InitializeResources` 插槽）+ 渲染循环（`RenderScene` 插槽）
> - [CMakeLists.txt](../CMakeLists.txt) — 添加 `/utf-8` 编译标志 + Model.hpp 源文件列表
> - [lib/](../lib/) — GLFW 3.4 VC2022 库文件（glfw3.lib + glfw3.dll）

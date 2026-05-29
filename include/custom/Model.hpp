/**
 * @file Model.hpp
 * @brief 3D模型加载与渲染系统 - 计算机图形学实验二 成员B
 *
 * 实现功能：
 * - Assimp 跨平台模型加载（OBJ/glTF/FBX 等格式）
 * - 自建 OBJ 格式解析器（无外部依赖备选方案）
 * - 程序化几何体生成（Cube/Sphere/Plane/Cylinder/Pyramid）
 * - Mesh 类：VAO/VBO/EBO 封装与 GPU 数据上传
 * - Model 类：多 Mesh 管理、递归场景图遍历、变换矩阵
 *
 * 依赖：OpenGL 3.3+, GLM, Assimp（可选，推荐启用）
 */

#pragma once

#include "glad/glad.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

// ============================================================================
// 补充 glad.h 中缺失的 OpenGL 常量（该 glad 版本为精简版）
// ============================================================================
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif

// ============================================================================
// Assimp 可选依赖
// ============================================================================
// 若安装了 Assimp，取消下一行注释以启用 glTF/FBX/OBJ 等格式的完整支持
// #define USE_ASSIMP

#ifdef USE_ASSIMP
    #include <assimp/Importer.hpp>
    #include <assimp/scene.h>
    #include <assimp/postprocess.h>
#endif

// ============================================================================
// 顶点数据结构
// ============================================================================

/**
 * @struct Vertex
 * @brief 顶点数据：位置 + 法线 + 纹理坐标
 */
struct Vertex {
    glm::vec3 Position;   ///< 顶点位置 (x, y, z)
    glm::vec3 Normal;     ///< 顶点法线 (nx, ny, nz)
    glm::vec2 TexCoords;  ///< 纹理坐标 (u, v)

    Vertex() : Position(0.0f), Normal(0.0f), TexCoords(0.0f) {}

    Vertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& tex)
        : Position(pos), Normal(norm), TexCoords(tex) {}
};

// ============================================================================
// 纹理类（基础实现，成员C可扩展）
// ============================================================================

/**
 * @class Texture
 * @brief 纹理封装（基础实现，供成员C扩展材质系统）
 */
class Texture {
public:
    Texture() : m_id(0), m_type("diffuse") {}

    /// 从已有 OpenGL 纹理 ID 包装（用于外部加载的纹理）
    Texture(unsigned int id, const std::string& type = "diffuse")
        : m_id(id), m_type(type) {}

    /// 创建纯色纹理（无外部图片文件时的备选方案）
    static Texture CreateSolidColor(unsigned char r, unsigned char g, unsigned char b) {
        unsigned int id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        unsigned char data[3] = { r, g, b };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glBindTexture(GL_TEXTURE_2D, 0);
        return Texture(id, "diffuse");
    }

    void Bind(unsigned int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, m_id);
    }

    unsigned int GetID() const { return m_id; }
    const std::string& GetType() const { return m_type; }

private:
    unsigned int m_id;
    std::string m_type;
};

// ============================================================================
// Mesh 类：单次绘制调用对应的几何数据
// ============================================================================

/**
 * @class Mesh
 * @brief 网格数据封装，管理 VAO/VBO/EBO 的创建、绑定与绘制
 *
 * 一个 Mesh 对应一组顶点+索引数据，是 GPU 绘制的最小单元。
 * Model 由多个 Mesh 组成（对应多材质子网格）。
 */
class Mesh {
public:
    std::vector<Vertex> vertices;     ///< 顶点数组
    std::vector<unsigned int> indices; ///< 索引数组
    std::vector<Texture> textures;     ///< 关联的纹理列表

    Mesh() {}

    /**
     * @brief 构造函数：传入几何数据并上传至 GPU
     */
    Mesh(const std::vector<Vertex>& verts,
         const std::vector<unsigned int>& idx,
         const std::vector<Texture>& tex = {})
        : vertices(verts), indices(idx), textures(tex) {
        SetupMesh();
    }

    /**
     * @brief 绘制网格
     * @param shader 当前绑定的着色器程序（通过 Shader::setInt 获取 uniform location）
     *
     * 绘制前绑定纹理，绘制后解绑以恢复状态。
     */
    void Draw(const class Shader& shader) const;

    /**
     * @brief 清理 OpenGL 资源
     */
    void Cleanup() {
        if (m_ebo != 0) glDeleteBuffers(1, &m_ebo);
        if (m_vbo != 0) glDeleteBuffers(1, &m_vbo);
        if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
        m_vao = m_vbo = m_ebo = 0;
    }

    ~Mesh() { Cleanup(); }

    // 禁止拷贝（OpenGL 资源不可拷贝），允许移动
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept
        : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo)
        , vertices(std::move(other.vertices))
        , indices(std::move(other.indices))
        , textures(std::move(other.textures)) {
        other.m_vao = other.m_vbo = other.m_ebo = 0;
    }

private:
    unsigned int m_vao = 0;  ///< 顶点数组对象
    unsigned int m_vbo = 0;  ///< 顶点缓冲对象
    unsigned int m_ebo = 0;  ///< 索引缓冲对象

    void SetupMesh() {
        if (vertices.empty()) return;

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);

        glBindVertexArray(m_vao);

        // 上传顶点数据
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                     vertices.data(), GL_STATIC_DRAW);

        // 上传索引数据
        if (!indices.empty()) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                         indices.data(), GL_STATIC_DRAW);
        }

        // 顶点属性布局 (location = 0, 1, 2)
        // location 0: 位置 vec3
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, Position));

        // location 1: 法线 vec3
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, Normal));

        // location 2: 纹理坐标 vec2
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, TexCoords));

        glBindVertexArray(0);
    }
};

// ============================================================================
// Model 类：场景模型容器
// ============================================================================

/**
 * @class Model
 * @brief 3D 模型封装，管理多个 Mesh 及场景图遍历
 *
 * 支持三种加载方式：
 * 1. Assimp 自动格式检测（需 #define USE_ASSIMP）
 * 2. 自建 OBJ 文本解析器（备选）
 * 3. 程序化几何工厂方法（Cube, Sphere, Plane, Cylinder, Pyramid 等）
 */
class Model {
public:
    std::vector<Mesh> meshes;          ///< 所有子网格
    std::string directory;             ///< 模型文件所在目录（用于加载关联纹理）
    glm::mat4 transform = glm::mat4(1.0f); ///< 世界空间变换矩阵

    Model() {}

    /**
     * @brief 从文件路径加载模型（自动选择 Assimp 或自建 OBJ 解析器）
     * @param path 模型文件路径（相对项目根目录）
     */
    explicit Model(const std::string& path) {
        LoadFromFile(path);
    }

    /**
     * @brief 从程序化生成的网格构造模型
     */
    explicit Model(Mesh mesh) {
        meshes.push_back(std::move(mesh));
    }

    /**
     * @brief 绘制模型的所有子网格
     * @param shader 当前激活的着色器程序
     */
    void Draw(const class Shader& shader) const {
        for (const auto& mesh : meshes) {
            mesh.Draw(shader);
        }
    }

    /**
     * @brief 从文件加载模型
     */
    bool LoadFromFile(const std::string& path) {
        // 提取目录路径
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) {
            directory = path.substr(0, pos + 1);
        }

        std::string ext;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            ext = path.substr(dot);
        }

#ifdef USE_ASSIMP
        return LoadWithAssimp(path);
#else
        if (ext == ".obj" || ext == ".OBJ") {
            return LoadOBJ(path);
        }
        std::cerr << "[Model] 未启用 Assimp 且不是 .obj 文件: " << path << std::endl;
        std::cerr << "[Model] 提示：安装 Assimp 并 #define USE_ASSIMP 以支持更多格式" << std::endl;
        return false;
#endif
    }

#ifdef USE_ASSIMP
    /**
     * @brief 通过 Assimp 加载模型（支持 OBJ/glTF/FBX/Collada 等格式）
     */
    bool LoadWithAssimp(const std::string& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "[Assimp] 加载失败: " << importer.GetErrorString() << std::endl;
            return false;
        }

        std::cout << "[Assimp] 成功加载: " << path
                  << " (网格数: " << scene->mNumMeshes << ")" << std::endl;

        ProcessNode(scene->mRootNode, scene);
        return true;
    }

    /**
     * @brief 递归遍历 Assimp 场景图节点
     * @param node 当前节点
     * @param scene Assimp 场景对象
     *
     * 遍历 aiNode 树，从每个节点的 mMeshes 索引中提取网格数据，
     * 转换为本项目的 Mesh 格式并存入 meshes 容器。
     */
    void ProcessNode(aiNode* node, const aiScene* scene) {
        // 处理当前节点的所有网格
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(ProcessMesh(ai_mesh, scene));
        }

        // 递归处理子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            ProcessNode(node->mChildren[i], scene);
        }
    }

    /**
     * @brief 将 Assimp aiMesh 转换为本项目的 Mesh 格式
     */
    Mesh ProcessMesh(aiMesh* ai_mesh, const aiScene* scene) {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<Texture> textures;

        // 提取顶点数据
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

        // 提取索引数据
        for (unsigned int i = 0; i < ai_mesh->mNumFaces; i++) {
            aiFace& face = ai_mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        // 提取材质/纹理信息
        if (ai_mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[ai_mesh->mMaterialIndex];
            std::vector<Texture> diffuseMaps = LoadMaterialTextures(
                material, aiTextureType_DIFFUSE, "diffuse");
            textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        }

        return Mesh(vertices, indices, textures);
    }

    /**
     * @brief 加载 Assimp 材质中的纹理
     */
    std::vector<Texture> LoadMaterialTextures(aiMaterial* mat,
                                               aiTextureType type,
                                               const std::string& typeName) {
        std::vector<Texture> textures;
        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            mat->GetTexture(type, i, &str);
            // 注意：此处仅记录纹理路径，实际纹理加载由成员C的Texture类完成
            // 当前返回占位纹理
            std::cout << "  [Material] 纹理引用: " << str.C_Str() << std::endl;
        }
        return textures;
    }
#endif // USE_ASSIMP

    // ========================================================================
    // 自建 OBJ 文件解析器（无需外部依赖）
    // ========================================================================

    /**
     * @brief 自建 Wavefront OBJ 格式解析器
     * @param path .obj 文件路径
     * @return 是否解析成功
     *
     * 支持的 OBJ 关键字：
     * - v  (顶点位置), vt (纹理坐标), vn (法线)
     * - f  (面，支持 v, v/vt, v//vn, v/vt/vn 四种格式)
     * - #  (注释行)
     */
    bool LoadOBJ(const std::string& path) {
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
        std::map<std::string, unsigned int> vertexCache;

        auto GetOrCreateVertex = [&](const std::string& key,
                                      const glm::vec3& pos,
                                      const glm::vec2& tex,
                                      const glm::vec3& norm) -> unsigned int {
            auto it = vertexCache.find(key);
            if (it != vertexCache.end()) return it->second;
            unsigned int idx = static_cast<unsigned int>(vertices.size());
            vertices.emplace_back(pos, norm, tex);
            vertexCache[key] = idx;
            return idx;
        };

        std::string line;
        int lineNum = 0;
        while (std::getline(file, line)) {
            lineNum++;
            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            if (prefix == "v") {
                // 顶点位置
                glm::vec3 pos;
                iss >> pos.x >> pos.y >> pos.z;
                tempPositions.push_back(pos);
            }
            else if (prefix == "vt") {
                // 纹理坐标
                glm::vec2 tex;
                iss >> tex.x >> tex.y;
                tempTexCoords.push_back(tex);
            }
            else if (prefix == "vn") {
                // 法线
                glm::vec3 norm;
                iss >> norm.x >> norm.y >> norm.z;
                tempNormals.push_back(norm);
            }
            else if (prefix == "f") {
                // 面（支持三角形和四边形，四边形会拆分为2个三角形）
                std::vector<std::string> faceVertices;
                std::string token;
                while (iss >> token) faceVertices.push_back(token);

                if (faceVertices.size() < 3) {
                    std::cerr << "[OBJ] 行 " << lineNum << ": 面至少需要3个顶点" << std::endl;
                    continue;
                }

                // 第一个顶点作为扇形的公共顶点
                auto parseVertex = [&](const std::string& vstr,
                                        const glm::vec3& defaultPos,
                                        const glm::vec2& defaultTex,
                                        const glm::vec3& defaultNorm) -> unsigned int {
                    std::istringstream viss(vstr);
                    std::string part;
                    int posIdx = -1, texIdx = -1, normIdx = -1;

                    // 解析格式：posIdx/texIdx/normIdx
                    if (std::getline(viss, part, '/')) {
                        if (!part.empty()) posIdx = std::stoi(part);
                    }
                    if (std::getline(viss, part, '/')) {
                        if (!part.empty()) texIdx = std::stoi(part);
                    }
                    if (std::getline(viss, part, '/')) {
                        if (!part.empty()) normIdx = std::stoi(part);
                    }

                    // OBJ 索引从1开始
                    glm::vec3 pos = (posIdx > 0 && posIdx <= (int)tempPositions.size())
                        ? tempPositions[posIdx - 1] : defaultPos;
                    glm::vec2 tex = (texIdx > 0 && texIdx <= (int)tempTexCoords.size())
                        ? tempTexCoords[texIdx - 1] : defaultTex;
                    glm::vec3 norm = (normIdx > 0 && normIdx <= (int)tempNormals.size())
                        ? tempNormals[normIdx - 1] : defaultNorm;

                    return GetOrCreateVertex(vstr, pos, tex, norm);
                };

                unsigned int first = parseVertex(faceVertices[0],
                    glm::vec3(0), glm::vec2(0), glm::vec3(0,1,0));

                // 扇形三角剖分（支持n边形面）
                for (size_t i = 1; i + 1 < faceVertices.size(); i++) {
                    indices.push_back(first);
                    indices.push_back(parseVertex(faceVertices[i],
                        glm::vec3(0), glm::vec2(0), glm::vec3(0,1,0)));
                    indices.push_back(parseVertex(faceVertices[i + 1],
                        glm::vec3(0), glm::vec2(0), glm::vec3(0,1,0)));
                }
            }
        }

        file.close();

        if (vertices.empty()) {
            std::cerr << "[OBJ] 文件中未找到顶点数据: " << path << std::endl;
            return false;
        }

        // 若无索引（纯三角形列表），生成顺序索引
        if (indices.empty()) {
            for (unsigned int i = 0; i < vertices.size(); i++) {
                indices.push_back(i);
            }
        }

        meshes.emplace_back(vertices, indices);
        std::cout << "[OBJ] 成功加载: " << path
                  << " (顶点: " << vertices.size()
                  << ", 三角面: " << indices.size() / 3 << ")" << std::endl;
        return true;
    }

    // ========================================================================
    // 程序化几何体工厂方法（无模型文件时的备选方案）
    // ========================================================================

    /// 生成立方体（边长=1，原点为中心）
    static Model CreateCube() {
        std::vector<Vertex> vertices = {
            // 前面 (z=0.5)
            {{-0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {0,0}}, {{ 0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {1,0}},
            {{ 0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {1,1}}, {{-0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {0,1}},
            // 后面 (z=-0.5)
            {{ 0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {0,0}}, {{-0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {1,0}},
            {{-0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {1,1}}, {{ 0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {0,1}},
            // 右面 (x=0.5)
            {{ 0.5f, -0.5f,  0.5f}, { 1, 0, 0}, {0,0}}, {{ 0.5f, -0.5f, -0.5f}, { 1, 0, 0}, {1,0}},
            {{ 0.5f,  0.5f, -0.5f}, { 1, 0, 0}, {1,1}}, {{ 0.5f,  0.5f,  0.5f}, { 1, 0, 0}, {0,1}},
            // 左面 (x=-0.5)
            {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0,0}}, {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1,0}},
            {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1,1}}, {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {0,1}},
            // 顶面 (y=0.5)
            {{-0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {0,0}}, {{ 0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {1,0}},
            {{ 0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {1,1}}, {{-0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {0,1}},
            // 底面 (y=-0.5)
            {{-0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {0,0}}, {{ 0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {1,0}},
            {{ 0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {1,1}}, {{-0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {0,1}},
        };

        std::vector<unsigned int> indices;
        for (unsigned int face = 0; face < 6; face++) {
            unsigned int base = face * 4;
            indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });
        }

        return Model(Mesh(vertices, indices));
    }

    /// 生成平面（xz平面，边长=1，法线朝上）
    static Model CreatePlane() {
        std::vector<Vertex> vertices = {
            {{-0.5f, 0.0f,  0.5f}, {0, 1, 0}, {0, 1}},
            {{ 0.5f, 0.0f,  0.5f}, {0, 1, 0}, {1, 1}},
            {{ 0.5f, 0.0f, -0.5f}, {0, 1, 0}, {1, 0}},
            {{-0.5f, 0.0f, -0.5f}, {0, 1, 0}, {0, 0}},
        };
        std::vector<unsigned int> indices = { 0, 1, 2, 0, 2, 3 };
        return Model(Mesh(vertices, indices));
    }

    /// 生成经纬球（半径=0.5，分段数可调）
    static Model CreateSphere(unsigned int sectors = 36, unsigned int stacks = 18) {
        float radius = 0.5f;
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        for (unsigned int y = 0; y <= stacks; y++) {
            float v = static_cast<float>(y) / stacks;
            float phi = v * glm::pi<float>();

            for (unsigned int x = 0; x <= sectors; x++) {
                float u = static_cast<float>(x) / sectors;
                float theta = u * 2.0f * glm::pi<float>();

                float px = radius * sin(phi) * cos(theta);
                float py = radius * cos(phi);
                float pz = radius * sin(phi) * sin(theta);

                vertices.push_back({
                    {px, py, pz},
                    {px / radius, py / radius, pz / radius},
                    {u, v}
                });
            }
        }

        for (unsigned int y = 0; y < stacks; y++) {
            for (unsigned int x = 0; x < sectors; x++) {
                unsigned int i0 = y * (sectors + 1) + x;
                unsigned int i1 = i0 + 1;
                unsigned int i2 = (y + 1) * (sectors + 1) + x;
                unsigned int i3 = i2 + 1;
                indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
            }
        }

        return Model(Mesh(vertices, indices));
    }

    /// 生成圆柱体（半径=0.5，高度=1，原点为中心）
    static Model CreateCylinder(unsigned int sectors = 36) {
        float radius = 0.5f, halfH = 0.5f;
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // 侧面
        for (unsigned int i = 0; i <= sectors; i++) {
            float u = static_cast<float>(i) / sectors;
            float theta = u * 2.0f * glm::pi<float>();
            float x = radius * cos(theta);
            float z = radius * sin(theta);
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0, z));

            vertices.push_back({{x, halfH, z}, normal, {u, 0}});   // 顶环
            vertices.push_back({{x, -halfH, z}, normal, {u, 1}});  // 底环
        }

        for (unsigned int i = 0; i < sectors; i++) {
            unsigned int i0 = i * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }

        // 顶面中心点 + 顶环顶点（索引偏移）
        unsigned int topCenter = static_cast<unsigned int>(vertices.size());
        vertices.push_back({{0, halfH, 0}, {0, 1, 0}, {0.5f, 0.5f}});
        for (unsigned int i = 0; i <= sectors; i++) {
            float theta = static_cast<float>(i) / sectors * 2.0f * glm::pi<float>();
            vertices.push_back({{radius * cos(theta), halfH, radius * sin(theta)}, {0, 1, 0},
                                {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}});
        }
        for (unsigned int i = 0; i < sectors; i++) {
            indices.insert(indices.end(), { topCenter, topCenter + 1 + i, topCenter + 2 + i });
        }

        // 底面中心点 + 底环顶点
        unsigned int botCenter = static_cast<unsigned int>(vertices.size());
        vertices.push_back({{0, -halfH, 0}, {0, -1, 0}, {0.5f, 0.5f}});
        for (unsigned int i = 0; i <= sectors; i++) {
            float theta = static_cast<float>(i) / sectors * 2.0f * glm::pi<float>();
            vertices.push_back({{radius * cos(theta), -halfH, radius * sin(theta)}, {0, -1, 0},
                                {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}});
        }
        for (unsigned int i = 0; i < sectors; i++) {
            indices.insert(indices.end(), { botCenter, botCenter + 2 + i, botCenter + 1 + i });
        }

        return Model(Mesh(vertices, indices));
    }

    /// 生成四棱锥（底面边长=1，高度=1，原点为中心）
    static Model CreatePyramid() {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // 底面（4个角 + 中心用于扇形）
        float h = 0.5f;
        glm::vec3 b0(-0.5f, -h,  0.5f), b1( 0.5f, -h,  0.5f);
        glm::vec3 b2( 0.5f, -h, -0.5f), b3(-0.5f, -h, -0.5f);
        glm::vec3 top(0.0f, h, 0.0f);
        glm::vec3 dn(0, -1, 0);

        // 底面
        vertices.insert(vertices.end(), {
            {b0, dn, {0,0}}, {b1, dn, {1,0}}, {b2, dn, {1,1}}, {b3, dn, {0,1}}
        });
        indices.insert(indices.end(), { 0, 1, 2, 0, 2, 3 });

        // 前面三角形（b0, b1, top）
        glm::vec3 fn = glm::normalize(glm::cross(b1 - b0, top - b0));
        unsigned int baseIdx = 4;
        vertices.insert(vertices.end(), {
            {b0, fn, {0,0}}, {b1, fn, {1,0}}, {top, fn, {0.5f, 1}}
        });
        indices.insert(indices.end(), { baseIdx, baseIdx+1, baseIdx+2 });

        // 右面三角形（b1, b2, top）
        glm::vec3 rn = glm::normalize(glm::cross(b2 - b1, top - b1));
        baseIdx += 3;
        vertices.insert(vertices.end(), {
            {b1, rn, {0,0}}, {b2, rn, {1,0}}, {top, rn, {0.5f, 1}}
        });
        indices.insert(indices.end(), { baseIdx, baseIdx+1, baseIdx+2 });

        // 后面三角形（b2, b3, top）
        glm::vec3 bn = glm::normalize(glm::cross(b3 - b2, top - b2));
        baseIdx += 3;
        vertices.insert(vertices.end(), {
            {b2, bn, {0,0}}, {b3, bn, {1,0}}, {top, bn, {0.5f, 1}}
        });
        indices.insert(indices.end(), { baseIdx, baseIdx+1, baseIdx+2 });

        // 左面三角形（b3, b0, top）
        glm::vec3 ln = glm::normalize(glm::cross(b0 - b3, top - b3));
        baseIdx += 3;
        vertices.insert(vertices.end(), {
            {b3, ln, {0,0}}, {b0, ln, {1,0}}, {top, ln, {0.5f, 1}}
        });
        indices.insert(indices.end(), { baseIdx, baseIdx+1, baseIdx+2 });

        return Model(Mesh(vertices, indices));
    }

    /// 生成圆锥（底面半径=0.5，高度=1）
    static Model CreateCone(unsigned int sectors = 36) {
        float radius = 0.5f, halfH = 0.5f;
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        glm::vec3 top(0, halfH, 0);
        glm::vec3 botCenter(0, -halfH, 0);

        // 底面
        unsigned int bcIdx = static_cast<unsigned int>(vertices.size());
        vertices.push_back({botCenter, {0, -1, 0}, {0.5f, 0.5f}});
        for (unsigned int i = 0; i <= sectors; i++) {
            float theta = static_cast<float>(i) / sectors * 2.0f * glm::pi<float>();
            vertices.push_back({
                {radius * cos(theta), -halfH, radius * sin(theta)},
                {0, -1, 0},
                {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}
            });
        }
        for (unsigned int i = 0; i < sectors; i++) {
            indices.insert(indices.end(), { bcIdx, bcIdx + 2 + i, bcIdx + 1 + i });
        }

        // 侧面
        unsigned int sideBase = static_cast<unsigned int>(vertices.size());
        for (unsigned int i = 0; i <= sectors; i++) {
            float theta = static_cast<float>(i) / sectors * 2.0f * glm::pi<float>();
            float x = radius * cos(theta), z = radius * sin(theta);
            glm::vec3 pos(x, -halfH, z);
            glm::vec3 normal = glm::normalize(
                glm::cross(glm::vec3(-z, 0, x), top - pos));
            vertices.push_back({pos, normal, {0, 1}});
        }
        unsigned int topIdx = static_cast<unsigned int>(vertices.size());
        vertices.push_back({top, {0, 1, 0}, {0.5f, 0}});
        for (unsigned int i = 0; i < sectors; i++) {
            indices.insert(indices.end(), { sideBase + i, topIdx, sideBase + i + 1 });
        }

        return Model(Mesh(vertices, indices));
    }
};

// ============================================================================
// Mesh::Draw 实现（放在 Model 类定义之后，避免循环依赖）
// ============================================================================

// 注：此函数在调用方通过 Shader 的 uniform setter 方法来绑定纹理
// 实际使用时需要 main.cpp 中的 Shader 类提供一致接口
// 此处仅为接口说明，具体实现在 RenderScene 中完成
inline void Mesh::Draw(const class Shader& shader) const {
    if (m_vao == 0) return;

    // 绑定纹理（材质系统由成员C实现）
    unsigned int diffuseNr = 0;
    for (size_t i = 0; i < textures.size(); i++) {
        std::string number = (textures[i].GetType() == "diffuse")
            ? std::to_string(diffuseNr++) : "";
        // 纹理绑定委托给 Shader 类的 uniform setter
        textures[i].Bind(static_cast<unsigned int>(i));
    }

    // 绘制
    glBindVertexArray(m_vao);
    if (!indices.empty()) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()),
                       GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    }
    glBindVertexArray(0);

    // 解绑纹理
    for (size_t i = 0; i < textures.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(i));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

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
#include <set>
#include <algorithm>

#include "stb_image.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

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
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
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
    std::string matName;
    std::string groupName;
    bool hasEmissive = false;
    unsigned int emissiveTexID = 0;

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

    Mesh& operator=(Mesh&& other) noexcept {
        if (this != &other) {
            Cleanup();
            m_vao = other.m_vao;
            m_vbo = other.m_vbo;
            m_ebo = other.m_ebo;
            vertices = std::move(other.vertices);
            indices = std::move(other.indices);
            textures = std::move(other.textures);
            other.m_vao = other.m_vbo = other.m_ebo = 0;
        }
        return *this;
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
struct CollisionAABB {
    glm::vec3 min;
    glm::vec3 max;
};

struct LightData {
    glm::vec3 position;
    glm::vec3 color = glm::vec3(1.0f, 0.85f, 0.6f);
    float constant = 1.0f;
    float linear = 0.22f;
    float quadratic = 0.20f;
    float radius = 3.0f;
    bool enabled = false;
};

struct FanData {
    std::vector<int> bladeMeshIndices;
    std::vector<int> shelterMeshIndices;
    glm::vec3 pivot;
    bool spinning = false;
    float angle = 0.0f;
};

class Model {
public:
    std::vector<Mesh> meshes;          ///< 所有子网格
    std::vector<Mesh> doorMeshes;      ///< 门子网格（从主网格分离，支持独立变换）
    std::vector<CollisionAABB> wallColliders;   ///< 竖直面碰撞盒（逐三角面法线分类生成）
    std::vector<CollisionAABB> floorColliders;  ///< 水平面碰撞盒（用于站立检测）
    std::vector<LightData> lightSources;       ///< 从 OBJ 自动检测的灯具光源
    std::vector<FanData> fanGroups;           ///< 从 OBJ 自动检测的吊扇
    std::vector<int> fanBladeIndices;        ///< 所有风扇叶片 mesh 索引（用于快速查找）
    std::map<int, int> bladeToFanIdx;        ///< mesh索引 → fanGroups索引
    std::string directory;             ///< 模型文件所在目录（用于加载关联纹理）
    glm::mat4 transform = glm::mat4(1.0f); ///< 世界空间变换矩阵

    glm::vec3 doorBMin = glm::vec3(0.0f);
    glm::vec3 doorBMax = glm::vec3(0.0f);
    bool hasDoor = false;

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

    bool HasDoor() const { return hasDoor; }
    const std::vector<Mesh>& GetDoorMeshes() const { return doorMeshes; }
    std::vector<Mesh> ExtractDoorMeshes() { return std::move(doorMeshes); }
    const std::vector<CollisionAABB>& GetWallColliders() const { return wallColliders; }
    const std::vector<CollisionAABB>& GetFloorColliders() const { return floorColliders; }
    std::vector<LightData>& GetLightSources() { return lightSources; }
    const std::vector<LightData>& GetLightSources() const { return lightSources; }
    std::vector<FanData>& GetFanGroups() { return fanGroups; }
    const std::vector<FanData>& GetFanGroups() const { return fanGroups; }
    const std::vector<int>& GetFanBladeMeshIndices() const { return fanBladeIndices; }
    glm::vec3 GetDoorCenter() const { return (doorBMin + doorBMax) * 0.5f; }
    glm::vec3 GetDoorSize() const { return doorBMax - doorBMin; }

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
        std::cout << "[OBJ] 开始解析: " << path << std::endl;

        size_t slash = path.find_last_of("/\\");
        std::string dir = (slash != std::string::npos) ? path.substr(0, slash + 1) : "";

        // ====== 第0步：扫描 MTL 引用，解析材质→纹理文件名映射 ======
        std::map<std::string, std::string> matToTexFile;
        std::map<std::string, std::string> matToEmissiveFile;
        {
            std::ifstream hdr(path);
            std::string hl;
            while (std::getline(hdr, hl)) {
                if (hl.rfind("mtllib ", 0) == 0) {
                    std::string mtlPath = hl.substr(7);
                    mtlPath.erase(mtlPath.find_last_not_of(" \t\r\n") + 1);
                    std::string fullMtlPath = dir + mtlPath;
                    std::cout << "[MTL] 读取: " << fullMtlPath << std::endl;
                    std::ifstream mf(fullMtlPath);
                    if (mf.is_open()) {
                        std::string ml, curMtl;
                        while (std::getline(mf, ml)) {
                            if (ml.empty() || ml[0] == '#') continue;
                            if (ml.rfind("newmtl ", 0) == 0) {
                                curMtl = ml.substr(7);
                                curMtl.erase(curMtl.find_last_not_of(" \t\r\n") + 1);
                            } else if (ml.rfind("map_Kd ", 0) == 0 && !curMtl.empty()) {
                                std::string tf = ml.substr(7);
                                tf.erase(tf.find_last_not_of(" \t\r\n") + 1);
                                size_t fs = tf.find_last_of("/\\");
                                std::string fname = (fs != std::string::npos) ? tf.substr(fs + 1) : tf;
                                matToTexFile[curMtl] = fname;
                            } else if (ml.rfind("map_Ke ", 0) == 0 && !curMtl.empty()) {
                                std::string tf = ml.substr(7);
                                tf.erase(tf.find_last_not_of(" \t\r\n") + 1);
                                size_t fs = tf.find_last_of("/\\");
                                std::string fname = (fs != std::string::npos) ? tf.substr(fs + 1) : tf;
                                matToEmissiveFile[curMtl] = fname;
                            }
                        }
                        std::cout << "[MTL] 材质映射: " << matToTexFile.size()
                                  << " 漫反射, " << matToEmissiveFile.size() << " 发光" << std::endl;
                    }
                    break;
                }
            }
        }

        // ====== 第1步：解析 OBJ 几何体 ======
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[OBJ] 无法打开文件: " << path << std::endl;
            return false;
        }

        std::vector<glm::vec3> tempP;
        std::vector<glm::vec2> tempT;
        std::vector<glm::vec3> tempN;

        struct SubMesh {
            std::vector<Vertex> verts;
            std::vector<unsigned int> idx;
            std::string matName;
            std::string groupName;
        };
        std::vector<SubMesh> subMeshes;
        std::string curMat = "default";
        std::string curGroup;
        SubMesh cur;
        std::map<std::string, unsigned int> vcache;

        auto flush = [&]() {
            if (!cur.verts.empty()) {
                if (cur.idx.empty()) {
                    for (unsigned int i = 0; i < cur.verts.size(); i++)
                        cur.idx.push_back(i);
                }
                cur.matName = curMat;
                cur.groupName = curGroup;
                subMeshes.push_back(std::move(cur));
                cur = SubMesh();
                vcache.clear();
            }
        };

        std::string line;
        int lineNum = 0;
        while (std::getline(file, line)) {
            lineNum++;
            if (line.empty() || line[0] == '#') continue;

            char first = line[0];

            if (first == 'u') {
                if (line.rfind("usemtl ", 0) == 0) {
                    flush();
                    curMat = line.substr(7);
                    curMat.erase(curMat.find_last_not_of(" \t\r\n") + 1);
                }
                continue;
            }
            if (first == 'm' || first == 'o' || first == 's') continue;
            if (first == 'g') {
                flush();
                curGroup = line.substr(2);
                curGroup.erase(curGroup.find_last_not_of(" \t\r\n") + 1);
                continue;
            }

            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            if (prefix == "v") {
                float x, y, z;
                if (iss >> x >> y >> z) tempP.emplace_back(x, y, z);
            }
            else if (prefix == "vt") {
                float u, v;
                if (iss >> u >> v) tempT.emplace_back(u, v);
            }
            else if (prefix == "vn") {
                float nx, ny, nz;
                if (iss >> nx >> ny >> nz) tempN.emplace_back(nx, ny, nz);
            }
            else if (prefix == "f") {
                std::vector<std::string> fv;
                std::string tok;
                while (iss >> tok) fv.push_back(tok);
                if (fv.size() < 3) continue;

                struct FVI { int p, t, n; bool hasT, hasN; };
                std::vector<FVI> face;
                for (auto& s : fv) {
                    FVI f = {0, 0, 0, false, false};
                    const char* c = s.c_str();
                    f.p = atoi(c);
                    const char* s1 = strchr(c, '/');
                    if (s1) {
                        f.t = atoi(s1+1);
                        f.hasT = true;
                        const char* s2 = strchr(s1+1, '/');
                        if (s2) { f.n = atoi(s2+1); f.hasN = true; }
                    }
                    face.push_back(f);
                }

                auto resolveP = [&](int pi, bool) -> glm::vec3 { return
                    (pi > 0 && pi <= (int)tempP.size()) ? tempP[pi-1] :
                    (pi < 0 && (-pi) <= (int)tempP.size()) ? tempP[tempP.size()+pi] : glm::vec3(0); };
                auto resolveT = [&](int ti, bool hasT) -> glm::vec2 { return
                    hasT ?
                    ((ti > 0 && ti <= (int)tempT.size()) ? tempT[ti-1] :
                     (ti < 0 && (-ti) <= (int)tempT.size()) ? tempT[tempT.size()+ti] : glm::vec2(0))
                    : glm::vec2(0); };
                auto resolveN = [&](int ni, bool hasN) -> glm::vec3 { return
                    hasN ?
                    ((ni > 0 && ni <= (int)tempN.size()) ? tempN[ni-1] :
                     (ni < 0 && (-ni) <= (int)tempN.size()) ? tempN[tempN.size()+ni] : glm::vec3(0,1,0))
                    : glm::vec3(0,1,0); };

                auto addVert = [&](const FVI& fvi) -> unsigned int {
                    std::string key = std::to_string(fvi.p)+"/"+std::to_string(fvi.t)+"/"+std::to_string(fvi.n);
                    auto it = vcache.find(key);
                    if (it != vcache.end()) return it->second;
                    unsigned int idx = (unsigned int)cur.verts.size();
                    cur.verts.emplace_back(resolveP(fvi.p, false), resolveN(fvi.n, fvi.hasN), resolveT(fvi.t, fvi.hasT));
                    vcache[key] = idx;
                    return idx;
                };

                unsigned int i0 = addVert(face[0]);
                for (size_t j = 1; j + 1 < face.size(); j++) {
                    unsigned int i1 = addVert(face[j]);
                    unsigned int i2 = addVert(face[j+1]);
                    cur.idx.push_back(i0);
                    cur.idx.push_back(i1);
                    cur.idx.push_back(i2);
                }
            }
        }
        flush();
        file.close();

        std::cout << "[OBJ] 几何解析完成: " << lineNum << "行, "
                  << subMeshes.size() << "个子网格" << std::endl;

        int totalV = 0, totalF = 0;
        for (auto& sm : subMeshes) { totalV += (int)sm.verts.size(); totalF += (int)sm.idx.size()/3; }
        std::cout << "[OBJ] 总顶点: " << totalV << ", 总三角面: " << totalF << std::endl;

        glm::vec3 doorBBMin(1e10f), doorBBMax(-1e10f);
        int wallCount = 0, floorCount = 0;
        const float WALL_PAD = 0.15f;
        const float WALL_NORMAL_Y_MAX = 0.5f;
        const float FLOOR_NORMAL_Y_MIN = 0.55f;

        for (auto& sm : subMeshes) {
            if (sm.verts.empty() || sm.idx.size() < 3) continue;

            glm::vec2 uvMin(1e10f), uvMax(-1e10f);
            for (auto& v : sm.verts) {
                uvMin.x = std::min(uvMin.x, v.TexCoords.x);
                uvMin.y = std::min(uvMin.y, v.TexCoords.y);
                uvMax.x = std::max(uvMax.x, v.TexCoords.x);
                uvMax.y = std::max(uvMax.y, v.TexCoords.y);
            }

            if (sm.matName == "Puerta") {
                glm::vec3 bbMin(1e10f), bbMax(-1e10f);
                for (auto& v : sm.verts) {
                    bbMin = glm::min(bbMin, v.Position);
                    bbMax = glm::max(bbMax, v.Position);
                }
                doorBBMin = glm::min(doorBBMin, bbMin);
                doorBBMax = glm::max(doorBBMax, bbMax);
                glm::vec3 size = bbMax - bbMin;
                std::cout << "[Door] " << sm.matName << " faces=" << sm.idx.size()/3
                          << " bb=[" << bbMin.x << "," << bbMin.y << "," << bbMin.z << "]~["
                          << bbMax.x << "," << bbMax.y << "," << bbMax.z << "]"
                          << " center=[" << (bbMin.x+bbMax.x)/2 << "," << (bbMin.y+bbMax.y)/2 << "," << (bbMin.z+bbMax.z)/2 << "]"
                          << " size=[" << size.x << "," << size.y << "," << size.z << "]"
                          << std::endl;
            } else {
                for (size_t t = 0; t + 2 < sm.idx.size(); t += 3) {
                    const glm::vec3& v0 = sm.verts[sm.idx[t]].Position;
                    const glm::vec3& v1 = sm.verts[sm.idx[t+1]].Position;
                    const glm::vec3& v2 = sm.verts[sm.idx[t+2]].Position;
                    glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                    if (std::isnan(normal.x)) continue;

                    glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
                    glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);
                    float triHeight = triMax.y - triMin.y;
                    float absNY = std::abs(normal.y);

                    if (absNY < WALL_NORMAL_Y_MAX) {
                        if (triHeight >= 0.25f) {
                            CollisionAABB box;
                            box.min = triMin;
                            box.max = triMax;
                            float nxz = std::sqrt(normal.x * normal.x + normal.z * normal.z);
                            if (nxz > 0.001f) {
                                float dx = normal.x / nxz * WALL_PAD;
                                float dz = normal.z / nxz * WALL_PAD;
                                box.min.x -= std::abs(dx);
                                box.max.x += std::abs(dx);
                                box.min.z -= std::abs(dz);
                                box.max.z += std::abs(dz);
                            } else {
                                box.min.x -= WALL_PAD;
                                box.max.x += WALL_PAD;
                                box.min.z -= WALL_PAD;
                                box.max.z += WALL_PAD;
                            }
                            wallColliders.push_back(box);
                            wallCount++;
                        }
                    } else if (normal.y >= FLOOR_NORMAL_Y_MIN) {
                        CollisionAABB box;
                        box.min = triMin;
                        box.max = triMax;
                        box.min.y -= 0.05f;
                        box.max.y += 0.05f;
                        floorColliders.push_back(box);
                        floorCount++;
                    }
                }
            }
            float rangeX = uvMax.x - uvMin.x;
            float rangeY = uvMax.y - uvMin.y;
            if (rangeX > 2.0f || rangeY > 2.0f || uvMin.x < -0.5f || uvMin.y < -0.5f) {
                float scaleX = (rangeX > 0.0001f) ? 1.0f / rangeX : 1.0f;
                float scaleY = (rangeY > 0.0001f) ? 1.0f / rangeY : 1.0f;
                float s = std::min(scaleX, scaleY);
                for (auto& v : sm.verts) {
                    v.TexCoords.x = (v.TexCoords.x - uvMin.x) * s;
                    v.TexCoords.y = (v.TexCoords.y - uvMin.y) * s;
                }
            }
        }
        std::cout << "[Collision] 墙面碰撞盒: " << wallCount
                  << ", 地面碰撞盒: " << floorCount << std::endl;

        // ====== 第2步：逐材质加载纹理 ======
        std::map<std::string, Texture> loadedTextures;
        std::map<std::string, unsigned int> loadedEmissive;
        std::vector<std::string> missingTex;

        auto findTexFile = [](const std::string& texName) -> std::string {
            std::string candidates[12];
            int nCand = 0;
            std::string base = texName;
            size_t dot = base.find_last_of('.');
            std::string stem = (dot != std::string::npos) ? base.substr(0, dot) : base;
            std::string ext  = (dot != std::string::npos) ? base.substr(dot) : "";

            candidates[nCand++] = "../asset/texture/" + base;
            candidates[nCand++] = "asset/texture/" + base;
            candidates[nCand++] = "../asset/texture/" + base + ".001";
            candidates[nCand++] = "asset/texture/" + base + ".001";
            candidates[nCand++] = "../asset/texture/" + stem + ".001" + ext;
            candidates[nCand++] = "asset/texture/" + stem + ".001" + ext;
            candidates[nCand++] = "../asset/texture/" + base + ".001" + ext;
            candidates[nCand++] = "asset/texture/" + base + ".001" + ext;
#ifdef _WIN32
            int wlen = MultiByteToWideChar(CP_UTF8, 0, stem.c_str(), -1, nullptr, 0);
            if (wlen > 1) {
                std::wstring wstem(wlen - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, stem.c_str(), -1, &wstem[0], wlen);
                std::wstring pat = L"../asset/texture/" + wstem + L"*";
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW(pat.c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        int u8len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
                        std::string u8name(u8len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, &u8name[0], u8len, nullptr, nullptr);
                        candidates[nCand++] = "../asset/texture/" + u8name;
                        candidates[nCand++] = "asset/texture/" + u8name;
                    } while (0);
                    FindClose(h);
                }
            }
#endif
            auto fileExists = [](const std::string& path) -> bool {
#ifdef _WIN32
                int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
                if (wlen <= 1) return false;
                std::wstring wp(wlen - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wp[0], wlen);
                DWORD attr = GetFileAttributesW(wp.c_str());
                return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
                std::ifstream test(path, std::ios::binary);
                return test.good();
#endif
            };

            for (int i = 0; i < nCand; i++) {
                if (candidates[i].empty()) continue;
                if (fileExists(candidates[i])) return candidates[i];
            }
            return "";
        };

        auto loadTexFromFile = [](const std::string& filePath) -> unsigned int {
#ifdef _WIN32
            int wlen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
            std::wstring wpath(wlen - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wpath[0], wlen);
            HANDLE fh = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fh == INVALID_HANDLE_VALUE) return 0;
            DWORD fsize = GetFileSize(fh, nullptr);
            std::vector<unsigned char> buf(fsize);
            DWORD bread = 0;
            ReadFile(fh, buf.data(), fsize, &bread, nullptr);
            CloseHandle(fh);
#else
            std::ifstream in(filePath, std::ios::binary | std::ios::ate);
            if (!in.is_open()) return 0;
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<unsigned char> buf((size_t)size);
            if (!in.read((char*)buf.data(), size)) return 0;
#endif

            int iw = 0, ih = 0, ch = 0;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load_from_memory(buf.data(), (int)buf.size(), &iw, &ih, &ch, 0);
            if (!data) return 0;

            unsigned int texID = 0;
            glGenTextures(1, &texID);
            glBindTexture(GL_TEXTURE_2D, texID);
            GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, iw, ih, 0, fmt, GL_UNSIGNED_BYTE, data);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            stbi_image_free(data);

            std::cout << "[Tex] " << filePath << " (" << iw << "x" << ih << ") id=" << texID << std::endl;
            std::cout.flush();
            return texID;
        };

        for (auto& sm : subMeshes) {
            if (sm.matName.empty() || sm.matName == "default") continue;
            auto it = loadedTextures.find(sm.matName);
            if (it != loadedTextures.end()) continue;
            auto mf = matToTexFile.find(sm.matName);
            if (mf == matToTexFile.end()) {
                missingTex.push_back(sm.matName + "(无MTL)");
                continue;
            }

            std::string texPath = findTexFile(mf->second);
            if (texPath.empty()) {
                missingTex.push_back(sm.matName + ":" + mf->second);
                continue;
            }

            unsigned int texID = loadTexFromFile(texPath);
            if (texID) {
                loadedTextures[sm.matName] = Texture(texID, "diffuse");
                std::cout << "[Tex] " << sm.matName << " -> " << texPath << " id=" << texID << std::endl;
                std::cout.flush();
            } else {
                missingTex.push_back(sm.matName + ":" + mf->second + "(stbi失败)");
            }
        }

        if (!missingTex.empty()) {
            std::cout << "[Tex] 缺失纹理 (" << missingTex.size() << "个): ";
            for (auto& m : missingTex) std::cout << m << "; ";
            std::cout << std::endl;
            std::cout.flush();
        }

        for (auto& sm : subMeshes) {
            auto it = matToEmissiveFile.find(sm.matName);
            if (it == matToEmissiveFile.end()) continue;
            std::string texPath = findTexFile(it->second);
            if (texPath.empty()) continue;
            unsigned int texID = loadTexFromFile(texPath);
            if (texID) {
                loadedEmissive[sm.matName] = texID;
                std::cout << "[Emissive] " << sm.matName << " -> " << texPath
                          << " id=" << texID << std::endl;
                std::cout.flush();
            }
        }

        auto isLampMat = [](const std::string& name) -> bool {
            std::string lower;
            for (char c : name) lower += (char)tolower((unsigned char)c);
            return lower.find("lampara") != std::string::npos
                || lower.find("emisor") != std::string::npos
                || lower.find("foco") != std::string::npos;
        };

        for (auto& sm : subMeshes) {
            if (sm.verts.empty()) continue;
            std::string matNameLower;
            for (char c : sm.matName) matNameLower += (char)tolower((unsigned char)c);
            auto& lc = matNameLower;
            bool isFoco   = lc == "foco"   || lc.rfind("foco.", 0)   == 0;
            bool isEmisor = lc == "emisor" || lc.rfind("emisor.", 0) == 0;
            bool isLampara = lc == "lampara1" || lc == "lampara2"
                          || lc.rfind("lampara3", 0) == 0 || lc == "lampara3.001";
            if (!isFoco && !isEmisor && !isLampara) continue;
            if (isFoco) continue;

            glm::vec3 bbMin(1e10f), bbMax(-1e10f);
            for (auto& v : sm.verts) {
                bbMin = glm::min(bbMin, v.Position);
                bbMax = glm::max(bbMax, v.Position);
            }
            glm::vec3 center = (bbMin + bbMax) * 0.5f;
            glm::vec3 size = bbMax - bbMin;

            LightData ld;
            ld.position = center;
            if (isEmisor) {
                ld.color = glm::vec3(0.80f, 0.45f, 0.01f);
                ld.radius = 4.0f;
            } else {
                ld.color = glm::vec3(1.0f, 0.85f, 0.55f);
                if (lc == "lampara1")      ld.radius = 2.5f;
                else if (lc == "lampara2") ld.radius = 4.0f;
                else                       ld.radius = 3.0f;
            }
            lightSources.push_back(ld);
        }

        std::cout << "[Light] 检测到 " << lightSources.size() << " 个灯具光源" << std::endl;
        std::cout.flush();

        if (doorBBMin.x < 1e9f) {
            doorBMin = doorBBMin;
            doorBMax = doorBBMax;
            hasDoor = true;
            std::cout << "[Door] 全局包围盒: [" << doorBBMin.x << "," << doorBBMin.y << "," << doorBBMin.z
                      << "]~[" << doorBBMax.x << "," << doorBBMax.y << "," << doorBBMax.z << "]"
                      << " center=[" << (doorBBMin.x+doorBBMax.x)/2 << "," << (doorBBMin.y+doorBBMax.y)/2 << "," << (doorBBMin.z+doorBBMax.z)/2 << "]"
                      << std::endl;
        }

        // ====== 预检：从 subMeshes 检测吊扇 ======
        std::map<std::string, std::vector<int>> fanSMGroups;
        std::map<std::string, std::string> fanKeyLower;  // lower → original
        for (int si = 0; si < (int)subMeshes.size(); si++) {
            auto& sm = subMeshes[si];
            if (sm.groupName.empty()) continue;
            std::string gn = sm.groupName;
            size_t us = gn.find('_');
            if (us == std::string::npos) continue;
            std::string base = gn.substr(0, us);
            std::string lower; for (char c : base) lower += (char)tolower((unsigned char)c);
            if (lower != "ventilador" && lower.rfind("ventilador.", 0) != 0) continue;
            fanSMGroups[base].push_back(si);
            fanKeyLower[lower] = base;
        }
        // 第二遍：检测 v / v.NNN 扇叶组，归入对应 Ventilador
        for (int si = 0; si < (int)subMeshes.size(); si++) {
            auto& sm = subMeshes[si];
            if (sm.groupName.empty()) continue;
            std::string gn = sm.groupName;
            size_t us = gn.find('_');
            if (us == std::string::npos) continue;
            std::string base = gn.substr(0, us);
            std::string lower; for (char c : base) lower += (char)tolower((unsigned char)c);
            if (lower != "v" && lower.rfind("v.", 0) != 0) continue;
            std::string suffix = lower.substr(1);
            std::string fanKey = "ventilador" + suffix;
            auto it = fanKeyLower.find(fanKey);
            if (it != fanKeyLower.end()) {
                fanSMGroups[it->second].push_back(si);
            }
        }
        std::cout << "[Fan PRE] 风扇组=" << fanSMGroups.size() << std::endl;

        // ====== 第3步：创建 Mesh，绑定纹理 ======
        std::vector<int> smToMeshIdx(subMeshes.size(), -1);
        for (int si = 0; si < (int)subMeshes.size(); si++) {
            auto& sm = subMeshes[si];
            std::vector<Texture> texs;
            auto it = loadedTextures.find(sm.matName);
            if (it != loadedTextures.end()) texs.push_back(it->second);
            if (sm.matName == "Puerta") {
                doorMeshes.emplace_back(sm.verts, sm.idx, texs);
            } else {
                smToMeshIdx[si] = (int)meshes.size();
                meshes.emplace_back(sm.verts, sm.idx, texs);
                meshes.back().matName = sm.matName;
                auto ei = loadedEmissive.find(sm.matName);
                if (ei != loadedEmissive.end()) {
                    meshes.back().hasEmissive = true;
                    meshes.back().emissiveTexID = ei->second;
                }
            }
        }
        std::cout << "[OBJ] 完成: " << meshes.size() << "个mesh, "
                  << loadedTextures.size() << "个纹理, "
                  << loadedEmissive.size() << "个发光";
        if (hasDoor) std::cout << ", 门=" << doorMeshes.size() << "个子网格";
        std::cout << std::endl;
        std::cout.flush();

        // ====== 风扇后处理：subMesh索引 → mesh索引 ======
        for (auto& kv : fanSMGroups) {
            FanData fd;
            glm::vec3 bbMin(1e10f), bbMax(-1e10f);
            glm::vec3 vbMin(1e10f), vbMax(-1e10f);
            for (int si : kv.second) {
                int mi = smToMeshIdx[si];
                if (mi < 0) continue;
                auto& sm = subMeshes[si];
                fd.bladeMeshIndices.push_back(mi);
                fanBladeIndices.push_back(mi);
                bladeToFanIdx[mi] = (int)fanGroups.size();
                for (auto& v : sm.verts) {
                    bbMin = glm::min(bbMin, v.Position);
                    bbMax = glm::max(bbMax, v.Position);
                }
                if (sm.groupName.find("Ventilador") == 0 || sm.groupName.find("ventilador") == 0) {
                    for (auto& v : sm.verts) {
                        vbMin = glm::min(vbMin, v.Position);
                        vbMax = glm::max(vbMax, v.Position);
                    }
                }
            }
            if (vbMin.x < 1e9f) fd.pivot = (vbMin + vbMax) * 0.5f;
            else fd.pivot = (bbMin + bbMax) * 0.5f;
            fanGroups.push_back(std::move(fd));
        }
        if (!fanGroups.empty()) {
            std::cout << "[Fan] 检测到 " << fanGroups.size() << " 个吊扇, "
                      << fanBladeIndices.size() << " 个叶片mesh" << std::endl;
            for (int i = 0; i < (int)fanGroups.size(); i++) {
                std::cout << "  Fan#" << i << " pivot=(" << fanGroups[i].pivot.x << "," << fanGroups[i].pivot.y << "," << fanGroups[i].pivot.z << ") blades=" << fanGroups[i].bladeMeshIndices.size() << std::endl;
            }
        }

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

    unsigned int diffuseNr = 0;
    for (size_t i = 0; i < textures.size(); i++) {
        if (textures[i].GetType() == "diffuse") {
            textures[i].Bind(static_cast<unsigned int>(i));
            diffuseNr++;
        }
    }

    glBindVertexArray(m_vao);
    if (!indices.empty()) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()),
                       GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    }
    glBindVertexArray(0);

    for (size_t i = 0; i < textures.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(i));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

struct Shader
{
    GLuint id;
    Shader(const char *vsPath, const char *fsPath)
    {
        auto load = [&](const char *path)
        {
            std::ifstream f(path);
            return std::string(
                std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
        };
        auto compile = [&](const std::string &src, GLenum type)
        {
            GLuint s = glCreateShader(type);
            const char *cstr = src.c_str();
            glShaderSource(s, 1, &cstr, nullptr);
            glCompileShader(s);
            GLint ok;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char buf[512];
                glGetShaderInfoLog(s, 512, nullptr, buf);
                std::cerr << "Shader compile error: " << buf << std::endl;
            }
            return s;
        };
        std::string vs = load(vsPath), fs = load(fsPath);
        GLuint vsID = compile(vs, GL_VERTEX_SHADER);
        GLuint fsID = compile(fs, GL_FRAGMENT_SHADER);
        id = glCreateProgram();
        glAttachShader(id, vsID);
        glAttachShader(id, fsID);
        glLinkProgram(id);
        glDeleteShader(vsID);
        glDeleteShader(fsID);
    }
    void use() const { glUseProgram(id); }
};

struct TextureInfo
{
    GLuint id = 0;
    std::string type; // 例如 "texture_diffuse", "texture_specular"
    std::string path; // 加载纹理时的完整路径，用于缓存查找
};

struct Mesh
{
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;
    std::vector<TextureInfo> textures; // 每个 Mesh 可以有多个纹理

    Mesh() = default;

    // 构造函数现在接收纹理信息
    Mesh(const std::vector<float> &vertexData,
         const std::vector<unsigned int> &indices,
         std::vector<TextureInfo> meshTextures)                                  // 新增参数
        : indexCount((GLsizei)indices.size()), textures(std::move(meshTextures)) // 存储纹理
    {
        if (indexCount == 0 || vertexData.empty())
            return;

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     vertexData.size() * sizeof(float),
                     vertexData.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(unsigned int),
                     indices.data(), GL_STATIC_DRAW);

        // 顶点布局: 位置(3) + 法线(3) + 颜色(3) + 纹理坐标(2) = 11 floats
        GLsizei stride = 11 * sizeof(float);
        // 位置属性 (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        // 法线属性 (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        // 颜色属性 (location = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
        // 纹理坐标属性 (location = 3) - 新增
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void *)(9 * sizeof(float)));

        glBindVertexArray(0);
    }

    ~Mesh()
    { /* ... (析构函数保持不变，清理VAO, VBO, EBO) ... */
        if (EBO != 0)
            glDeleteBuffers(1, &EBO);
        if (VBO != 0)
            glDeleteBuffers(1, &VBO);
        if (VAO != 0)
            glDeleteVertexArrays(1, &VAO);
    }
    Mesh(Mesh &&other) noexcept
        : VAO(other.VAO), VBO(other.VBO), EBO(other.EBO), indexCount(other.indexCount), textures(std::move(other.textures))
    {
        other.VAO = 0;
        other.VBO = 0;
        other.EBO = 0;
        other.indexCount = 0;
    }
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other)
        {
            if (EBO != 0)
                glDeleteBuffers(1, &EBO);
            if (VBO != 0)
                glDeleteBuffers(1, &VBO);
            if (VAO != 0)
                glDeleteVertexArrays(1, &VAO);
            VAO = other.VAO;
            VBO = other.VBO;
            EBO = other.EBO;
            indexCount = other.indexCount;
            textures = std::move(other.textures);
            other.VAO = 0;
            other.VBO = 0;
            other.EBO = 0;
            other.indexCount = 0;
        }
        return *this;
    }
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    // draw 函数现在需要 Shader 对象来设置 uniform
    void draw(const Shader &shaderProgram) const
    {
        if (VAO == 0 || indexCount == 0)
            return;

        bool hasDiffuseTexture = false;
        unsigned int diffuseTextureUnit = 0; // 我们将漫反射纹理绑定到纹理单元0

        for (const auto &texInfo : textures)
        {
            if (texInfo.type == "texture_diffuse" && texInfo.id != 0)
            {
                glActiveTexture(GL_TEXTURE0 + diffuseTextureUnit); // 通常是 GL_TEXTURE0
                glBindTexture(GL_TEXTURE_2D, texInfo.id);
                // 在 shader 中，漫反射采样器 uniform (例如 uDiffuseSampler) 应设为 diffuseTextureUnit (这里是0)
                hasDiffuseTexture = true;
                break; // 简化：只使用找到的第一个漫反射纹理
            }
        }

        shaderProgram.use(); // 确保着色器已激活
        glUniform1i(glGetUniformLocation(shaderProgram.id, "uHasDiffuseTexture"), hasDiffuseTexture ? 1 : 0);
        // uDiffuseSampler uniform 在主渲染循环中设置一次即可，如果它总是纹理单元0

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
        // if (hasDiffuseTexture) {
        //     glBindTexture(GL_TEXTURE_2D, 0); // 解绑纹理（可选）
        // }
    }
};

struct LightConfig
{
    glm::vec3 position;     // 光源位置
    glm::vec3 color;        // 光源颜色 (会影响环境光、漫反射光和镜面光)
    float ambientStrength;  // 环境光强度
    float specularStrength; // 镜面光强度
    float shininess;        // 高光指数 (影响高光锐利程度)
};

struct CameraController
{

    // 灵敏度
    static constexpr float kZoomSpeed = 0.25f;
    static constexpr float kPanSpeed = 0.005f; // 更小的步长
    static constexpr float kRotateSpeed = 0.1f;

    static constexpr float minRadius = 0.01f;
    static constexpr float maxRadius = 100.0f;

    CameraController(GLFWwindow *window)
        : radius(1.0f),
          yaw(-90.0f),
          pitch(0.0f),
          target(0.0f, 0.0f, 0.0f),
          // 在这里“捕获”一份初始状态
          initRadius(radius),
          initYaw(yaw),
          initPitch(pitch),
          initTarget(target),
          dragging(false),
          dragButton(GLFW_MOUSE_BUTTON_LEFT),
          dragMods(0),
          lastX(0), lastY(0)
    {
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, ScrollCallback);
        glfwSetKeyCallback(window, KeyCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetCursorPosCallback(window, CursorPosCallback);
    }

    // 外部获取视图矩阵
    glm::mat4 getViewMatrix(glm::vec3 &outCamPos) const
    {
        outCamPos.x = target.x + radius * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        outCamPos.y = target.y + radius * sin(glm::radians(pitch));
        outCamPos.z = target.z + radius * sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::lookAt(outCamPos, target, {0, 1, 0});
    }

    // 调用恢复到初始状态
    void reset()
    {
        radius = initRadius;
        yaw = initYaw;
        pitch = initPitch;
        target = initTarget;
    }

private:
    // —— 当前可变状态 ——
    float radius;
    float yaw;
    float pitch;
    glm::vec3 target;

    // —— 构造时备份的初始状态（private） ——
    float initRadius;
    float initYaw;
    float initPitch;
    glm::vec3 initTarget;

    // 拖拽状态
    bool dragging;
    int dragButton;
    int dragMods;
    double lastX, lastY;
    // 滚轮缩放（不变）
    static void ScrollCallback(GLFWwindow *w, double /*x*/, double y)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam)
            return;
        cam->radius = glm::clamp(cam->radius - float(y) * kZoomSpeed, CameraController::minRadius, CameraController::maxRadius);
    }

    // 键盘平移（不变）
    static void KeyCallback(GLFWwindow *w, int key, int /*sc*/, int action, int /*mods*/)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam || (action != GLFW_PRESS && action != GLFW_REPEAT))
            return;

        if (key == GLFW_KEY_R)
        {
            cam->reset();
        }
    }

    // 鼠标按键处理：左键拖拽旋转，中键拖拽平移，滚轮缩放
    static void MouseButtonCallback(GLFWwindow *w, int button, int action, int mods)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam)
            return;
        if (action == GLFW_PRESS)
        {
            cam->dragging = true;
            cam->dragButton = button;
            cam->dragMods = mods;
            double x, y;
            glfwGetCursorPos(w, &x, &y);
            cam->lastX = x;
            cam->lastY = y;
        }
        else if (action == GLFW_RELEASE)
        {
            cam->dragging = false;
        }
    }

    // 鼠标移动：只有在拖拽时生效
    static void CursorPosCallback(GLFWwindow *w, double xpos, double ypos)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam || !cam->dragging)
            return;

        double dx = xpos - cam->lastX;
        double dy = ypos - cam->lastY;
        cam->lastX = xpos;
        cam->lastY = ypos;

        if (cam->dragButton == GLFW_MOUSE_BUTTON_LEFT)
        {
            // 左键：同时控制偏航和俯仰
            cam->yaw += float(dx) * kRotateSpeed;
            cam->pitch += float(-dy) * kRotateSpeed;
            cam->pitch = glm::clamp(cam->pitch, -89.0f, 89.0f);
        }
        else if (cam->dragButton == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            // 计算 front/right/up 同之前
            glm::vec3 front;
            front.x = cos(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
            front.y = sin(glm::radians(cam->pitch));
            front.z = sin(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
            front = glm::normalize(front);

            glm::vec3 right = glm::normalize(glm::cross(front, {0, 1, 0}));
            glm::vec3 up = glm::normalize(glm::cross(right, front));
            float step = kPanSpeed * cam->radius;

            // —— 正确的“Grab”平移 ——
            cam->target += right * float(dx) * step; // 水平：鼠标右移→target向左移→场景右移
            cam->target += up * float(dy) * step;    // 垂直：鼠标下移→target向上移→场景下移
        }
        // （如需）else if (cam->dragButton == GLFW_MOUSE_BUTTON_RIGHT) { … }
    }
};

// 全局纹理缓存
std::vector<TextureInfo> g_loadedTexturesCache;

std::string g_droppedModelPath;
bool g_newModelPathAvailable = false;

// --- 拖放回调函数 ---
void drop_callback(GLFWwindow *window, int count, const char **paths)
{
    if (count > 0)
    {
        // 我们简单地处理第一个拖放的文件
        g_droppedModelPath = paths[0];
        g_newModelPathAvailable = true;
        std::cout << "File dropped: " << paths[0] << std::endl;
    }
}

// 替换你现有的 LoadTexture 函数
GLuint LoadTexture(
    const char *texturePathCStr,       // Assimp提供的路径 (可能是文件名或 "*index")
    const std::string &modelDirectory, // 模型文件所在的目录
    const aiScene *scene,              // Assimp场景指针 (用于访问嵌入纹理)
    const std::string &modelFilePath   // 完整模型文件路径 (用于为嵌入纹理生成唯一缓存键)
)
{
    std::string texturePathAssimp = std::string(texturePathCStr);
    std::string cacheKey;
    bool isEmbedded = (texturePathAssimp.rfind("*", 0) == 0);

    if (isEmbedded)
    {
        cacheKey = modelFilePath + texturePathAssimp;
    }
    else
    {
        cacheKey = texturePathAssimp;
        if (cacheKey.find(":/") == std::string::npos && cacheKey.find(":\\") == std::string::npos && cacheKey[0] != '/')
        {
            cacheKey = modelDirectory + '/' + cacheKey;
        }
    }

    for (const auto &texInfo : g_loadedTexturesCache)
    {
        if (texInfo.path == cacheKey)
        {
            return texInfo.id;
        }
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = nullptr;
    const aiTexture *currentEmbeddedTexture = nullptr; // <--- 在这里声明并初始化
    bool stbi_allocated_data = false;                  // <--- 用于跟踪内存是否由stb分配

    if (isEmbedded)
    {
        int textureIndex = std::stoi(texturePathAssimp.substr(1));
        if (scene && textureIndex >= 0 && static_cast<unsigned int>(textureIndex) < scene->mNumTextures)
        {
            currentEmbeddedTexture = scene->mTextures[textureIndex]; // <--- 在这里赋值
            if (currentEmbeddedTexture->mHeight == 0)
            {
                data = stbi_load_from_memory(
                    reinterpret_cast<unsigned char *>(currentEmbeddedTexture->pcData),
                    currentEmbeddedTexture->mWidth,
                    &width, &height, &nrComponents, 0);
                if (data)
                    stbi_allocated_data = true; // 标记为stb分配
            }
            else
            {
                width = currentEmbeddedTexture->mWidth;
                height = currentEmbeddedTexture->mHeight;
                nrComponents = 4;
                data = reinterpret_cast<unsigned char *>(currentEmbeddedTexture->pcData);
                stbi_allocated_data = false; // 数据直接来自Assimp，不由stb分配
            }
            if (!data)
            {
                std::cerr << "Failed to process embedded texture: " << texturePathAssimp << " from " << modelFilePath << std::endl;
            }
        }
        else
        {
            std::cerr << "Invalid embedded texture index or scene pointer for: " << texturePathAssimp << std::endl;
        }
    }
    else
    {
        data = stbi_load(cacheKey.c_str(), &width, &height, &nrComponents, 0);
        if (data)
            stbi_allocated_data = true; // 标记为stb分配
        else
        {
            std::cerr << "Texture failed to load at path: " << cacheKey << " | Reason: " << stbi_failure_reason() << std::endl;
        }
    }

    if (data)
    {
        GLenum internalFormat = 0;
        GLenum dataFormat = 0;
        if (nrComponents == 1)
        {
            internalFormat = GL_RED;
            dataFormat = GL_RED;
        }
        else if (nrComponents == 3)
        {
            internalFormat = GL_RGB;
            dataFormat = GL_RGB;
        }
        else if (nrComponents == 4)
        {
            internalFormat = GL_RGBA;
            dataFormat = GL_RGBA;
        }
        else
        {
            std::cerr << "Texture " << cacheKey << " loaded with unsupported " << nrComponents << " components." << std::endl;
            if (stbi_allocated_data)
                stbi_image_free(data); // 只释放stb分配的内存
            glDeleteTextures(1, &textureID);
            return 0;
        }

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (stbi_allocated_data)
        { // 只释放stb分配的内存
            stbi_image_free(data);
        }

        TextureInfo newTexCacheEntry;
        newTexCacheEntry.id = textureID;
        newTexCacheEntry.path = cacheKey;
        g_loadedTexturesCache.push_back(newTexCacheEntry);
        std::cout << "Loaded texture: " << cacheKey << " (ID: " << textureID << ")" << std::endl;
    }
    else
    {
        glDeleteTextures(1, &textureID); // 如果data为null，意味着加载失败，删除已生成的纹理ID
        return 0;
    }
    return textureID;
}

// 替换你现有的 loadMaterialTextures 函数
std::vector<TextureInfo> loadMaterialTextures(
    aiMaterial *mat,
    const std::string &modelDirectory,
    const aiScene *scene,            // 新增
    const std::string &modelFilePath // 新增
)
{
    std::vector<TextureInfo> textures;

    // 优先尝试 PBR 基础颜色纹理 (glTF/GLB 常用)
    for (unsigned int i = 0; i < mat->GetTextureCount(aiTextureType_BASE_COLOR); i++)
    {
        aiString str;
        mat->GetTexture(aiTextureType_BASE_COLOR, i, &str);
        GLuint textureId = LoadTexture(str.C_Str(), modelDirectory, scene, modelFilePath);
        if (textureId != 0)
        {
            TextureInfo texture;
            texture.id = textureId;
            texture.type = "texture_diffuse"; // 我们在着色器中统一按 "texture_diffuse" 处理
            // 从缓存中获取规范化路径
            for (const auto &cachedTex : g_loadedTexturesCache)
            {
                if (cachedTex.id == textureId)
                {
                    texture.path = cachedTex.path;
                    break;
                }
            }
            textures.push_back(texture);
        }
    }

    // 如果没有找到 BASE_COLOR 纹理，再尝试传统的 DIFFUSE 纹理
    if (textures.empty())
    {
        for (unsigned int i = 0; i < mat->GetTextureCount(aiTextureType_DIFFUSE); i++)
        {
            aiString str;
            mat->GetTexture(aiTextureType_DIFFUSE, i, &str);
            GLuint textureId = LoadTexture(str.C_Str(), modelDirectory, scene, modelFilePath);
            if (textureId != 0)
            {
                TextureInfo texture;
                texture.id = textureId;
                texture.type = "texture_diffuse";
                for (const auto &cachedTex : g_loadedTexturesCache)
                {
                    if (cachedTex.id == textureId)
                    {
                        texture.path = cachedTex.path;
                        break;
                    }
                }
                textures.push_back(texture);
            }
        }
    }
    return textures;
}

// 修改 loadModel 函数
std::vector<Mesh> loadModel(const std::string &path, const std::string &directory, const glm::vec3 &defaultColor = glm::vec3(0.8f, 0.8f, 0.8f))
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path, // path 是完整模型路径
                                             aiProcess_Triangulate |
                                                 aiProcess_GenSmoothNormals |
                                                 aiProcess_FlipUVs |
                                                 aiProcess_JoinIdenticalVertices |
                                                 aiProcess_ValidateDataStructure);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cerr << "Failed to load model: " << importer.GetErrorString() << "\n";
        return {};
    }

    std::vector<Mesh> meshes;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh *mesh = scene->mMeshes[i];
        std::vector<float> vertexData; /* ... (与之前相同，确保为11个float预留空间) ... */
        vertexData.reserve(mesh->mNumVertices * 11);
        std::vector<unsigned int> indices; /* ... (与之前相同) ... */
        std::vector<TextureInfo> meshTextures;

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            // 位置, 法线, 颜色, 纹理坐标的填充逻辑与你之前代码相同
            vertexData.push_back(mesh->mVertices[v].x); /* ... x,y,z ... */
            vertexData.push_back(mesh->mVertices[v].y);
            vertexData.push_back(mesh->mVertices[v].z);
            if (mesh->HasNormals())
            { /* ... normals ... */
                vertexData.push_back(mesh->mNormals[v].x);
                vertexData.push_back(mesh->mNormals[v].y);
                vertexData.push_back(mesh->mNormals[v].z);
            }
            else
            {
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
            }
            if (mesh->HasVertexColors(0))
            { /* ... colors ... */
                vertexData.push_back(mesh->mColors[0][v].r);
                vertexData.push_back(mesh->mColors[0][v].g);
                vertexData.push_back(mesh->mColors[0][v].b);
            }
            else
            {
                vertexData.push_back(defaultColor.r);
                vertexData.push_back(defaultColor.g);
                vertexData.push_back(defaultColor.b);
            }
            if (mesh->HasTextureCoords(0))
            { /* ... tex coords ... */
                vertexData.push_back(mesh->mTextureCoords[0][v].x);
                vertexData.push_back(mesh->mTextureCoords[0][v].y);
            }
            else
            {
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
            }
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; f++)
        { /* ... (索引填充与之前相同) ... */
            aiFace face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
            // 注意这里传递 scene 指针和原始模型路径 path
            meshTextures = loadMaterialTextures(material, directory, scene, path);
        }

        meshes.emplace_back(vertexData, indices, meshTextures);
    }
    return meshes;
}

int main(int argc, char **argv)
{

    // — GLFW & GLAD 初始化 —
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Model Viewer", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGL();
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback);

    std::vector<Mesh> meshes;
    std::string statusMessage; // 用于在窗口标题显示状态信息

    // --- 可选: 从命令行加载初始模型 ---
    if (argc > 1)
    {
        std::string fullPath = argv[1];
        std::string filename = std::filesystem::path(fullPath).filename().string();
        std::string directory = std::filesystem::path(fullPath).parent_path().string(); // 获取模型目录
        std::cout << "Attempting to load model from command line: " << fullPath << std::endl;

        meshes = loadModel(fullPath, directory); // 传递目录
        if (meshes.empty())
        { /* ... (statusMessage 设置) ... */
            statusMessage = "Error loading initial: " + filename + ". Drag & drop.";
            std::cerr << statusMessage << std::endl;
        }
        else
        { /* ... (statusMessage 设置) ... */
            statusMessage = "Loaded: " + filename;
            std::cout << "Successfully loaded initial model: " << fullPath << std::endl;
        }
    }
    else
    { /* ... (statusMessage 设置) ... */
        statusMessage = "Drag & drop a model file to load.";
        std::cout << statusMessage << std::endl;
    }

    Shader shader("shaders/vs.glsl", "shaders/fs.glsl");

    // --- 初始化光照配置 ---
    LightConfig pointLight;
    pointLight.position = glm::vec3(3.0f, 3.0f, 3.0f); // 调整光源位置
    pointLight.color = glm::vec3(1.0f, 1.0f, 1.0f);    // 白色光源
    pointLight.ambientStrength = 0.15f;                // 环境光稍弱一些
    pointLight.specularStrength = 0.6f;                // 镜面反射强度
    pointLight.shininess = 64.0f;                      // 更集中的高光

    CameraController camera(window);
    glEnable(GL_DEPTH_TEST);

    // — 渲染循环 —
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (g_newModelPathAvailable)
        {
            std::string currentDroppedFullPath = g_droppedModelPath;
            std::string currentDroppedFilename = std::filesystem::path(currentDroppedFullPath).filename().string();
            std::string currentDroppedDirectory = std::filesystem::path(currentDroppedFullPath).parent_path().string();

            g_droppedModelPath.clear();
            g_newModelPathAvailable = false;

            std::cout << "Processing dropped file: " << currentDroppedFullPath << std::endl;
            std::vector<Mesh> newMeshes = loadModel(currentDroppedFullPath, currentDroppedDirectory); // 传递目录
            if (!newMeshes.empty())
            { /* ... (meshes 和 statusMessage 更新) ... */
                meshes = std::move(newMeshes);
                statusMessage = "Loaded: " + currentDroppedFilename;
                std::cout << "Successfully loaded model from: " << currentDroppedFullPath << std::endl;
            }
            else
            { /* ... (meshes 和 statusMessage 更新) ... */
                meshes.clear();
                statusMessage = "Error loading: " + currentDroppedFilename + ". Drag & drop.";
                std::cerr << "Failed to load model from dropped file: " << currentDroppedFullPath << std::endl;
            }
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (meshes.empty())
        { /* ... (窗口标题设置) ... */
            glfwSetWindowTitle(window, ("Model Viewer - " + statusMessage).c_str());
        }
        else
        {
            /* ... (窗口标题设置) ... */
            if (!statusMessage.empty() && statusMessage.rfind("Loaded: ", 0) == 0)
            {
                glfwSetWindowTitle(window, ("Model Viewer - " + statusMessage.substr(8)).c_str());
            }
            else
            {
                glfwSetWindowTitle(window, "Model Viewer");
            }

            glm::mat4 model_matrix = glm::rotate(/* ... */ glm::mat4(1.f), (float)glfwGetTime() * 0.5f, glm::vec3(0.f, 1.f, 0.f));
            glm::vec3 camPos;
            glm::mat4 view = camera.getViewMatrix(camPos);
            glm::mat4 proj = glm::perspective(glm::radians(45.f), (h == 0 ? 1.0f : w / (float)h), 0.1f, 100.f);

            shader.use();
            // 设置通用 uniforms
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uModel"), 1, GL_FALSE, glm::value_ptr(model_matrix));
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uView"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
            glUniform3fv(glGetUniformLocation(shader.id, "uViewPos"), 1, glm::value_ptr(camPos));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightPos"), 1, glm::value_ptr(pointLight.position));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightColor"), 1, glm::value_ptr(pointLight.color));
            glUniform1f(glGetUniformLocation(shader.id, "uAmbientStrength"), pointLight.ambientStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uSpecularStrength"), pointLight.specularStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uShininess"), pointLight.shininess);

            // 设置漫反射纹理采样器 uniform (uDiffuseSampler) 到纹理单元 0 (只需要设置一次，因为它不会变)
            glUniform1i(glGetUniformLocation(shader.id, "uDiffuseSampler"), 0);

            for (auto &mesh_obj : meshes)
            {
                mesh_obj.draw(shader); // 将 shader 传递给 draw 函数
            }
        }

        glfwSwapBuffers(window);
    }

    meshes.clear(); // 清理 Mesh 对象的 GL 资源 (VAO/VBO/EBO)

    // --- 清理加载的纹理 ---
    for (const auto &texInfo : g_loadedTexturesCache)
    {
        if (texInfo.id != 0)
        {
            glDeleteTextures(1, &texInfo.id);
        }
    }
    g_loadedTexturesCache.clear();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

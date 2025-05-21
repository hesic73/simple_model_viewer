#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

struct Mesh
{
    GLuint VAO = 0, VBO = 0, EBO = 0; // 初始化为0，表示无效/未分配
    GLsizei indexCount = 0;

    Mesh() = default; // 允许默认构造，用于std::vector的某些操作，但实际使用应通过带参构造

    Mesh(const std::vector<float> &vertexData,
         const std::vector<unsigned int> &indices)
    {
        indexCount = (GLsizei)indices.size();
        if (indexCount == 0 || vertexData.empty())
            return; // 不创建空网格的GL资源

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

        GLsizei stride = 9 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

        glBindVertexArray(0);
    }

    // 析构函数：释放OpenGL资源
    ~Mesh()
    {
        // 确保OpenGL上下文仍然有效，并且这些句柄是有效的
        if (EBO != 0)
            glDeleteBuffers(1, &EBO);
        if (VBO != 0)
            glDeleteBuffers(1, &VBO);
        if (VAO != 0)
            glDeleteVertexArrays(1, &VAO);
    }

    // 移动构造函数
    Mesh(Mesh &&other) noexcept
        : VAO(other.VAO), VBO(other.VBO), EBO(other.EBO), indexCount(other.indexCount)
    {
        // 将 other 置为一个有效的空状态，防止其析构函数释放资源
        other.VAO = 0;
        other.VBO = 0;
        other.EBO = 0;
        other.indexCount = 0;
    }

    // 移动赋值运算符
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other)
        {
            // 释放当前对象的资源
            if (EBO != 0)
                glDeleteBuffers(1, &EBO);
            if (VBO != 0)
                glDeleteBuffers(1, &VBO);
            if (VAO != 0)
                glDeleteVertexArrays(1, &VAO);

            // 转移 other 的资源所有权
            VAO = other.VAO;
            VBO = other.VBO;
            EBO = other.EBO;
            indexCount = other.indexCount;

            // 将 other 置为一个有效的空状态
            other.VAO = 0;
            other.VBO = 0;
            other.EBO = 0;
            other.indexCount = 0;
        }
        return *this;
    }

    // 删除拷贝构造函数和拷贝赋值运算符，因为 Mesh 管理独占资源
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void draw() const
    {
        if (VAO == 0 || indexCount == 0)
            return; // 如果VAO无效或没有索引，则不绘制
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        // glBindVertexArray(0); // 通常在绘制多个物体时，不需要每次都解绑
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

std::vector<Mesh> loadModel(const std::string &path, const glm::vec3 &defaultColor = glm::vec3(0.8f, 0.8f, 0.8f))
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path,
                                             aiProcess_Triangulate |
                                                 aiProcess_GenSmoothNormals |
                                                 aiProcess_JoinIdenticalVertices |
                                                 aiProcess_ValidateDataStructure);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cerr << "Failed to load model: " << importer.GetErrorString() << "\n";
        return {};
    }
    std::vector<Mesh> meshes;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh *m = scene->mMeshes[i];
        std::vector<float> vertexData;
        std::vector<unsigned int> indices;
        // 每个顶点现在包含：位置 (3) + 法线 (3) + 颜色 (3) = 9 floats
        vertexData.reserve(m->mNumVertices * 9);
        indices.reserve(m->mNumFaces * 3);

        // 顶点、法线和颜色
        for (unsigned v = 0; v < m->mNumVertices; ++v)
        {
            // 位置
            vertexData.push_back(m->mVertices[v].x);
            vertexData.push_back(m->mVertices[v].y);
            vertexData.push_back(m->mVertices[v].z);

            // 法线 (Assimp应该已经生成了)
            if (m->HasNormals())
            {
                vertexData.push_back(m->mNormals[v].x);
                vertexData.push_back(m->mNormals[v].y);
                vertexData.push_back(m->mNormals[v].z);
            }
            else // Fallback, though aiProcess_GenSmoothNormals should prevent this
            {
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
            }

            // 颜色 (检查第一组顶点颜色 mColors[0])
            if (m->HasVertexColors(0))
            {
                vertexData.push_back(m->mColors[0][v].r);
                vertexData.push_back(m->mColors[0][v].g);
                vertexData.push_back(m->mColors[0][v].b);
            }
            else
            {
                // 如果模型没有顶点颜色，使用传入的默认颜色
                vertexData.push_back(defaultColor.r);
                vertexData.push_back(defaultColor.g);
                vertexData.push_back(defaultColor.b);
            }
        }
        // 索引
        for (unsigned f = 0; f < m->mNumFaces; ++f)
        {
            const aiFace &face = m->mFaces[f];
            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }
        meshes.emplace_back(vertexData, indices);
    }
    return meshes;
}
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
        std::string filename = std::filesystem::path(fullPath).filename().string(); // 提取文件名
        std::cout << "Attempting to load model from command line: " << fullPath << std::endl;

        meshes = loadModel(fullPath);
        if (meshes.empty())
        {
            statusMessage = "Error loading initial: " + filename + ". Drag & drop."; // 使用文件名
            std::cerr << statusMessage << std::endl;
        }
        else
        {
            statusMessage = "Loaded: " + filename; // 使用文件名
            std::cout << "Successfully loaded initial model: " << fullPath << std::endl;
        }
    }
    else
    {
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
            std::string currentDroppedFilename = std::filesystem::path(currentDroppedFullPath).filename().string(); // 提取文件名

            g_droppedModelPath.clear();
            g_newModelPathAvailable = false;

            std::cout << "Processing dropped file: " << currentDroppedFullPath << std::endl;

            std::vector<Mesh> newMeshes = loadModel(currentDroppedFullPath);

            if (!newMeshes.empty())
            {
                meshes = std::move(newMeshes);
                statusMessage = "Loaded: " + currentDroppedFilename; // 使用文件名
                std::cout << "Successfully loaded model from: " << currentDroppedFullPath << std::endl;
            }
            else
            {
                meshes.clear();
                statusMessage = "Error loading: " + currentDroppedFilename + ". Drag & drop."; // 使用文件名
                std::cerr << "Failed to load model from dropped file: " << currentDroppedFullPath << std::endl;
            }
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (meshes.empty())
        {
            // 没有模型时，更新窗口标题显示状态/提示
            glfwSetWindowTitle(window, ("Model Viewer - " + statusMessage).c_str());
            // 背景已经被glClear清空，这里可以未来扩展用于绘制屏幕文本
        }
        else
        {
            // 有模型时，可以显示模型名称或通用标题
            if (!statusMessage.empty() && statusMessage.rfind("Loaded: ", 0) == 0)
            {
                glfwSetWindowTitle(window, ("Model Viewer - " + statusMessage.substr(8)).c_str()); // 显示模型名
            }
            else
            {
                glfwSetWindowTitle(window, "Model Viewer");
            }

            glm::mat4 model_matrix = glm::rotate( // Renamed to avoid conflict if 'model' is a common var name
                glm::mat4(1.f), (float)glfwGetTime() * 0.5f,
                glm::vec3(0.f, 1.f, 0.f));

            glm::vec3 camPos;
            glm::mat4 view = camera.getViewMatrix(camPos);
            glm::mat4 proj = glm::perspective(glm::radians(45.f), (h == 0 ? 1.0f : w / (float)h), 0.1f, 100.f);

            shader.use();
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uModel"), 1, GL_FALSE, glm::value_ptr(model_matrix));
            // ... (其他 uniforms 设置) ...
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uView"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
            glUniform3fv(glGetUniformLocation(shader.id, "uViewPos"), 1, glm::value_ptr(camPos));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightPos"), 1, glm::value_ptr(pointLight.position));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightColor"), 1, glm::value_ptr(pointLight.color));
            glUniform1f(glGetUniformLocation(shader.id, "uAmbientStrength"), pointLight.ambientStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uSpecularStrength"), pointLight.specularStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uShininess"), pointLight.shininess);

            for (auto &mesh_obj : meshes)
                mesh_obj.draw();
        }

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

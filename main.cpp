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
    GLuint VAO, VBO, EBO;
    GLsizei indexCount;

    Mesh(const std::vector<float> &vertexData,
         const std::vector<unsigned int> &indices)
    {
        indexCount = (GLsizei)indices.size();

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        // vertex data
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     vertexData.size() * sizeof(float),
                     vertexData.data(), GL_STATIC_DRAW);

        // index data
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(unsigned int),
                     indices.data(), GL_STATIC_DRAW);

        // 顶点属性的总步长 (位置3 + 法线3 + 颜色3 = 9 floats)
        GLsizei stride = 9 * sizeof(float);

        // aPos (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              stride, (void *)0);
        // aNormal (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              stride,
                              (void *)(3 * sizeof(float))); // 偏移3个float

        // aColor (location = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                              stride,
                              (void *)(6 * sizeof(float))); // 偏移6个float (3 for pos + 3 for normal)

        glBindVertexArray(0);
    }

    void draw() const
    {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    }
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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: model_viewer <model_path>\n";
        return -1;
    }

    // — GLFW & GLAD 初始化 —
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Model Viewer", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGL();
    glfwSwapInterval(1);

    auto meshes = loadModel(argv[1]);
    if (meshes.empty())
        return -1;

    Shader shader("shaders/vs.glsl", "shaders/fs.glsl");

    CameraController camera(window);
    glEnable(GL_DEPTH_TEST);

    // — 渲染循环 —
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 旋转模型演示
        glm::mat4 model = glm::rotate(
            glm::mat4(1.f), (float)glfwGetTime(),
            glm::vec3(0.f, 1.f, 0.f));
        // 视图矩阵
        glm::vec3 camPos;
        glm::mat4 view = camera.getViewMatrix(camPos);
        glm::mat4 proj = glm::perspective(glm::radians(45.f), w / (float)h, 0.1f, 100.f);

        shader.use();
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(
            glGetUniformLocation(shader.id, "uViewPos"), 1, glm::value_ptr(camPos));

        for (auto &mesh : meshes)
            mesh.draw();

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

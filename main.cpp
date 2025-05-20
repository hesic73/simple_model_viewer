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

        // aPos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float), (void *)0);
        // aNormal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float),
                              (void *)(3 * sizeof(float)));

        glBindVertexArray(0);
    }

    void draw() const
    {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    }
};

std::vector<Mesh> loadModel(const std::string &path)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path,
                                             aiProcess_Triangulate |
                                                 aiProcess_GenSmoothNormals |
                                                 aiProcess_JoinIdenticalVertices);
    if (!scene)
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
        vertexData.reserve(m->mNumVertices * 6);
        indices.reserve(m->mNumFaces * 3);

        // 顶点和法线
        for (unsigned v = 0; v < m->mNumVertices; ++v)
        {
            vertexData.push_back(m->mVertices[v].x);
            vertexData.push_back(m->mVertices[v].y);
            vertexData.push_back(m->mVertices[v].z);
            vertexData.push_back(m->mNormals[v].x);
            vertexData.push_back(m->mNormals[v].y);
            vertexData.push_back(m->mNormals[v].z);
        }
        // 索引
        for (unsigned f = 0; f < m->mNumFaces; ++f)
        {
            const aiFace &face = m->mFaces[f];
            // Assimp 已经确保每个 face 是三角形（因为用了 aiProcess_Triangulate）
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
    // 参数
    float radius = 1.0f;
    float yaw = -90.0f;
    float pitch = 0.0f;
    glm::vec3 target = glm::vec3(0.0f);

    // 敏感度常量
    static constexpr float kZoomSpeed = 0.25f; // 每个滚轮刻度缩放量
    static constexpr float kPanSpeed = 0.02f;  // 平移步长单位（乘以 radius）
    static constexpr float min_radius = 0.05f; // 最小半径
    static constexpr float max_radius = 20.0f; // 最大半径

    // 构造时直接绑定回调
    CameraController(GLFWwindow *window)
    {
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, ScrollCallback);
        glfwSetKeyCallback(window, KeyCallback);
    }

    // 计算 view 矩阵
    glm::mat4 getViewMatrix(glm::vec3 &outCamPos) const
    {
        outCamPos.x = target.x + radius * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        outCamPos.y = target.y + radius * sin(glm::radians(pitch));
        outCamPos.z = target.z + radius * sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::lookAt(outCamPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

private:
    // 滚轮缩放
    static void ScrollCallback(GLFWwindow *w, double /*xoff*/, double yoff)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam)
            return;
        cam->radius -= static_cast<float>(yoff) * kZoomSpeed;
        cam->radius = glm::clamp(cam->radius, CameraController::min_radius, CameraController::max_radius);
    }

    // WASD/QE 平移
    static void KeyCallback(GLFWwindow *w, int key, int /*sc*/, int action, int /*mods*/)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam || (action != GLFW_PRESS && action != GLFW_REPEAT))
            return;

        // 计算前/右/上 向量
        glm::vec3 front;
        front.x = cos(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
        front.y = sin(glm::radians(cam->pitch));
        front.z = sin(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
        front = glm::normalize(front);

        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::normalize(glm::cross(right, front));
        float step = kPanSpeed * cam->radius;

        if (key == GLFW_KEY_W)
            cam->target += front * step;
        if (key == GLFW_KEY_S)
            cam->target -= front * step;
        if (key == GLFW_KEY_A)
            cam->target -= right * step;
        if (key == GLFW_KEY_D)
            cam->target += right * step;
        if (key == GLFW_KEY_Q)
            cam->target += up * step;
        if (key == GLFW_KEY_E)
            cam->target -= up * step;
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(w, GLFW_TRUE);
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

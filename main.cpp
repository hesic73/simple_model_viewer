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

// 一个简单的 Shader 封装
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

// 存放一个 Mesh 的 VBO/VAO
struct Mesh
{
    GLuint VAO, VBO;
    size_t vertexCount;
    Mesh(const std::vector<float> &data)
    {
        vertexCount = data.size() / 6; // 每顶点 6 float (pos+normal)
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     data.size() * sizeof(float),
                     data.data(), GL_STATIC_DRAW);

        // aPos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float), (void *)0);
        // aNormal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float), (void *)(3 * sizeof(float)));
        glBindVertexArray(0);
    }
    void draw() const
    {
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
};

// 加载 Assimp 场景，提取所有 mesh
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
        std::vector<float> data;
        data.reserve(m->mNumVertices * 6);
        for (unsigned v = 0; v < m->mNumVertices; ++v)
        {
            // 位置
            data.push_back(m->mVertices[v].x);
            data.push_back(m->mVertices[v].y);
            data.push_back(m->mVertices[v].z);
            // 法线
            data.push_back(m->mNormals[v].x);
            data.push_back(m->mNormals[v].y);
            data.push_back(m->mNormals[v].z);
        }
        meshes.emplace_back(data);
    }
    return meshes;
}

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

    // — 加载模型 & GPU 上传 —
    auto meshes = loadModel(argv[1]);
    if (meshes.empty())
        return -1;

    // — 编译 shader —
    Shader shader("shaders/vs.glsl", "shaders/fs.glsl");

    // — 摄像机/投影 & 基本状态 —
    glm::vec3 cameraPos(0.f, 0.f, 1.f);
    glm::mat4 proj = glm::perspective(
        glm::radians(45.f), 800.f / 600.f, 0.1f, 100.f);
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
        glm::mat4 view = glm::lookAt(
            cameraPos,
            glm::vec3(0.f),
            glm::vec3(0.f, 1.f, 0.f));

        shader.use();
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(
            glGetUniformLocation(shader.id, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(
            glGetUniformLocation(shader.id, "uViewPos"), 1, glm::value_ptr(cameraPos));

        for (auto &mesh : meshes)
            mesh.draw();

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

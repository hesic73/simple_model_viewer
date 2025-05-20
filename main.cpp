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

        // 顶点数据
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     vertexData.size() * sizeof(float),
                     vertexData.data(), GL_STATIC_DRAW);

        // 索引数据
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

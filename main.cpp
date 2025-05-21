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

#include "spdlog/spdlog.h"

#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

struct Shader
{
    GLuint id = 0; // Initialize to 0
    Shader(const char *vsPath, const char *fsPath)
    {
        auto load = [&](const char *path)
        {
            std::ifstream f(path);
            if (!f.is_open())
            {
                spdlog::error("Failed to open shader file: {}", path);
                return std::string();
            }
            return std::string(
                std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
        };
        auto compile = [&](const std::string &src, GLenum type, const char *shaderPath)
        {
            if (src.empty())
                return (GLuint)0; // Don't try to compile empty source

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
                spdlog::error("Shader compile error in {} (from {}): {}",
                              (type == GL_VERTEX_SHADER ? "Vertex Shader" : "Fragment Shader"),
                              shaderPath, buf);
                glDeleteShader(s); // Clean up failed shader
                return (GLuint)0;
            }
            return s;
        };
        std::string vs_src = load(vsPath);
        std::string fs_src = load(fsPath);

        if (vs_src.empty() || fs_src.empty())
        {
            spdlog::error("Failed to load one or more shader sources. VS: '{}', FS: '{}'", vsPath, fsPath);
            id = 0; // Mark as invalid
            return;
        }

        GLuint vsID = compile(vs_src, GL_VERTEX_SHADER, vsPath);
        GLuint fsID = compile(fs_src, GL_FRAGMENT_SHADER, fsPath);

        if (vsID == 0 || fsID == 0)
        {
            if (vsID != 0)
                glDeleteShader(vsID); // Clean up if one succeeded but other failed
            if (fsID != 0)
                glDeleteShader(fsID);
            id = 0; // Mark as invalid
            return;
        }

        id = glCreateProgram();
        glAttachShader(id, vsID);
        glAttachShader(id, fsID);
        glLinkProgram(id);

        GLint link_ok;
        glGetProgramiv(id, GL_LINK_STATUS, &link_ok);
        if (!link_ok)
        {
            char buf[512];
            glGetProgramInfoLog(id, 512, nullptr, buf);
            spdlog::error("Shader program link error: {}", buf);
            glDeleteProgram(id); // Clean up failed program
            id = 0;              // Mark as invalid
        }
        // Shaders are linked, no longer needed
        glDeleteShader(vsID);
        glDeleteShader(fsID);
    }
    void use() const
    {
        if (id != 0)
            glUseProgram(id);
    }
};

struct TextureInfo
{
    GLuint id = 0;
    std::string type; // e.g., "texture_diffuse", "texture_specular"
    std::string path; // Full path used for loading/caching the texture
};

struct Mesh
{
    GLuint VAO = 0, VBO = 0, EBO = 0; // Initialized to 0, indicating invalid/unallocated
    GLsizei indexCount = 0;
    std::vector<TextureInfo> textures; // Each Mesh can have multiple textures

    Mesh() = default; // Allow default construction, e.g., for std::vector operations

    // Constructor now also receives texture information
    Mesh(const std::vector<float> &vertexData,
         const std::vector<unsigned int> &indices,
         std::vector<TextureInfo> meshTextures)
        : indexCount((GLsizei)indices.size()), textures(std::move(meshTextures)) // Store textures
    {
        if (indexCount == 0 || vertexData.empty())
            return; // Don't create GL resources for an empty mesh

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

        // Vertex layout: Position(3) + Normal(3) + Color(3) + TexCoords(2) = 11 floats
        GLsizei stride = 11 * sizeof(float);
        // Position attribute (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        // Normal attribute (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        // Color attribute (location = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
        // Texture coordinate attribute (location = 3) - New
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void *)(9 * sizeof(float)));

        glBindVertexArray(0);
    }

    // Destructor: Release OpenGL resources
    ~Mesh()
    {
        // Ensure OpenGL context is still valid and these handles are valid
        if (EBO != 0)
            glDeleteBuffers(1, &EBO);
        if (VBO != 0)
            glDeleteBuffers(1, &VBO);
        if (VAO != 0)
            glDeleteVertexArrays(1, &VAO);
    }

    // Move constructor
    Mesh(Mesh &&other) noexcept
        : VAO(other.VAO), VBO(other.VBO), EBO(other.EBO), indexCount(other.indexCount), textures(std::move(other.textures))
    {
        // Leave 'other' in a valid but empty state to prevent its destructor from releasing resources
        other.VAO = 0;
        other.VBO = 0;
        other.EBO = 0;
        other.indexCount = 0;
    }

    // Move assignment operator
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other)
        {
            // Release current object's resources
            if (EBO != 0)
                glDeleteBuffers(1, &EBO);
            if (VBO != 0)
                glDeleteBuffers(1, &VBO);
            if (VAO != 0)
                glDeleteVertexArrays(1, &VAO);

            // Transfer ownership from 'other'
            VAO = other.VAO;
            VBO = other.VBO;
            EBO = other.EBO;
            indexCount = other.indexCount;
            textures = std::move(other.textures);

            // Leave 'other' in a valid but empty state
            other.VAO = 0;
            other.VBO = 0;
            other.EBO = 0;
            other.indexCount = 0;
        }
        return *this;
    }

    // Delete copy constructor and copy assignment operator as Mesh manages exclusive resources
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    // draw function now requires the Shader object to set uniforms
    void draw(const Shader &shaderProgram) const
    {
        if (VAO == 0 || indexCount == 0)
            return; // Don't draw if VAO is invalid or there are no indices

        bool hasDiffuseTexture = false;
        unsigned int diffuseTextureUnit = 0; // We bind the diffuse texture to texture unit 0

        for (const auto &texInfo : textures)
        {
            if (texInfo.type == "texture_diffuse" && texInfo.id != 0)
            {
                glActiveTexture(GL_TEXTURE0 + diffuseTextureUnit); // Typically GL_TEXTURE0
                glBindTexture(GL_TEXTURE_2D, texInfo.id);
                // The diffuse sampler uniform in the shader (e.g., uDiffuseSampler) should be set to this texture unit (0 here)
                hasDiffuseTexture = true;
                break; // Simplified: use only the first diffuse texture found
            }
        }

        shaderProgram.use(); // Ensure shader is active
        glUniform1i(glGetUniformLocation(shaderProgram.id, "uHasDiffuseTexture"), hasDiffuseTexture ? 1 : 0);
        // The uDiffuseSampler uniform is set once in the main render loop if it's always texture unit 0

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
        // if (hasDiffuseTexture) {
        //     glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture (optional)
        // }
    }
};

struct LightConfig
{
    glm::vec3 position;     // Light source position
    glm::vec3 color;        // Light source color (affects ambient, diffuse, and specular)
    float ambientStrength;  // Ambient light intensity
    float specularStrength; // Specular light intensity
    float shininess;        // Shininess factor (affects specular highlight size)
};

struct CameraController
{
    // Sensitivity settings
    static constexpr float kZoomSpeed = 0.25f;
    static constexpr float kPanSpeed = 0.005f; // Smaller step for smoother panning
    static constexpr float kRotateSpeed = 0.1f;

    static constexpr float minRadius = 0.01f;
    static constexpr float maxRadius = 100.0f;

    CameraController(GLFWwindow *window)
        : radius(1.0f),
          yaw(-90.0f),
          pitch(0.0f),
          target(0.0f, 0.0f, 0.0f),
          // Capture initial state here
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
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetCursorPosCallback(window, CursorPosCallback);
    }

    // Get the view matrix externally
    glm::mat4 getViewMatrix(glm::vec3 &outCamPos) const
    {
        outCamPos.x = target.x + radius * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        outCamPos.y = target.y + radius * sin(glm::radians(pitch));
        outCamPos.z = target.z + radius * sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::lookAt(outCamPos, target, {0, 1, 0}); // Up vector is (0,1,0)
    }

    // Call to restore to initial state
    void reset()
    {
        radius = initRadius;
        yaw = initYaw;
        pitch = initPitch;
        target = initTarget;
    }

private:
    // --- Current mutable state ---
    float radius;
    float yaw;   // In degrees
    float pitch; // In degrees
    glm::vec3 target;

    // --- Initial state backed up at construction (private) ---
    float initRadius;
    float initYaw;
    float initPitch;
    glm::vec3 initTarget;

    // Drag state
    bool dragging;
    int dragButton;
    int dragMods;
    double lastX, lastY;

    // Scroll wheel zoom (fixed behavior)
    static void ScrollCallback(GLFWwindow *w, double /*xoffset*/, double yoffset)
    {
        auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(w));
        if (!cam)
            return;
        cam->radius = glm::clamp(cam->radius - static_cast<float>(yoffset) * kZoomSpeed, minRadius, maxRadius);
    }

    // Mouse button handling: Left-drag rotates, Middle-drag pans
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
            glfwGetCursorPos(w, &cam->lastX, &cam->lastY);
        }
        else if (action == GLFW_RELEASE)
        {
            cam->dragging = false;
        }
    }

    // Mouse movement: Only effective when dragging
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
            // Left button: controls yaw and pitch
            cam->yaw += static_cast<float>(dx) * kRotateSpeed;
            cam->pitch += static_cast<float>(-dy) * kRotateSpeed; // Negative dy for conventional pitch
            cam->pitch = glm::clamp(cam->pitch, -89.0f, 89.0f);   // Clamp pitch
        }
        else if (cam->dragButton == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            // Middle button: pans the target
            // Calculate front/right/up vectors as before
            glm::vec3 front;
            front.x = cos(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
            front.y = sin(glm::radians(cam->pitch));
            front.z = sin(glm::radians(cam->yaw)) * cos(glm::radians(cam->pitch));
            front = glm::normalize(front);

            glm::vec3 globalUp = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(front, globalUp));
            glm::vec3 up = glm::normalize(glm::cross(right, front));
            float step = kPanSpeed * cam->radius; // Panning speed scales with zoom

            // Correct "Grab" panning
            cam->target -= right * static_cast<float>(dx) * step; // Horizontal: Mouse right -> target moves left (scene moves right)
            cam->target += up * static_cast<float>(dy) * step;    // Vertical: Mouse down -> target moves up (scene moves down)
        }
        // else if (cam->dragButton == GLFW_MOUSE_BUTTON_RIGHT) { /* For future use, e.g., other interactions */ }
    }
};

// Global texture cache
std::vector<TextureInfo> g_loadedTexturesCache;

// Global variables for file drop
std::string g_droppedModelPath;
bool g_newModelPathAvailable = false;

// Global flag to control model auto-rotation
static bool g_autoRotateModel = true;

// File drop callback function
void drop_callback(GLFWwindow *window, int count, const char **paths)
{
    if (count > 0)
    {
        // We simply handle the first dropped file
        g_droppedModelPath = paths[0];
        g_newModelPathAvailable = true;
        spdlog::info("File dropped: {}", paths[0]);
    }
}

// Loads a texture from file or embedded data
GLuint LoadTexture(
    const char *texturePathCStr,       // Path provided by Assimp (filename or "*index")
    const std::string &modelDirectory, // Directory of the model file
    const aiScene *scene,              // Assimp scene pointer (to access embedded textures)
    const std::string &modelFilePath   // Full model file path (for unique cache key for embedded textures)
)
{
    std::string texturePathAssimp = std::string(texturePathCStr);
    std::string cacheKey; // Unique key for searching/storing in g_loadedTexturesCache
    bool isEmbedded = (texturePathAssimp.rfind("*", 0) == 0);

    if (isEmbedded)
    {
        cacheKey = modelFilePath + texturePathAssimp; // e.g., "path/to/model.glb*0"
    }
    else
    {
        // Construct full path for external files as cache key
        cacheKey = texturePathAssimp;
        // If path is relative, prepend model directory
        if (cacheKey.find(":/") == std::string::npos && cacheKey.find(":\\") == std::string::npos && cacheKey[0] != '/')
        {
            cacheKey = modelDirectory + '/' + cacheKey;
        }
    }

    // 1. Check global cache
    for (const auto &texInfo : g_loadedTexturesCache)
    {
        if (texInfo.path == cacheKey)
        {
            // spdlog::debug("Reusing cached texture: {}", cacheKey);
            return texInfo.id;
        }
    }

    // 2. If not in cache, load the texture
    GLuint textureID = 0;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = nullptr;
    const aiTexture *currentEmbeddedTexture = nullptr; // Pointer to the embedded texture from Assimp scene
    bool stbi_allocated_data = false;                  // Flag to track if memory was allocated by stb_image

    if (isEmbedded)
    {
        int textureIndex = std::stoi(texturePathAssimp.substr(1)); // Get index from "*index" string
        if (scene && textureIndex >= 0 && static_cast<unsigned int>(textureIndex) < scene->mNumTextures)
        {
            currentEmbeddedTexture = scene->mTextures[textureIndex]; // Get embedded texture data
            if (currentEmbeddedTexture->mHeight == 0)
            { // Compressed format (e.g., PNG, JPG)
                data = stbi_load_from_memory(
                    reinterpret_cast<unsigned char *>(currentEmbeddedTexture->pcData),
                    currentEmbeddedTexture->mWidth, // This is the size of the compressed data
                    &width, &height, &nrComponents, 0);
                if (data)
                    stbi_allocated_data = true; // Mark as stb allocated
            }
            else
            { // Uncompressed format (typically ARGB8888)
                width = currentEmbeddedTexture->mWidth;
                height = currentEmbeddedTexture->mHeight;
                nrComponents = 4; // Assume RGBA for raw aiTexel data
                data = reinterpret_cast<unsigned char *>(currentEmbeddedTexture->pcData);
                stbi_allocated_data = false; // Data points directly to Assimp's memory, not stb allocated
            }
            if (!data)
            {
                spdlog::error("Failed to process embedded texture: {} from {}", texturePathAssimp, modelFilePath);
            }
        }
        else
        {
            spdlog::error("Invalid embedded texture index or scene pointer for: {}", texturePathAssimp);
        }
    }
    else
    { // External file
        data = stbi_load(cacheKey.c_str(), &width, &height, &nrComponents, 0);
        if (data)
            stbi_allocated_data = true; // Mark as stb allocated
        else
        {
            spdlog::error("Texture failed to load at path: {} | Reason: {}", cacheKey, stbi_failure_reason());
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
            spdlog::error("Texture {} loaded with unsupported {} components.", cacheKey, nrComponents);
            if (stbi_allocated_data)
                stbi_image_free(data);       // Free stb allocated memory
            glDeleteTextures(1, &textureID); // Clean up generated texture ID
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
        { // Free stb allocated memory
            stbi_image_free(data);
        }

        TextureInfo newTexCacheEntry;
        newTexCacheEntry.id = textureID;
        newTexCacheEntry.path = cacheKey; // Use the unique cache key
        g_loadedTexturesCache.push_back(newTexCacheEntry);
        spdlog::info("Loaded texture: {} (ID: {})", cacheKey, textureID);
    }
    else
    {                                    // If data is null, loading failed
        glDeleteTextures(1, &textureID); // Clean up generated texture ID
        return 0;
    }
    return textureID;
}

// Helper function to load material textures from Assimp material
std::vector<TextureInfo> loadMaterialTextures(
    aiMaterial *mat,
    const std::string &modelDirectory,
    const aiScene *scene,            // Pass Assimp scene for embedded textures
    const std::string &modelFilePath // Pass model file path for unique embedded texture keys
)
{
    std::vector<TextureInfo> textures;

    // Try loading PBR base color textures first (common for glTF/GLB)
    for (unsigned int i = 0; i < mat->GetTextureCount(aiTextureType_BASE_COLOR); i++)
    {
        aiString str;
        mat->GetTexture(aiTextureType_BASE_COLOR, i, &str);
        GLuint textureId = LoadTexture(str.C_Str(), modelDirectory, scene, modelFilePath);
        if (textureId != 0)
        {
            TextureInfo texture;
            texture.id = textureId;
            texture.type = "texture_diffuse"; // Treat as diffuse for our simple shader
            // Get the canonical path from cache for consistency
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

    // If no base color textures found, try traditional diffuse textures
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

// Loads a model from file
std::vector<Mesh> loadModel(const std::string &path, const std::string &directory, const glm::vec3 &defaultColor = glm::vec3(0.8f, 0.8f, 0.8f))
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path, // 'path' is the full model path
                                             aiProcess_Triangulate |
                                                 aiProcess_GenSmoothNormals |
                                                 aiProcess_FlipUVs | // Often needed as OpenGL UVs origin (0,0) is bottom-left
                                                 aiProcess_JoinIdenticalVertices |
                                                 aiProcess_ValidateDataStructure);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        spdlog::error("Failed to load model '{}': {}", path, importer.GetErrorString());
        return {};
    }

    std::vector<Mesh> meshes_vec; // Local vector for meshes of this model
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh *mesh_ptr = scene->mMeshes[i]; // Current Assimp mesh
        std::vector<float> vertexData;
        // Vertex data: Position(3) + Normal(3) + Color(3) + UV(2) = 11 floats
        vertexData.reserve(mesh_ptr->mNumVertices * 11);
        std::vector<unsigned int> indices;
        std::vector<TextureInfo> meshTextures; // Textures for the current mesh

        for (unsigned int v = 0; v < mesh_ptr->mNumVertices; ++v)
        {
            // Position
            vertexData.push_back(mesh_ptr->mVertices[v].x);
            vertexData.push_back(mesh_ptr->mVertices[v].y);
            vertexData.push_back(mesh_ptr->mVertices[v].z);
            // Normals
            if (mesh_ptr->HasNormals())
            {
                vertexData.push_back(mesh_ptr->mNormals[v].x);
                vertexData.push_back(mesh_ptr->mNormals[v].y);
                vertexData.push_back(mesh_ptr->mNormals[v].z);
            }
            else
            {
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f); // Default normal
            }
            // Vertex Colors
            if (mesh_ptr->HasVertexColors(0))
            {
                vertexData.push_back(mesh_ptr->mColors[0][v].r);
                vertexData.push_back(mesh_ptr->mColors[0][v].g);
                vertexData.push_back(mesh_ptr->mColors[0][v].b);
            }
            else
            {
                vertexData.push_back(defaultColor.r); // Default color
                vertexData.push_back(defaultColor.g);
                vertexData.push_back(defaultColor.b);
            }
            // Texture Coordinates (using the first set, if available)
            if (mesh_ptr->HasTextureCoords(0))
            {
                vertexData.push_back(mesh_ptr->mTextureCoords[0][v].x);
                vertexData.push_back(mesh_ptr->mTextureCoords[0][v].y);
            }
            else
            {
                vertexData.push_back(0.0f); // Default UVs
                vertexData.push_back(0.0f);
            }
        }

        // Indices
        for (unsigned int f = 0; f < mesh_ptr->mNumFaces; f++)
        {
            aiFace face = mesh_ptr->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        // Process materials and textures (simplified: only loads diffuse textures)
        if (mesh_ptr->mMaterialIndex >= 0)
        {
            aiMaterial *material = scene->mMaterials[mesh_ptr->mMaterialIndex];
            // Pass the Assimp scene pointer and the original model path for embedded texture handling
            meshTextures = loadMaterialTextures(material, directory, scene, path);
        }

        meshes_vec.emplace_back(vertexData, indices, meshTextures); // Pass texture info to Mesh constructor
    }
    return meshes_vec;
}

// Global key callback function
void GlobalKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        // ESC to close window
        if (key == GLFW_KEY_ESCAPE)
        {
            spdlog::info("ESC key pressed. Closing window.");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Space to toggle model rotation (only on initial press)
        if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
        {
            g_autoRotateModel = !g_autoRotateModel;
            spdlog::info("Space key pressed. Model auto-rotation toggled to: {}", g_autoRotateModel ? "ON" : "OFF");
        }

        // 'R' to reset camera
        if (key == GLFW_KEY_R)
        {
            auto *cam = static_cast<CameraController *>(glfwGetWindowUserPointer(window));
            if (cam)
            {
                spdlog::info("'R' key pressed. Resetting camera.");
                cam->reset();
            }
        }
    }
}

int main(int argc, char **argv)
{

    // --- GLFW & GLAD Initialization ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Model Viewer", nullptr, nullptr);
    if (!window)
    {
        spdlog::critical("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        spdlog::critical("Failed to initialize GLAD");
        glfwTerminate();
        return -1;
    }
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback); // Set file drop callback

    std::vector<Mesh> meshes_main; // Holds the meshes of the currently loaded model
    std::string statusMessage;     // Used to display status information in the window title

    // --- Optional: Load initial model from command line ---
    if (argc > 1)
    {
        std::string fullPath = argv[1];
        std::string filename = std::filesystem::path(fullPath).filename().string();
        std::string directory = std::filesystem::path(fullPath).parent_path().string(); // Get model directory
        spdlog::info("Attempting to load model from command line: {}", fullPath);

        meshes_main = loadModel(fullPath, directory); // Pass directory
        if (meshes_main.empty())
        {
            statusMessage = "Error loading initial: " + filename + ". Drag & drop."; // Use filename
            spdlog::error("{}", statusMessage);
        }
        else
        {
            statusMessage = "Loaded: " + filename; // Use filename
            spdlog::info("Successfully loaded initial model: {}", fullPath);
        }
    }
    else
    {
        statusMessage = "Drag & drop a model file to load.";
        spdlog::info("{}", statusMessage);
    }

    Shader shader("shaders/vs.glsl", "shaders/fs.glsl"); // Model shader
    if (shader.id == 0)
    { // Check if shader compilation/linking failed
        spdlog::critical("Failed to initialize shaders. Exiting.");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // --- Initialize light configuration ---
    LightConfig pointLight;
    pointLight.position = glm::vec3(3.0f, 3.0f, 3.0f); // Light position
    pointLight.color = glm::vec3(1.0f, 1.0f, 1.0f);    // White light
    pointLight.ambientStrength = 0.15f;                // Weaker ambient light
    pointLight.specularStrength = 0.6f;                // Specular reflection intensity
    pointLight.shininess = 64.0f;                      // More focused specular highlight

    CameraController camera(window);
    glfwSetKeyCallback(window, GlobalKeyCallback);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.25f, 0.3f, 1.0f); // Set background color

    float totalRotationAngle = 0.0f; // Accumulates the rotation angle
    float lastFrameTime = 0.0f;      // Time of the last frame
    constexpr float ROTATION_SPEED = 0.5f;

    lastFrameTime = (float)glfwGetTime(); // Initialize lastFrameTime before the loop starts

    // --- Render Loop ---
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents(); // Process events

        // --- Check if a new model needs to be loaded via drag-and-drop ---
        if (g_newModelPathAvailable)
        {
            std::string currentDroppedFullPath = g_droppedModelPath;
            std::string currentDroppedFilename = std::filesystem::path(currentDroppedFullPath).filename().string();
            std::string currentDroppedDirectory = std::filesystem::path(currentDroppedFullPath).parent_path().string();

            g_droppedModelPath.clear();      // Clear global path string
            g_newModelPathAvailable = false; // Reset flag

            spdlog::info("Processing dropped file: {}", currentDroppedFullPath);
            std::vector<Mesh> newMeshes = loadModel(currentDroppedFullPath, currentDroppedDirectory); // Pass directory
            if (!newMeshes.empty())
            {
                meshes_main = std::move(newMeshes);                  // RAII: Old Mesh objects in meshes_main are destructed
                statusMessage = "Loaded: " + currentDroppedFilename; // Use filename
                spdlog::info("Successfully loaded model from: {}", currentDroppedFullPath);
            }
            else
            {
                meshes_main.clear();                                                           // RAII: Clear potentially existing old model to show error/prompt status
                statusMessage = "Error loading: " + currentDroppedFilename + ". Drag & drop."; // Use filename
                spdlog::error("Failed to load model from dropped file: {}", currentDroppedFullPath);
            }
        }

        int w, h; // Framebuffer width and height
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (meshes_main.empty())
        {
            // If no model is loaded, update window title with status/prompt
            glfwSetWindowTitle(window, ("Model Viewer - " + statusMessage).c_str());
            // Background is cleared by glClear; future text rendering could go here
        }
        else
        {
            std::string titleBase = "Model Viewer";
            if (!statusMessage.empty() && statusMessage.rfind("Loaded: ", 0) == 0)
            {
                titleBase += " - " + statusMessage.substr(8);
            }

            // Append rotation status to title
            if (g_autoRotateModel)
            {
                // titleBase += " (Rotating)";
            }
            else
            {
                titleBase += " (Paused)";
            }

            glfwSetWindowTitle(window, titleBase.c_str());

            float currentFrameTime = (float)glfwGetTime();
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            if (g_autoRotateModel)
            {
                totalRotationAngle += ROTATION_SPEED * deltaTime;
            }

            glm::mat4 model_matrix = glm::rotate(glm::mat4(1.f), totalRotationAngle, glm::vec3(0.f, 1.f, 0.f));

            glm::vec3 camPos;
            glm::mat4 view = camera.getViewMatrix(camPos);
            glm::mat4 proj = glm::perspective(glm::radians(45.f), (h == 0 ? 1.0f : w / (float)h), 0.1f, 100.f);

            shader.use();
            // Set common uniforms
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uModel"), 1, GL_FALSE, glm::value_ptr(model_matrix));
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uView"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
            glUniform3fv(glGetUniformLocation(shader.id, "uViewPos"), 1, glm::value_ptr(camPos));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightPos"), 1, glm::value_ptr(pointLight.position));
            glUniform3fv(glGetUniformLocation(shader.id, "uLightColor"), 1, glm::value_ptr(pointLight.color));
            glUniform1f(glGetUniformLocation(shader.id, "uAmbientStrength"), pointLight.ambientStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uSpecularStrength"), pointLight.specularStrength);
            glUniform1f(glGetUniformLocation(shader.id, "uShininess"), pointLight.shininess);

            // Set diffuse texture sampler uniform to texture unit 0 (needs to be set once as it doesn't change)
            glUniform1i(glGetUniformLocation(shader.id, "uDiffuseSampler"), 0);

            for (auto &mesh_obj : meshes_main)
            {
                mesh_obj.draw(shader); // Pass shader to draw function
            }
        }
        glfwSwapBuffers(window);
    }

    // Clean up Mesh objects' GL resources (VAO/VBO/EBO) before OpenGL context is destroyed
    // Mesh's RAII destructor is called when meshes_main.clear() or meshes_main goes out of scope.
    // Calling meshes_main.clear() here while context is valid is good practice.
    meshes_main.clear();

    // --- Clean up loaded textures ---
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
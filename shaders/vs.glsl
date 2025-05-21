#version 330 core
layout(location = 0) in vec3 aPos;        // Vertex position
layout(location = 1) in vec3 aNormal;     // Vertex normal
layout(location = 2) in vec3 aColor;      // Vertex color
layout(location = 3) in vec2 aTexCoords;  // Texture coordinates

uniform mat4 uModel; // Model matrix
uniform mat4 uView;  // View matrix
uniform mat4 uProj;  // Projection matrix

out vec3 FragPos;      // Fragment position in world space
out vec3 Normal;       // Normal in world space
out vec3 VertexColor;  // Vertex color to be passed to fragment shader
out vec2 vTexCoords;   // Texture coordinates to be passed to fragment shader

void main()
{
    FragPos = vec3(uModel * vec4(aPos, 1.0));
    Normal  = mat3(transpose(inverse(uModel))) * aNormal; // Calculate normal in world space
    VertexColor = aColor;       // Pass through vertex color
    vTexCoords = aTexCoords;    // Pass through texture coordinates
    gl_Position = uProj * uView * vec4(FragPos, 1.0);
}
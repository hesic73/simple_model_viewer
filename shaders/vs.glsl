#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aTexCoords; // 新增：纹理坐标

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 FragPos;
out vec3 Normal;
out vec3 VertexColor;
out vec2 vTexCoords; // 新增：传递给片段着色器的纹理坐标

void main()
{
    FragPos = vec3(uModel * vec4(aPos, 1.0));
    Normal  = mat3(transpose(inverse(uModel))) * aNormal;
    VertexColor = aColor;
    vTexCoords = aTexCoords; // 传递纹理坐标
    gl_Position = uProj * uView * vec4(FragPos, 1.0);
}
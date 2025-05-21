#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 VertexColor;
in vec2 vTexCoords; // 新增：从顶点着色器接收的纹理坐标

out vec4 FragColor;

// 光照参数
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uSpecularStrength;
uniform float uShininess;
uniform vec3 uViewPos;

// 纹理采样器
uniform sampler2D uDiffuseSampler;    // 漫反射纹理采样器
uniform bool uHasDiffuseTexture; // 是否有漫反射纹理

void main()
{
    vec3 materialBaseColor;
    if (uHasDiffuseTexture) {
        materialBaseColor = texture(uDiffuseSampler, vTexCoords).rgb;
        // 可选：与顶点颜色混合，例如 materialBaseColor *= VertexColor;
    } else {
        materialBaseColor = VertexColor; // 没有纹理则使用顶点颜色
    }

    // 光照计算 (与之前相同)
    vec3 ambient = uAmbientStrength * uLightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), uShininess);
    vec3 specular = uSpecularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * materialBaseColor;
    FragColor = vec4(result, 1.0);
}
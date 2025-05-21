#version 330 core
in vec3 FragPos;     // 片段在世界空间中的位置 (来自顶点着色器)
in vec3 Normal;      // 片段的法线 (来自顶点着色器)
in vec3 VertexColor; // 片段的顶点颜色 (来自顶点着色器)

out vec4 FragColor;

// --- 光照参数 (由 C++ 设置) ---
uniform vec3 uLightPos;         // 光源在世界空间中的位置
uniform vec3 uLightColor;       // 光源的颜色

uniform float uAmbientStrength;   // 环境光强度
uniform float uSpecularStrength;  // 镜面光强度
uniform float uShininess;         // 高光指数 (例如 32, 64, 128)

// --- 其他 Uniforms ---
uniform vec3 uViewPos;          // 观察者/摄像机在世界空间中的位置

void main()
{
    // 1. 环境光 (Ambient)
    // 环境光部分通常是光源颜色乘以一个较小的强度因子
    vec3 ambient = uAmbientStrength * uLightColor;

    // 2. 漫反射光 (Diffuse)
    vec3 norm = normalize(Normal); // 标准化法线向量
    vec3 lightDir = normalize(uLightPos - FragPos); // 计算从片段指向光源的向量
    float diff = max(dot(norm, lightDir), 0.0);     // 计算漫反射强度 (点积，不小于0)
    vec3 diffuse = diff * uLightColor;              // 漫反射光颜色

    // 3. 镜面光 (Specular)
    vec3 viewDir = normalize(uViewPos - FragPos);    // 计算从片段指向观察者的向量
    vec3 reflectDir = reflect(-lightDir, norm);      // 计算反射向量
                                                     // (注意 lightDir 是从片段到光源，所以用 -lightDir 作为入射光方向)
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), uShininess); // 计算镜面高光强度
    vec3 specular = uSpecularStrength * spec * uLightColor;          // 镜面光颜色

    // 最终颜色 = (环境光 + 漫反射光 + 镜面光) * 物体表面颜色 (来自顶点)
    vec3 result = (ambient + diffuse + specular) * VertexColor;
    FragColor = vec4(result, 1.0);
}
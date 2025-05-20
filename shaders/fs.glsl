#version 330 core
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 uLightPos = vec3(5.0, 5.0, 5.0);
uniform vec3 uViewPos;       // 摄像机位置
uniform vec3 uLightColor = vec3(1.0);
uniform vec3 uObjectColor = vec3(0.8, 0.8, 0.8);

void main()
{
    // 环境光
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * uLightColor;

    // 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    // 镜面反射
    float specularStrength = 0.5;
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * uLightColor;

    vec3 color = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(color, 1.0);
}

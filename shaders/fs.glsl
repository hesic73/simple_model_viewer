#version 330 core
in vec3 FragPos;      // Fragment position in world space (from vertex shader)
in vec3 Normal;       // Normal in world space (from vertex shader)
in vec3 VertexColor;  // Vertex color (from vertex shader)
in vec2 vTexCoords;   // Texture coordinates (from vertex shader)

out vec4 FragColor; // Output fragment color

// --- Lighting parameters (set from C++) ---
uniform vec3 uLightPos;         // Light position in world space
uniform vec3 uLightColor;       // Light color
uniform float uAmbientStrength;   // Ambient light intensity
uniform float uSpecularStrength;  // Specular light intensity
uniform float uShininess;         // Shininess factor for specular highlights

// --- Other Uniforms ---
uniform vec3 uViewPos;          // Observer/camera position in world space

// --- Texture samplers ---
uniform sampler2D uDiffuseSampler;    // Diffuse texture sampler
uniform bool uHasDiffuseTexture;   // Flag indicating if a diffuse texture is present

void main()
{
    vec3 materialBaseColor;
    if (uHasDiffuseTexture) {
        materialBaseColor = texture(uDiffuseSampler, vTexCoords).rgb;
        // Optional: Modulate with vertex color, e.g., materialBaseColor *= VertexColor;
    } else {
        materialBaseColor = VertexColor; // Use vertex color if no texture is present
    }

    // --- Lighting Calculation ---
    // 1. Ambient light
    vec3 ambient = uAmbientStrength * uLightColor;

    // 2. Diffuse light
    vec3 norm = normalize(Normal); // Normalize the normal vector
    vec3 lightDir = normalize(uLightPos - FragPos); // Direction from fragment to light source
    float diff = max(dot(norm, lightDir), 0.0);     // Diffuse intensity (dot product, clamped to >= 0)
    vec3 diffuse = diff * uLightColor;              // Diffuse light color

    // 3. Specular light
    vec3 viewDir = normalize(uViewPos - FragPos);    // Direction from fragment to viewer
    vec3 reflectDir = reflect(-lightDir, norm);      // Reflected light direction
                                                     // (-lightDir because reflect expects vector from light to surface)
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), uShininess); // Specular highlight intensity
    vec3 specular = uSpecularStrength * spec * uLightColor;          // Specular light color

    // Final color = (sum of light components) * material's base color
    vec3 result = (ambient + diffuse + specular) * materialBaseColor;
    FragColor = vec4(result, 1.0);
}
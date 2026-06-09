#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;
in vec4 FragPosSpotLightSpace;

uniform sampler2D shadowMap;
uniform sampler2D spotShadowMap;
uniform sampler2D texture_diffuse;
uniform sampler2D texture_emissive;
uniform sampler2D sceneDepth;
uniform bool useTexture;
uniform bool hasEmissive;
uniform bool isLit;
uniform vec3 cameraPos;

uniform mat4 projection;
uniform mat4 view;

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight dirLight;

#define MAX_POINT_LIGHTS 64

struct PointLight {
    vec3 position;
    vec3 color;
    float constant;
    float linear;
    float quadratic;
    float radius;
};

uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;
uniform float pointLightMaster;

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    float specStrength;
};
uniform SpotLight spotLight;
uniform int spotLightEnabled;

uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform vec3 objectColor;
uniform float matShininess;
uniform float matSpecStrength;
uniform float shadowBias;
uniform int pcfRadius;

float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(depthMap, 0);
    int count = 0;
    for (int x = -pcfRadius; x <= pcfRadius; x++) {
        for (int y = -pcfRadius; y <= pcfRadius; y++) {
            float pcfDepth = texture(depthMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - shadowBias > pcfDepth ? 1.0 : 0.0;
            count++;
        }
    }
    return shadow / float(count);
}

float ScreenSpaceOcclusion(vec3 fragWorld, vec3 lightWorld, mat4 proj, mat4 view, sampler2D depthTex, vec2 fragCoord) {
    vec3 toLight = lightWorld - fragWorld;
    float totalDist = length(toLight);
    if (totalDist < 0.01) return 1.0;
    vec3 dir = toLight / totalDist;

    const int STEPS = 8;
    float stepSize = totalDist / float(STEPS);

    float hash = fract(sin(dot(fragCoord, vec2(127.1, 311.7))) * 43758.5453);

    int occludedSteps = 0;
    float rayT = stepSize * 0.2 + hash * stepSize * 0.6;

    for (int i = 0; i < STEPS; i++) {
        vec3 rayPos = fragWorld + dir * rayT;
        rayT += stepSize;
        if (rayT > totalDist) break;

        vec4 clip = proj * view * vec4(rayPos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sampledDepth = texture(depthTex, uv).r;
        float rayDepth = ndc.z * 0.5 + 0.5;
        if (sampledDepth < rayDepth - 0.003) {
            occludedSteps++;
        }
    }
    if (occludedSteps == 0) return 1.0;
    if (occludedSteps >= STEPS) return 0.0;
    return 1.0 - float(occludedSteps) / float(STEPS);
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, float shadow, float shininess, float specStrength) {
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 diffuse = light.color * light.intensity * diff * (1.0 - shadow);
    vec3 specular = light.color * light.intensity * spec * specStrength * (1.0 - shadow);
    return diffuse + specular;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess, float specStrength) {
    vec3 lightDir = normalize(light.position - fragPos);
    float NdotL = dot(normal, lightDir);
    float diff = max(NdotL, 0.0);
    float backFace = step(0.0, NdotL);
    if (backFace < 0.5) return vec3(0.0);

    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float windowAtten = 1.0 - smoothstep(light.radius * 0.7, light.radius, dist);
    attenuation *= windowAtten;
    if (attenuation < 0.001) return vec3(0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);

    vec3 diffuse  = light.color * attenuation * diff;
    vec3 specular = light.color * attenuation * spec * specStrength;
    float ssOcclusion = ScreenSpaceOcclusion(fragPos, light.position, projection, view, sceneDepth, gl_FragCoord.xy);
    return (diffuse + specular) * ssOcclusion;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess, float spotShadow) {
    vec3 lightDir = normalize(light.position - fragPos);
    float NdotL = dot(normal, lightDir);
    float diff = max(NdotL, 0.0);
    float backFace = step(0.0, NdotL);
    if (backFace < 0.5) return vec3(0.0);

    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    attenuation *= intensity;
    if (attenuation < 0.001) return vec3(0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);

    vec3 diffuse  = light.color * attenuation * diff * (1.0 - spotShadow);
    vec3 specular = light.color * attenuation * spec * light.specStrength * (1.0 - spotShadow);
    return diffuse + specular;
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    float shadow = ShadowCalculation(FragPosLightSpace, shadowMap);
    float spotShadow = ShadowCalculation(FragPosSpotLightSpace, spotShadowMap);

    vec3 lighting = vec3(0.0);

    lighting += CalcDirLight(dirLight, norm, viewDir, shadow, matShininess, matSpecStrength);

    for (int i = 0; i < numPointLights; i++) {
        lighting += CalcPointLight(pointLights[i], norm, FragPos, viewDir, matShininess, matSpecStrength) * pointLightMaster;
    }

    lighting += CalcSpotLight(spotLight, norm, FragPos, viewDir, matShininess, spotShadow) * float(spotLightEnabled);

    vec4 texColor = useTexture ? texture(texture_diffuse, TexCoords) : vec4(1.0);
    vec3 surfaceColor = objectColor * texColor.rgb;

    vec3 result = ambientColor * ambientIntensity * surfaceColor;
    result += lighting * surfaceColor;

    if (hasEmissive && isLit) {
        vec3 emissiveColor = texture(texture_emissive, TexCoords).rgb;
        result += emissiveColor * 0.8;
    }

    float alpha = useTexture ? texColor.a : 1.0;
    if (alpha < 0.1) discard;
    FragColor = vec4(result, alpha);
}

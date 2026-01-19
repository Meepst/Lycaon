#pragma once

#include "common.h"

struct alignas(16) Vertex{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct alignas(16) Material{
    int albedoTexture;
    int normalTexture;
    int specularTexture;
    int emissiveTexture;
    glm::vec4 specularFactor;
    glm::vec4 diffuseFactor;
    glm::vec3 emissiveFactor;
};

bool parseScene(const char* path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<Material>& materials,
    std::vector<std::string>& texturePaths);

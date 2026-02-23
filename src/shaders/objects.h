struct Ray {
    vec3 origin;
    vec3 direction;
};

struct Vertex {
    vec3 position;
    vec3 normal;
    vec3 uv;
};

struct Triangle {
    uint v0, v1, v2;
    uint materialID;
};

struct PathSegment {
    Ray ray;
    vec3 throughput;
    int pixelID;
    int depth;
    bool alive;
};

struct Intersection {
    vec3 position;
    vec3 normal;
    vec2 uv;
    int materialID;
    float t;
    bool hit;
};

struct Material {
    int albedoTexture;
    int normalTexture;
    int specularTexture;
    int emissiveTexture;
    vec4 specularFactor;
    vec4 diffuseFactor;
    vec3 emissiveFactor;
};

struct Ray {
    vec3 origin;
    vec3 direction;
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
    vec3 albedo;
    vec3 emission;
    float roughness;
    float metallic;
};

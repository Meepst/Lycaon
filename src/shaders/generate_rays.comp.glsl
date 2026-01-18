#version 450
#extension GL_GOOGLE_include_directive: require

#include "objects.h"

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D outputImage;

layout(push_constant) uniform Config {
    mat4 invViewProjection;
    uvec2 resolution;
    uint frameCount;
    uint maxDepth;
} config;

layout(set = 0, binding = 1) buffer PathSegments {
    PathSegment segments[];
};

uint xxhash32(uint seed) {
    const uint PRIME32_2 = 0x85EBCA77U;
    const uint PRIME32_3 = 0xC2B2AE3DU;
    const uint PRIME32_4 = 0x27D4EB2FU;
    const uint PRIME32_5 = 0x165667B1U;

    uint h32 = seed + PRIME32_5;
    h32 = PRIME32_4 * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2 * (h32 ^ (h32 >> 15));
    h32 = PRIME32_3 * (h32 ^ (h32 >> 13));

    return h32 ^ (h32 >> 16);
}

float rand(uint seed) {
    return float(xxhash32(seed)) / 4294967296.0;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= config.resolution.x || pixel.y >= config.resolution.y) {
        return;
    }
    uint pixelID = pixel.y * config.resolution.x + pixel.x;
    uint seed = pixelID * config.frameCount + 1;

    vec2 jitter = vec2(rand(seed), rand(seed)) - 0.5;
    vec2 uv = (vec2(pixel) + 0.5 + jitter) / vec2(config.resolution);
    vec2 ndc = uv * 2.0 - 1.0;

    vec4 target = config.invViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(target.xyz / target.w);
    vec4 origin = config.invViewProjection * vec4(0.0, 0.0, 0.0, 1.0);
    origin.xyz /= origin.w;

    segments[pixelID].ray.direction = direction;
    segments[pixelID].ray.origin = origin.xyz;
    segments[pixelID].throughput = vec3(1.0);
    segments[pixelID].pixelID = int(pixelID);
    segments[pixelID].depth = 0;
    segments[pixelID].alive = true;
}

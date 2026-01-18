#version 460
#extension GL_EXT_buffer_reference : require

layout(local_size_x = 32) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D image;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer IndexBuffer {
    uint Indices[];
};

layout(push_constant) uniform constants {
    mat4 renderMatrix;
    VertexBuffer vertexBuffer;
    IndexBuffer indexBuffer;
    uint indexCount;
} PushConstants;

layout(set = 0, binding = 1) uniform sampler2D displayTexture;

ivec2 projectToPixel(vec3 pos, ivec2 imgSize) {
    vec4 projected = PushConstants.renderMatrix * vec4(pos, 1.0);

    // Perspective divide
    vec2 ndc = projected.xy / projected.w;

    // Convert NDC (-1 to 1) to Pixel (0 to Size)
    return ivec2((ndc * 0.5 + 0.5) * vec2(imgSize - 1));
}

void drawLine(ivec2 p0, ivec2 p1, vec4 color) {
    ivec2 delta = p1 - p0;
    int steps = max(abs(delta.x), abs(delta.y));

    for (int i = 0; i <= steps; i++) {
        float t = float(i) / float(steps);
        ivec2 pixel = ivec2(vec2(p0) + t * vec2(delta));
        imageStore(image, pixel, color);
    }
}

float signedTriArea(int ax, int ay, int bx, int by, int cx, int cy) {
    return 0.5 * ((by - ay) * (bx + ax) + (cy - by) * (cx + bx) + (ay - cy) * (ax + cx));
}

void triangle(int ax, int ay, int bx, int by, int cx, int cy) {
    int bbminx = min(min(ax, bx), cx);
    int bbminy = min(min(ay, by), cy);
    int bbmaxx = max(max(ax, bx), cx);
    int bbmaxy = max(max(ay, by), cy);

    float totalArea = signedTriArea(ax, ay, bx, by, cx, cy);

    for (int x = bbminx; x <= bbmaxx; x++) {
        for (int y = bbminy; y <= bbmaxy; y++) {
            float alpha = signedTriArea(x, y, bx, by, cx, cy) / totalArea;
            float beta = signedTriArea(x, y, cx, cy, ax, ay) / totalArea;
            float gamma = signedTriArea(x, y, ax, ay, bx, by) / totalArea;
            if (alpha < 0 || beta < 0 || gamma < 0) {
                continue;
            }
            imageStore(image, ivec2(x, y), texture(displayTexture, vec2(alpha, beta)));
        }
    }
}

void main() {
    uint triIndex = gl_GlobalInvocationID.x;
    if (triIndex * 3 + 2 >= PushConstants.indexCount) return;

    ivec2 imgSize = imageSize(image);

    // Fetch vertex indices
    uint i0 = PushConstants.indexBuffer.Indices[triIndex * 3 + 0];
    uint i1 = PushConstants.indexBuffer.Indices[triIndex * 3 + 1];
    uint i2 = PushConstants.indexBuffer.Indices[triIndex * 3 + 2];

    // Project vertices to pixel coordinates
    ivec2 p0 = projectToPixel(PushConstants.vertexBuffer.vertices[i0].position, imgSize);
    ivec2 p1 = projectToPixel(PushConstants.vertexBuffer.vertices[i1].position, imgSize);
    ivec2 p2 = projectToPixel(PushConstants.vertexBuffer.vertices[i2].position, imgSize);

    triangle(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
}

#version 460

layout(local_size_x = 16, local_size_y = 16) in;

// Standard bindings — mapped to heap offsets at pipeline creation
layout(set = 0, binding = 0) uniform sampler2D inputTex;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform PushData {
    float brightness;
};

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = imageSize(outputImage);

    if (pixelCoord.x >= imageSize.x || pixelCoord.y >= imageSize.y)
        return;

    vec2 uv = (vec2(pixelCoord) + 0.5) / vec2(imageSize);
    vec4 color = texture(inputTex, uv);

    color.rgb *= brightness;

    imageStore(outputImage, pixelCoord, color);
}

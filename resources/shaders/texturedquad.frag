#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

layout(std140, binding = 0) uniform Uniforms {
    float rowOffset;
    float padding1;
    float padding2;
    float padding3;
};

void main()
{
    // Apply ring buffer offset per-pixel (not per-vertex, to avoid fract() interpolation issue)
    vec2 uv = vec2(v_uv.x, fract(v_uv.y + rowOffset));
    fragColor = texture(tex, uv);
}

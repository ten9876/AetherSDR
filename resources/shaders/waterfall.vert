#version 440

layout(location = 0) in vec2 position;  // clip space (-1 to 1)
layout(location = 1) in vec2 texcoord;  // 0 to 1

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform Uniforms {
    float rowOffset;    // ring buffer offset (0.0 to 1.0)
    float padding1;
    float padding2;
    float padding3;
};

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    // Apply ring buffer offset to V coordinate
    v_uv = vec2(texcoord.x, fract(texcoord.y + rowOffset));
}

#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D overlayTex;

void main()
{
    fragColor = texture(overlayTex, v_uv);
}

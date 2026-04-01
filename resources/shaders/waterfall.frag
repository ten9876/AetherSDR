#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D waterfallTex;  // intensity data (R channel)
layout(binding = 2) uniform sampler2D colorLut;       // 256x1 color gradient

layout(std140, binding = 0) uniform Uniforms {
    float rowOffset;
    float blackLevel;   // intensity below this is black
    float colorRange;   // intensity range for color mapping
    float padding3;
};

void main()
{
    float intensity = texture(waterfallTex, v_uv).r;

    // Map intensity to 0-1 range using black level and color range
    float t = clamp((intensity - blackLevel) / colorRange, 0.0, 1.0);

    // Look up color from 256x1 gradient texture
    fragColor = texture(colorLut, vec2(t, 0.5));
}

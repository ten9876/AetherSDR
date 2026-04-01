#version 440

layout(location = 0) in vec2 position;  // x = normalized (0-1), y = dBm value
layout(location = 0) out float v_alpha;

layout(std140, binding = 0) uniform Uniforms {
    vec4 viewport;      // x, y, width, height in clip space
    float minDbm;       // bottom of scale
    float maxDbm;       // top of scale
    float fillAlpha;    // alpha for fill triangles (0 = line only)
    float isFill;       // 1.0 for fill pass, 0.0 for line pass
};

void main()
{
    // Map x from 0-1 to clip space using viewport
    float cx = viewport.x + position.x * viewport.z;

    // Map dBm to clip space Y (maxDbm at top, minDbm at bottom)
    float t = clamp((position.y - minDbm) / (maxDbm - minDbm), 0.0, 1.0);
    float cy = viewport.y + t * viewport.w;

    gl_Position = vec4(cx, cy, 0.0, 1.0);
    v_alpha = (isFill > 0.5) ? fillAlpha : 1.0;
}

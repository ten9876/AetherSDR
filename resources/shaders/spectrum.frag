#version 440

layout(location = 0) in float v_alpha;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Uniforms {
    vec4 viewport;
    float minDbm;
    float maxDbm;
    float fillAlpha;
    float isFill;
    vec4 lineColor;     // FFT trace color
    vec4 fillColor;     // FFT fill color
};

void main()
{
    if (isFill > 0.5)
        fragColor = vec4(fillColor.rgb, fillColor.a * v_alpha);
    else
        fragColor = lineColor;
}

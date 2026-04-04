#version 440

layout(location = 0) in vec2 inPos;    // (x_ndc, y_ndc)
layout(location = 1) in vec4 inColor;  // (r, g, b, a)

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0);
    v_color = inColor;
}

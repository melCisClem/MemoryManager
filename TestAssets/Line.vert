#version 450 core
layout (location = 0) in vec2 aPostion;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec3 vColor;

uniform mat3 uViewXform;

void main() 
{
    gl_Position = vec4(vec2(uViewXform * vec3(aPostion, 1.0)), 0.0, 1.0);
    vColor = aColor;
}
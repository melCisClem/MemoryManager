#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;

uniform mat3 uViewXform;

void main()
{
    vec3 position = vec3(aPosition.xy, 1.0);
    position = uViewXform * position;
    gl_Position = vec4(position.xy, aPosition.z, 1.0);
    vTexCoord = aTexCoord;
}
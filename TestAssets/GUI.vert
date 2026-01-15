#version 450 core
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aColor;
layout (location = 3) in float aElement;

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) out vec3 vColor;
layout (location = 2) flat out float vElement;

uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vElement = aElement;
}
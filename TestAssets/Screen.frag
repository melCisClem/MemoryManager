#version 450 core
out vec4 fFragColor;
in vec2 vTexCoord;
uniform sampler2D uTexture;
void main()
{
    fFragColor = texture(uTexture, vTexCoord);
}
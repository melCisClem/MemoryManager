#version 450 core
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vColor;
layout(location = 2) flat in float vElement; // 0 = quad, 1 = text
out vec4 fFragColor;

uniform sampler2D uTex2d;

// optional tuning uniform (0.5 is the midpoint of distance field)
uniform float uSDFMid = 0.5;

void main()
{
    if(vElement == 0.0)
    {
        vec4 texColor = texture(uTex2d, vTexCoord);
        vec3 outRGB = texColor.rgb * vColor;
        fFragColor = vec4(outRGB, texColor.a);
    }
    else if(vElement == 1.0)
    {
        float dist = texture(uTex2d, vTexCoord).r;

        // smoothing factor based on derivative of distance (adapts to scale)
        float smoothing = fwidth(dist);

        // if smoothing is extremely small, clamp to a minimum to avoid zero-width
        smoothing = max(smoothing, 0.01 * (1.0 / 256.0)); // tweak if needed

        float alpha = smoothstep(uSDFMid - smoothing, uSDFMid + smoothing, dist);

        fFragColor = vec4(vColor, alpha);
    }
    else
    {
        fFragColor = vec4(vColor, 1.0);
    }
}
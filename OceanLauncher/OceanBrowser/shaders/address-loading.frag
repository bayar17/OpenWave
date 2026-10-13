#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 barSize;
    float cornerRadius;
    float borderWidth;
    float phase;
    float pixelSoftness;
} ubuf;

void main()
{
    vec2 p = (qt_TexCoord0 - vec2(0.5)) * ubuf.barSize;
    float radius = min(ubuf.cornerRadius, min(ubuf.barSize.x, ubuf.barSize.y) * 0.5);
    vec2 q = abs(p) - (ubuf.barSize * 0.5 - vec2(radius));
    float distance = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    float aa = ubuf.pixelSoftness;
    float outer = 1.0 - smoothstep(-aa, 0.0, distance);
    float inner = smoothstep(-ubuf.borderWidth - aa * 0.5, -ubuf.borderWidth + aa * 0.5, distance);
    float alpha = outer * inner;
    if (alpha <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    float sector = fract(atan(p.y, p.x) / 6.28318530718 - ubuf.phase / 360.0) * 4.0;
    float blend = smoothstep(0.0, 1.0, fract(sector));
    vec3 cyan = vec3(0.0, 0.86, 0.91);
    vec3 blue = vec3(0.14, 0.43, 1.0);
    vec3 violet = vec3(0.53, 0.29, 1.0);
    vec3 magenta = vec3(0.92, 0.21, 0.80);
    vec3 color;
    if (sector < 1.0) color = mix(cyan, blue, blend);
    else if (sector < 2.0) color = mix(blue, violet, blend);
    else if (sector < 3.0) color = mix(violet, magenta, blend);
    else color = mix(magenta, cyan, blend);
    fragColor = vec4(color * alpha, alpha) * ubuf.qt_Opacity;
}

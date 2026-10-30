#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float yaw;
    float pitch;
    float roll;
    float fieldOfView;
    float aspect;
    int mode;
};

layout(binding = 1) uniform sampler2D source;

const float PI = 3.14159265358979323846;

vec3 rotateView(vec3 value, float viewPitch) {
    float cr = cos(roll);
    float sr = sin(roll);
    value = vec3(value.x * cr - value.y * sr,
                 value.x * sr + value.y * cr, value.z);
    float cp = cos(viewPitch);
    float sp = sin(viewPitch);
    value = vec3(value.x, value.y * cp + value.z * sp,
                 -value.y * sp + value.z * cp);
    float cy = cos(yaw);
    float sy = sin(yaw);
    return vec3(value.x * cy + value.z * sy, value.y,
                -value.x * sy + value.z * cy);
}

void main() {
    vec2 point = vec2(qt_TexCoord0.x * 2.0 - 1.0, 1.0 - qt_TexCoord0.y * 2.0);
    vec3 ray;
    if (mode == 0) {
        float tangent = tan(fieldOfView * 0.5);
        ray = normalize(vec3(point.x * aspect * tangent, point.y * tangent, 1.0));
    } else {
        float radius = length(point);
        if (radius > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        if (radius < 0.000001) {
            ray = vec3(0.0, 0.0, 1.0);
        } else {
            float theta = mode == 1
                ? radius * fieldOfView * 0.5
                : 2.0 * atan(radius * tan(fieldOfView * 0.25));
            ray = vec3(point / radius * sin(theta), cos(theta));
        }
    }
    float viewPitch = pitch - (mode == 3 ? PI * 0.5 : 0.0);
    vec3 direction = normalize(rotateView(ray, viewPitch));
    float longitude = atan(direction.x, direction.z);
    float latitude = asin(clamp(direction.y, -1.0, 1.0));
    vec2 uv = vec2(fract(longitude / (2.0 * PI) + 0.5),
                   clamp(0.5 - latitude / PI, 0.0, 1.0));
    fragColor = texture(source, uv) * qt_Opacity;
}

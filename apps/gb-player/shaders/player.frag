// GLSL body; the renderer prepends a desktop or ES version header.
//
// One pass from decoded planar YUV to the displayed view. For the MAX layout this ports the
// gb_media CPU reference: face-specific EAC lookup, wrapped side-face overlap blending, and the
// shared virtual-camera conventions. Keep both implementations in step.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uY0;
uniform sampler2D uU0;
uniform sampler2D uV0;
uniform sampler2D uY1;
uniform sampler2D uU1;
uniform sampler2D uV1;
uniform vec2 uSource;      // luma size of the first stream, pixels
uniform vec2 uSize;        // view size, logical pixels
uniform int uMode;         // see VideoView::Mode
uniform int uLayout;       // 0 flat, 1 MAX dual-stream EAC
uniform int uHasFrame;
uniform float uOverlap;
uniform float uYaw;
uniform float uPitch;
uniform float uRoll;
uniform float uFieldOfView;
uniform float uZoom;
uniform vec2 uPan;
uniform float uKr;
uniform float uKb;
uniform int uFullRange;

const float PI = 3.14159265358979323846;
const vec3 BACKGROUND = vec3(0.035, 0.05, 0.065);

const int FACE_LEFT = 0;
const int FACE_RIGHT = 1;
const int FACE_TOP = 2;
const int FACE_FRONT = 3;
const int FACE_BACK = 4;
const int FACE_DOWN = 5;

vec3 toRgb(float y, float u, float v) {
    float luma = uFullRange == 1 ? y : (y * 255.0 - 16.0) / 219.0;
    float cb = uFullRange == 1 ? (u * 255.0 - 128.0) / 255.0 : (u * 255.0 - 128.0) / 224.0;
    float cr = uFullRange == 1 ? (v * 255.0 - 128.0) / 255.0 : (v * 255.0 - 128.0) / 224.0;
    float r = luma + 2.0 * (1.0 - uKr) * cr;
    float b = luma + 2.0 * (1.0 - uKb) * cb;
    float g = (luma - uKr * r - uKb * b) / (1.0 - uKr - uKb);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

vec3 sampleStream(int stream, vec2 uv) {
    if (stream == 0)
        return toRgb(texture(uY0, uv).r, texture(uU0, uv).r, texture(uV0, uv).r);
    return toRgb(texture(uY1, uv).r, texture(uU1, uv).r, texture(uV1, uv).r);
}

// Pixel-centre coordinates, as used by the CPU reference sampler.
vec3 samplePixel(int stream, vec2 pixel) {
    return sampleStream(stream, (pixel + 0.5) / uSource);
}

float equalAngle(float value) {
    return atan(value) * 4.0 / PI;
}

// Cube frame: x right, y forward, z up.
vec3 packedFaceColor(vec3 direction) {
    vec3 a = abs(direction);
    int face;
    vec3 p;
    if (a.x >= a.y && a.x >= a.z) {
        p = direction / a.x;
        face = direction.x < 0.0 ? FACE_LEFT : FACE_RIGHT;
    } else if (a.y >= a.z) {
        p = direction / a.y;
        face = direction.y < 0.0 ? FACE_BACK : FACE_FRONT;
    } else {
        p = direction / a.z;
        face = direction.z < 0.0 ? FACE_DOWN : FACE_TOP;
    }
    vec2 uv;
    if (face == FACE_LEFT) uv = vec2(equalAngle(p.y) + 1.0, equalAngle(p.z) + 1.0) * 0.5;
    else if (face == FACE_RIGHT) uv = vec2(1.0 - equalAngle(p.y), equalAngle(p.z) + 1.0) * 0.5;
    else if (face == FACE_FRONT) uv = vec2(equalAngle(p.x) + 1.0, equalAngle(p.z) + 1.0) * 0.5;
    else if (face == FACE_BACK) uv = vec2(1.0 - equalAngle(p.x), equalAngle(p.z) + 1.0) * 0.5;
    else if (face == FACE_DOWN) uv = vec2(1.0 - equalAngle(p.x), 1.0 - equalAngle(p.y)) * 0.5;
    else uv = vec2(1.0 - equalAngle(p.x), equalAngle(p.y) + 1.0) * 0.5;
    if (face == FACE_DOWN || face == FACE_BACK || face == FACE_TOP) uv = vec2(uv.y, 1.0 - uv.x);
    uv = clamp(uv, 0.0, 0.99999994);

    float centerWidth = uSource.y;
    float sideWidth = floor((uSource.x - centerWidth) * 0.5);
    float blendWidth = floor(uOverlap * 0.5);
    float y = (1.0 - uv.y) * (uSource.y - 1.0);
    if (face == FACE_FRONT || face == FACE_BACK) {
        float x = sideWidth + uv.x * (centerWidth - 1.0);
        return samplePixel(face == FACE_FRONT ? 0 : 1, vec2(x, y));
    }
    int stream = (face == FACE_LEFT || face == FACE_RIGHT) ? 0 : 1;
    float offset = (face == FACE_RIGHT || face == FACE_TOP) ? sideWidth + centerWidth : 0.0;
    float ratio = blendWidth / sideWidth;
    float leftU = 2.0 * (0.5 - ratio) * uv.x;
    float rightU = 2.0 * (0.5 - ratio) * (uv.x - 0.5) + 0.5 + ratio;
    if (leftU <= 0.5 - 2.0 * ratio) return samplePixel(stream, vec2(offset + leftU * sideWidth, y));
    if (rightU >= 0.5 + 2.0 * ratio) return samplePixel(stream, vec2(offset + rightU * sideWidth, y));
    vec3 first = samplePixel(stream, vec2(offset + leftU * sideWidth, y));
    vec3 second = samplePixel(stream, vec2(offset + rightU * sideWidth, y));
    float alpha = clamp((leftU - 0.5 + 2.0 * ratio) / (2.0 * ratio), 0.0, 1.0);
    return mix(first, second, alpha);
}

vec3 rotateView(vec3 value, float pitch) {
    float cr = cos(uRoll);
    float sr = sin(uRoll);
    value = vec3(value.x * cr - value.y * sr, value.x * sr + value.y * cr, value.z);
    float cp = cos(pitch);
    float sp = sin(pitch);
    value = vec3(value.x, value.y * cp + value.z * sp, -value.y * sp + value.z * cp);
    float cy = cos(uYaw);
    float sy = sin(uYaw);
    return vec3(value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy);
}

// Aspect-preserving placement of a 2D image, with zoom and pan in image units.
vec2 fitted(float contentAspect) {
    float viewAspect = uSize.x / max(uSize.y, 1.0);
    vec2 scale = viewAspect > contentAspect ? vec2(viewAspect / contentAspect, 1.0)
                                            : vec2(1.0, contentAspect / viewAspect);
    return (vUv - 0.5) * scale / uZoom + uPan + 0.5;
}

bool outside(vec2 uv) {
    return uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0;
}

void main() {
    if (uHasFrame == 0) {
        fragColor = vec4(BACKGROUND, 1.0);
        return;
    }
    int mode = uLayout == 0 ? 7 : uMode;
    if (mode >= 5) {
        int stream = mode == 6 ? 1 : 0;
        vec2 uv = fitted(uSource.x / uSource.y);
        fragColor = vec4(outside(uv) ? BACKGROUND : sampleStream(stream, uv), 1.0);
        return;
    }
    if (mode == 4) {
        vec2 uv = fitted(2.0);
        if (outside(uv)) {
            fragColor = vec4(BACKGROUND, 1.0);
            return;
        }
        float longitude = uv.x * 2.0 * PI - PI;
        float latitude = 0.5 * PI - uv.y * PI;
        vec3 direction = vec3(cos(latitude) * sin(longitude), cos(latitude) * cos(longitude),
                              sin(latitude));
        fragColor = vec4(packedFaceColor(direction), 1.0);
        return;
    }
    vec2 point = vec2(vUv.x * 2.0 - 1.0, 1.0 - vUv.y * 2.0);
    vec3 ray;
    if (mode == 0) {
        float tangent = tan(uFieldOfView * 0.5);
        ray = normalize(vec3(point.x * (uSize.x / max(uSize.y, 1.0)) * tangent, point.y * tangent, 1.0));
    } else {
        float radius = length(point);
        if (radius > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        if (radius < 0.000001) {
            ray = vec3(0.0, 0.0, 1.0);
        } else {
            float theta = mode == 1 ? radius * uFieldOfView * 0.5
                                    : 2.0 * atan(radius * tan(uFieldOfView * 0.25));
            ray = vec3(point / radius * sin(theta), cos(theta));
        }
    }
    vec3 view = normalize(rotateView(ray, uPitch - (mode == 3 ? PI * 0.5 : 0.0)));
    // Camera frame (x right, y up, z forward) to cube frame (x right, y forward, z up).
    fragColor = vec4(packedFaceColor(vec3(view.x, view.z, view.y)), 1.0);
}

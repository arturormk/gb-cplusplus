// GLSL body; the renderer prepends a desktop or ES version header.
layout(location = 0) in vec2 aPosition;
out vec2 vUv;
uniform mat4 uMatrix;
uniform vec2 uSize;

void main() {
    vUv = aPosition;
    gl_Position = uMatrix * vec4(aPosition * uSize, 0.0, 1.0);
}

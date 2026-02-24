#version 330 core
out vec4 FragColor;

void main() {
    // Uniform shadow alpha — stencil buffer prevents overlap darkening,
    // giving a clean single unified shadow for body + weapon + shield.
    FragColor = vec4(0.0, 0.0, 0.0, 0.25);
}

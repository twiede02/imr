#version 460 core
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
    // Miss hit: set background color (e.g., black)
    payload = vec3(0.0, 0.0, 0.0);
}


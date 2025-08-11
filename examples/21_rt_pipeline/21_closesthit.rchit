#version 460 core
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
    // Hit: set hit color (e.g., red)
    payload = vec3(1.0, 0.0, 0.0);
}


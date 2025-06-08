#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

struct Tri { vec3 v0, v1, v2; vec3 color; };

layout(scalar, buffer_reference) buffer TrianglesBuffer {
    Tri triangles[12];
};

layout(scalar, push_constant) uniform T {
	TrianglesBuffer triangles_buffer;
    uint triangles_count;
    mat4 matrix;
	float time;
} push_constants;

layout(location = 0)
out vec3 color;

void main() {
    mat4 matrix = push_constants.matrix;
    uint triId = gl_VertexIndex / 3;
    uint triVtxId = gl_VertexIndex % 3;
    Tri tri = push_constants.triangles_buffer.triangles[triId];
    vec3 vertex;
    if (triVtxId == 0)
        vertex = tri.v0;
    if (triVtxId == 1)
        vertex = tri.v1;
    if (triVtxId == 2)
        vertex = tri.v2;
    gl_Position = matrix * vec4(vertex, 1.0);
    color = tri.color;
}
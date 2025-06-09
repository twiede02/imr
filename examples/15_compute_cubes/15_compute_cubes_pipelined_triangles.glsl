#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Tri { vec3 v0, v1, v2; vec3 color; };

layout(scalar, buffer_reference) buffer TrianglesBuffer {
    Tri triangles[12];
};

layout(scalar, buffer_reference) buffer MatricesBuffer {
    mat4 matrices[16];
};

struct PreprocessedTri {
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec2 ss_v0;
    vec2 ss_v1;
    vec2 ss_v2;
    vec3 color;
};

layout(scalar, buffer_reference) buffer PreprocessedTrianglesBuffer {
    PreprocessedTri triangles[192];
};

layout(scalar, push_constant) uniform T {
	TrianglesBuffer triangles_buffer;
    uint triangles_count;
    MatricesBuffer matrices_buffer;
    uint matrices_count;
    PreprocessedTrianglesBuffer output_buffer;
	float time;
} push_constants;

PreprocessedTri processTri(Tri tri, mat4 matrix) {
    vec4 v0 = matrix * vec4(tri.v0, 1);
    vec4 v1 = matrix * vec4(tri.v1, 1);
    vec4 v2 = matrix * vec4(tri.v2, 1);
    vec2 ss_v0 = vec2(v0.xy) / v0.w;
    vec2 ss_v1 = vec2(v1.xy) / v1.w;
    vec2 ss_v2 = vec2(v2.xy) / v2.w;

    vec4 pixelColor = vec4(tri.color, 1);

    return PreprocessedTri(v0, v1, v2, ss_v0, ss_v1, ss_v2, tri.color);
}

void main() {
    if (gl_GlobalInvocationID.x >= push_constants.triangles_count
    || gl_GlobalInvocationID.y >= push_constants.matrices_count)
        return;

    uint tri_id = gl_GlobalInvocationID.y * push_constants.triangles_count + gl_GlobalInvocationID.x;

    mat4 matrix = push_constants.matrices_buffer.matrices[gl_GlobalInvocationID.y];
    push_constants.output_buffer.triangles[tri_id] = processTri(push_constants.triangles_buffer.triangles[gl_GlobalInvocationID.x], matrix);
}
#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0)
uniform image2D renderTarget;

layout(set = 0, binding = 1)
uniform image2D depthBuffer;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Tri { vec3 v0, v1, v2; vec3 color; };

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
    PreprocessedTrianglesBuffer preprocessed_triangles_buffer;
    uint triangles_count;
} push_constants;

float cross_2(vec2 a, vec2 b) {
    return cross(vec3(a, 0), vec3(b, 0)).z;
}

float barCoord(vec2 a, vec2 b, vec2 point){
    vec2 PA = point - a;
    vec2 BA = b - a;
    return cross_2(PA, BA);
}

vec3 barycentricTri2(vec2 v0, vec2 v1, vec2 v2, vec2 point) {
    float triangleArea = barCoord(v0.xy, v1.xy, v2.xy);

    float u = barCoord(v0.xy, v1.xy, point) / triangleArea;
    float v = barCoord(v1.xy, v2.xy, point) / triangleArea;

    return vec3(u, v, triangleArea);
}

bool is_inside_edge(vec2 e0, vec2 e1, vec2 p) {
    if (e1.x == e0.x)
    return (e1.x > p.x) ^^ (e0.y > e1.y);
    float a = (e1.y - e0.y) / (e1.x - e0.x);
    float b = e0.y + (0 - e0.x) * a;
    float ey = a * p.x + b;
    return (ey < p.y) ^^ (e0.x > e1.x);
}

void drawTri(PreprocessedTri tri, vec2 point) {
    vec4 v0 = tri.v0;
    vec4 v1 = tri.v1;
    vec4 v2 = tri.v2;
    vec2 ss_v0 = tri.ss_v0;
    vec2 ss_v1 = tri.ss_v1;
    vec2 ss_v2 = tri.ss_v2;

    vec4 pixelColor = vec4(tri.color, 1);

    bool backface = ((is_inside_edge(ss_v1.xy, ss_v0.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v1.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v0.xy, ss_v2.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0)));
    bool frontface = (is_inside_edge(ss_v0.xy, ss_v1.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v1.xy, ss_v2.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v0.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0));
    if (!frontface && !backface)
        return;

    vec3 baryResults = barycentricTri2(ss_v0.xy, ss_v1.xy, ss_v2.xy, point);
    float u = baryResults.x;
    float v = baryResults.y;
    float w = 1 - u - v;

    vec3 v_ws = vec3(v0.w, v1.w, v2.w);
    vec3 ss_v_coefs = vec3(v, w, u);
    vec3 pc_v_coefs = ss_v_coefs / v_ws;
    float pc_v_coefs_sum = pc_v_coefs.x + pc_v_coefs.y + pc_v_coefs.z;
    pc_v_coefs = pc_v_coefs / pc_v_coefs_sum;

    float depth = float(dot(ss_v_coefs, vec3(v0.z / v0.w, v1.z / v1.w, v2.z / v2.w)));

    if (depth < 0)
        return;

    float prevDepth = imageLoad(depthBuffer, ivec2(gl_GlobalInvocationID.xy)).x;
    if (depth < prevDepth)
        imageStore(depthBuffer, ivec2(gl_GlobalInvocationID.xy), vec4(depth));
    else
        return;

    //pixelColor = vec4(prevDepth);
    //pixelColor.r = 1;

    //vec4 previousPixelColor = imageLoad(renderTarget, ivec2(gl_GlobalInvocationID.xy));
    //pixelColor.rgb = mix(pixelColor.rgb, previousPixelColor.rgb, 0.5);
    imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), pixelColor);
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    if (gl_GlobalInvocationID.x >= img_size.x || gl_GlobalInvocationID.y >= img_size.y)
        return;

    vec2 point = vec2(gl_GlobalInvocationID.xy) / vec2(img_size);
    point = point * 2.0 - vec2(1.0);

    for (int i = 0; i < push_constants.triangles_count; i++) {
        drawTri(push_constants.preprocessed_triangles_buffer.triangles[i], point);
    }
}
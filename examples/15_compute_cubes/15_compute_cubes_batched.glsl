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

#define dvec3 vec3
#define dvec2 vec2
#define double float

layout(scalar, buffer_reference) buffer TrianglesBuffer {
    Tri triangles[12];
};

layout(scalar, push_constant) uniform T {
	TrianglesBuffer triangles_buffer;
    uint triangles_count;
    mat4 m;
	float time;
} push_constants;

double cross_2(dvec2 a, dvec2 b) {
    return cross(dvec3(a, 0), dvec3(b, 0)).z;
}

double barCoord(dvec2 a, dvec2 b, dvec2 point){
    dvec2 PA = point - a;
    dvec2 BA = b - a;
    return cross_2(PA, BA);
}

dvec3 barycentricTri2(dvec2 v0, dvec2 v1, dvec2 v2, dvec2 point) {
    double triangleArea = barCoord(v0.xy, v1.xy, v2.xy);

    double u = barCoord(v0.xy, v1.xy, point) / triangleArea;
    double v = barCoord(v1.xy, v2.xy, point) / triangleArea;

    return dvec3(u, v, triangleArea);
}

bool is_inside_edge(dvec2 e0, dvec2 e1, dvec2 p) {
    if (e1.x == e0.x)
    return (e1.x > p.x) ^^ (e0.y > e1.y);
    double a = (e1.y - e0.y) / (e1.x - e0.x);
    double b = e0.y + (0 - e0.x) * a;
    double ey = a * p.x + b;
    return (ey < p.y) ^^ (e0.x > e1.x);
}

void drawTri(Tri tri, dvec2 point) {
    vec4 os_v0 = vec4(tri.v0, 1);
    vec4 os_v1 = vec4(tri.v1, 1);
    vec4 os_v2 = vec4(tri.v2, 1);
    vec4 v0 = push_constants.m * os_v0;
    vec4 v1 = push_constants.m * os_v1;
    vec4 v2 = push_constants.m * os_v2;
    dvec2 ss_v0 = dvec2(v0.xy) / v0.w;
    dvec2 ss_v1 = dvec2(v1.xy) / v1.w;
    dvec2 ss_v2 = dvec2(v2.xy) / v2.w;

    vec4 pixelColor = vec4(tri.color, 1);

    bool backface = ((is_inside_edge(ss_v1.xy, ss_v0.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v1.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v0.xy, ss_v2.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0)));
    bool frontface = (is_inside_edge(ss_v0.xy, ss_v1.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v1.xy, ss_v2.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v0.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0));
    if (!frontface && !backface)
        return;

    dvec3 baryResults = barycentricTri2(ss_v0.xy, ss_v1.xy, ss_v2.xy, point);
    double u = baryResults.x;
    double v = baryResults.y;
    double w = 1 - u - v;

    dvec3 v_ws = vec3(v0.w, v1.w, v2.w);
    dvec3 ss_v_coefs = vec3(v, w, u);
    dvec3 pc_v_coefs = ss_v_coefs / v_ws;
    double pc_v_coefs_sum = pc_v_coefs.x + pc_v_coefs.y + pc_v_coefs.z;
    pc_v_coefs = pc_v_coefs / pc_v_coefs_sum;

    float depth = float(dot(ss_v_coefs, vec3(v0.z / v0.w, v1.z / v1.w, v2.z / v2.w)));

    float tcx = float(dot(vec3(os_v0.x, os_v1.x, os_v2.x), pc_v_coefs));
    float tcy = float(dot(vec3(os_v0.y, os_v1.y, os_v2.y), pc_v_coefs));
    float tcz = float(dot(vec3(os_v0.z, os_v1.z, os_v2.z), pc_v_coefs));
    vec3 tc = vec3(tcx, tcy, tcz);
    ivec3 tci = ivec3(tc * 16 + 0.001);
    pixelColor.xyz = vec3(1.0, 1.0, tc.x);
    if ((tci.x + tci.y + tci.z) % 2 == 0)
        pixelColor.xyz = vec3(1.0, 0.0, tc.y);

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

    dvec2 point = dvec2(gl_GlobalInvocationID.xy) / vec2(img_size);
    point = point * 2.0 - dvec2(1.0);

    for (int i = 0; i < push_constants.triangles_count; i++) {
        drawTri(push_constants.triangles_buffer.triangles[i], point);
    }
}
#version 450
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0)
uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

struct Tri { vec3 v0, v1, v2; vec3 color; };

layout(scalar, push_constant) uniform T {
	Tri triangle;
    mat4 m;
	float time;
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
    float scaling = barCoord(v0.xy, v1.xy, v2.xy);

    float u = barCoord(v0.xy, v1.xy, point) / scaling;
    float v = barCoord(v1.xy, v2.xy, point) / scaling;

    //if (scaling > 0)
    //    return vec3(-1);

    return vec3(u, v, scaling);
}

bool is_inside_edge(vec2 e0, vec2 e1, vec2 p) {
    if (e1.x == e0.x)
    return (e1.x > p.x) ^^ (e0.y > e1.y);
    float a = (e1.y - e0.y) / (e1.x - e0.x);
    float b = e0.y + (0 - e0.x) * a;
    float ey = a * p.x + b;
    return (ey < p.y) ^^ (e0.x > e1.x);
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    if (gl_GlobalInvocationID.x >= img_size.x || gl_GlobalInvocationID.y >= img_size.y)
        return;

    vec2 point = vec2(gl_GlobalInvocationID.xy) / vec2(img_size);
    point = point * 2.0 - vec2(1.0);

    vec4 os_v0 = vec4(push_constants.triangle.v0, 1);
    vec4 os_v1 = vec4(push_constants.triangle.v1, 1);
    vec4 os_v2 = vec4(push_constants.triangle.v2, 1);
    vec4 v0 = push_constants.m * os_v0;
    vec4 v1 = push_constants.m * os_v1;
    vec4 v2 = push_constants.m * os_v2;
    vec2 ss_v0 = v0.xy /= v0.w;
    vec2 ss_v1 = v1.xy /= v1.w;
    vec2 ss_v2 = v2.xy /= v2.w;

    vec3 baryResults = barycentricTri2(ss_v0.xy, ss_v1.xy, ss_v2.xy, point);
    float u = baryResults.x;
    float v = baryResults.y;
    float w = 1 - u - v;

    //if (!(u >= 0.0 && v >= 0.0 && w >= 0.0))
    //    return;

    vec4 c = vec4(push_constants.triangle.color, 1);

    bool backface = ((is_inside_edge(ss_v1.xy, ss_v0.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v1.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v0.xy, ss_v2.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0)));
    //if (not_backface)
    //    return;

    bool frontface = (is_inside_edge(ss_v0.xy, ss_v1.xy, point) ^^ (v0.w < 0) ^^ (v1.w < 0)) && (is_inside_edge(ss_v1.xy, ss_v2.xy, point) ^^ (v1.w < 0) ^^ (v2.w < 0)) && (is_inside_edge(ss_v2.xy, ss_v0.xy, point) ^^ (v2.w < 0) ^^ (v0.w < 0));
    if (!frontface && !backface)
        return;

    vec3 v_ws = vec3(v0.w, v1.w, v2.w);
    vec3 ss_v_coefs = vec3(v, w, u);
    vec3 pc_v_coefs = ss_v_coefs / v_ws;
    float pc_v_coefs_sum = pc_v_coefs.x + pc_v_coefs.y + pc_v_coefs.z;
    pc_v_coefs = pc_v_coefs / pc_v_coefs_sum;

    float depth = dot(pc_v_coefs, vec3(v0.w, v1.w, v2.w));

    float tcx = dot(vec3(os_v0.x, os_v1.x, os_v2.x), pc_v_coefs);
    float tcy = dot(vec3(os_v0.y, os_v1.y, os_v2.y), pc_v_coefs);
    float tcz = dot(vec3(os_v0.z, os_v1.z, os_v2.z), pc_v_coefs);
    vec3 tc = vec3(tcx, tcy, tcz);
    ivec3 tci = ivec3(tc * 16 + 0.001);
    c.xyz = vec3(1.0, 1.0, tc.x);
    if ((tci.x + tci.y + tci.z) % 2 == 0)
        c.xyz = vec3(1.0, 0.0, tc.y);

    if (depth < 0)
        return;
    //if (depth < 0 || depth > 1)
    //    return;

    //c.xyz = vec3(depth * 0.6 + 0.2, 0.25, 0.25);
    //c.xyz = vec3(tcx, tcy, tcz);

    vec4 p = imageLoad(renderTarget, ivec2(gl_GlobalInvocationID.xy));
    c.rgb = mix(c.rgb, p.rgb, 0.5);
    imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), c);
}
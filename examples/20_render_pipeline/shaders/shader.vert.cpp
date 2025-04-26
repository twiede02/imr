#include <stdint.h>
#include <shady.h>

#include "math.h"

using namespace vcc;
using namespace nasl;

#include "noise.h"

location(0) input native_vec3 inPosition;
location(1) input native_vec3 inColor;

push_constant struct constants1 {
    mat4 render_matrix;
    vec3 camera_pos;
} PushConstants;

location(0) output native_vec3 fragColor;
location(1) output native_vec2 fragUV;

float length(vec2 v) {
    return vcc::sqrtf(v.x * v.x + v.y * v.y);
}

extern "C" {

vertex_shader void main() {
    vec4 input_position = vec4(inPosition, 1.0);
    vec3 camera_pos = PushConstants.camera_pos;
    //vec4 camera_pos = PushConstants.render_matrix.rows[3];
    //camera_pos.xyz = (vec3) camera_pos.xyz / (float) camera_pos.w;
    //input_position.xz = input_position.xz - vec2(camera_pos.xz);
    float distance = fmaxf(fabs(input_position.x), fabs(input_position.z));

    //input_position.xz = input_position.xz + vec2(camera_pos.xz);
    input_position.xz = vec2(input_position.xz) * clamp( 2.0f + powf(4.0, distance * 4.0f + 0.0f), 1.0, 16000.0);
    input_position.xz = input_position.xz + vec2(camera_pos.xz);

    vec2 uv = input_position.xz;
    float f = perlin_noise(uv, true);
    //input_position.y = distance;
    input_position.y = 0.8f * f;

    gl_Position = PushConstants.render_matrix * input_position;
    fragUV = uv;
    fragColor = inColor;
}

}

/* vim: set filetype=cpp: */
